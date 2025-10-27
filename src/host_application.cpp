// src/host_application.cpp
#include "sparkplug/host_application.hpp"

#include "sparkplug/topic.hpp"

#include <cstring>
#include <format>
#include <future>
#include <thread>
#include <utility>

#include <MQTTAsync.h>

namespace sparkplug {

namespace {
constexpr int CONNECTION_TIMEOUT_MS = 5000;
constexpr int DISCONNECT_TIMEOUT_MS = 11000;

void on_connect_success(void* context, MQTTAsync_successData* response) {
  (void)response;
  auto* promise = static_cast<std::promise<void>*>(context);
  promise->set_value();
}

void on_connect_failure(void* context, MQTTAsync_failureData* response) {
  auto* promise = static_cast<std::promise<void>*>(context);
  auto error = std::format("Connection failed: code={}", response ? response->code : -1);
  promise->set_exception(std::make_exception_ptr(std::runtime_error(error)));
}

void on_disconnect_success(void* context, MQTTAsync_successData* response) {
  (void)response;
  auto* promise = static_cast<std::promise<void>*>(context);
  promise->set_value();
}

void on_disconnect_failure(void* context, MQTTAsync_failureData* response) {
  auto* promise = static_cast<std::promise<void>*>(context);
  auto error = std::format("Disconnect failed: code={}", response ? response->code : -1);
  promise->set_exception(std::make_exception_ptr(std::runtime_error(error)));
}

} // namespace

HostApplication::HostApplication(Config config) : config_(std::move(config)) {
}

HostApplication::~HostApplication() {
  if (client_ && is_connected_) {
    (void)disconnect();
  }
}

HostApplication::HostApplication(HostApplication&& other) noexcept
    : config_(std::move(other.config_)), client_(std::move(other.client_)),
      is_connected_(other.is_connected_)
// mutex_ is default-constructed (mutexes are not moveable)
{
  std::lock_guard<std::mutex> lock(other.mutex_);
  other.is_connected_ = false;
}

HostApplication& HostApplication::operator=(HostApplication&& other) noexcept {
  if (this != &other) {
    // Lock both mutexes in consistent order to avoid deadlock
    std::lock(mutex_, other.mutex_);
    std::lock_guard<std::mutex> lock1(mutex_, std::adopt_lock);
    std::lock_guard<std::mutex> lock2(other.mutex_, std::adopt_lock);

    config_ = std::move(other.config_);
    client_ = std::move(other.client_);
    is_connected_ = other.is_connected_;
    other.is_connected_ = false;
  }
  return *this;
}

void HostApplication::set_credentials(std::optional<std::string> username,
                                      std::optional<std::string> password) {
  std::lock_guard<std::mutex> lock(mutex_);
  config_.username = std::move(username);
  config_.password = std::move(password);
}

std::expected<void, std::string> HostApplication::connect() {
  std::lock_guard<std::mutex> lock(mutex_);

  MQTTAsync raw_client = nullptr;
  int rc = MQTTAsync_create(&raw_client, config_.broker_url.c_str(), config_.client_id.c_str(),
                            MQTTCLIENT_PERSISTENCE_NONE, nullptr);
  if (rc != MQTTASYNC_SUCCESS) {
    return std::unexpected(std::format("Failed to create client: {}", rc));
  }
  client_ = MQTTAsyncHandle(raw_client);

  MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;
  conn_opts.keepAliveInterval = config_.keep_alive_interval;
  conn_opts.cleansession = config_.clean_session;

  // Set credentials if provided
  if (config_.username.has_value()) {
    conn_opts.username = config_.username.value().c_str();
  }
  if (config_.password.has_value()) {
    conn_opts.password = config_.password.value().c_str();
  }

  MQTTAsync_SSLOptions ssl_opts = MQTTAsync_SSLOptions_initializer;
  if (config_.tls.has_value()) {
    const auto& tls = config_.tls.value();
    ssl_opts.trustStore = tls.trust_store.c_str();
    ssl_opts.keyStore = tls.key_store.empty() ? nullptr : tls.key_store.c_str();
    ssl_opts.privateKey = tls.private_key.empty() ? nullptr : tls.private_key.c_str();
    ssl_opts.privateKeyPassword =
        tls.private_key_password.empty() ? nullptr : tls.private_key_password.c_str();
    ssl_opts.enabledCipherSuites =
        tls.enabled_cipher_suites.empty() ? nullptr : tls.enabled_cipher_suites.c_str();
    ssl_opts.enableServerCertAuth = tls.enable_server_cert_auth;
    conn_opts.ssl = &ssl_opts;
  }

  // NOTE: No Last Will Testament for Host Applications (unlike Edge Nodes)
  // Host Applications should explicitly call publish_state_death() before disconnect()

  std::promise<void> connect_promise;
  auto connect_future = connect_promise.get_future();

  conn_opts.context = &connect_promise;
  conn_opts.onSuccess = on_connect_success;
  conn_opts.onFailure = on_connect_failure;

  rc = MQTTAsync_connect(client_.get(), &conn_opts);
  if (rc != MQTTASYNC_SUCCESS) {
    return std::unexpected(std::format("Failed to connect: {}", rc));
  }

  auto status = connect_future.wait_for(std::chrono::milliseconds(CONNECTION_TIMEOUT_MS));
  if (status == std::future_status::timeout) {
    return std::unexpected("Connection timeout");
  }

  try {
    connect_future.get();
  } catch (const std::exception& e) {
    return std::unexpected(e.what());
  }

  is_connected_ = true;
  return {};
}

std::expected<void, std::string> HostApplication::disconnect() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!client_) {
    return std::unexpected("Not connected");
  }

  std::promise<void> disconnect_promise;
  auto disconnect_future = disconnect_promise.get_future();

  MQTTAsync_disconnectOptions opts = MQTTAsync_disconnectOptions_initializer;
  opts.timeout = DISCONNECT_TIMEOUT_MS;
  opts.context = &disconnect_promise;
  opts.onSuccess = on_disconnect_success;
  opts.onFailure = on_disconnect_failure;

  int rc = MQTTAsync_disconnect(client_.get(), &opts);
  if (rc != MQTTASYNC_SUCCESS) {
    return std::unexpected(std::format("Failed to disconnect: {}", rc));
  }

  auto status = disconnect_future.wait_for(std::chrono::milliseconds(DISCONNECT_TIMEOUT_MS));
  if (status == std::future_status::timeout) {
    is_connected_ = false;
    return {};
  }

  try {
    disconnect_future.get();
  } catch (const std::exception&) {
  }

  is_connected_ = false;
  return {};
}

std::expected<void, std::string> HostApplication::publish_state_birth(uint64_t timestamp) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!is_connected_) {
    return std::unexpected("Not connected");
  }

  std::string json_payload = std::format("{{\"online\":true,\"timestamp\":{}}}", timestamp);

  std::string topic = std::format("STATE/{}", config_.host_id);

  std::vector<uint8_t> payload_data(json_payload.begin(), json_payload.end());

  return publish_raw_message(topic, payload_data, config_.qos, true);
}

std::expected<void, std::string> HostApplication::publish_state_death(uint64_t timestamp) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!is_connected_) {
    return std::unexpected("Not connected");
  }

  std::string json_payload = std::format("{{\"online\":false,\"timestamp\":{}}}", timestamp);

  std::string topic = std::format("STATE/{}", config_.host_id);

  std::vector<uint8_t> payload_data(json_payload.begin(), json_payload.end());

  return publish_raw_message(topic, payload_data, config_.qos, true);
}

std::expected<void, std::string> HostApplication::publish_node_command(
    std::string_view group_id, std::string_view target_edge_node_id, PayloadBuilder& payload) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!is_connected_) {
    return std::unexpected("Not connected");
  }

  Topic topic{.group_id = std::string(group_id),
              .message_type = MessageType::NCMD,
              .edge_node_id = std::string(target_edge_node_id),
              .device_id = ""};

  auto payload_data = payload.build();
  return publish_command_message(topic.to_string(), payload_data);
}

std::expected<void, std::string> HostApplication::publish_device_command(
    std::string_view group_id, std::string_view target_edge_node_id,
    std::string_view target_device_id, PayloadBuilder& payload) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!is_connected_) {
    return std::unexpected("Not connected");
  }

  Topic topic{.group_id = std::string(group_id),
              .message_type = MessageType::DCMD,
              .edge_node_id = std::string(target_edge_node_id),
              .device_id = std::string(target_device_id)};

  auto payload_data = payload.build();
  return publish_command_message(topic.to_string(), payload_data);
}

std::expected<void, std::string>
HostApplication::publish_raw_message(std::string_view topic, std::span<const uint8_t> payload_data,
                                     int qos, bool retain) {
  if (!client_ || !is_connected_) {
    return std::unexpected("Not connected");
  }

  MQTTAsync_message msg = MQTTAsync_message_initializer;
  msg.payload = const_cast<void*>(reinterpret_cast<const void*>(payload_data.data()));
  msg.payloadlen = static_cast<int>(payload_data.size());
  msg.qos = qos;
  msg.retained = retain ? 1 : 0;

  MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;

  int rc = MQTTAsync_sendMessage(client_.get(), std::string(topic).c_str(), &msg, &opts);
  if (rc != MQTTASYNC_SUCCESS) {
    return std::unexpected(std::format("Failed to publish: {}", rc));
  }

  return {};
}

std::expected<void, std::string>
HostApplication::publish_command_message(std::string_view topic,
                                         std::span<const uint8_t> payload_data) {
  if (!client_ || !is_connected_) {
    return std::unexpected("Not connected");
  }

  MQTTAsync_message msg = MQTTAsync_message_initializer;
  msg.payload = const_cast<void*>(reinterpret_cast<const void*>(payload_data.data()));
  msg.payloadlen = static_cast<int>(payload_data.size());
  msg.qos = config_.qos;
  msg.retained = 0;

  MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;

  int rc = MQTTAsync_sendMessage(client_.get(), std::string(topic).c_str(), &msg, &opts);
  if (rc != MQTTASYNC_SUCCESS) {
    return std::unexpected(std::format("Failed to publish: {}", rc));
  }

  return {};
}

} // namespace sparkplug
