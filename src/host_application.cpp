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
constexpr int CONNECTION_TIMEOUT_MS = 10000; // Increased from 5s to 10s
constexpr int DISCONNECT_TIMEOUT_MS = 11000;
constexpr uint64_t SEQ_NUMBER_MAX = 256;

void on_connect_success(void* context, MQTTAsync_successData* response) {
  (void)response;
  auto* promise = static_cast<std::promise<void>*>(context);
  promise->set_value();
}

void on_connect_failure(void* context, MQTTAsync_failureData* response) {
  auto* promise = static_cast<std::promise<void>*>(context);
  std::string error;
  if (response) {
    error = std::format("Connection failed: code={}, message={}", response->code,
                        response->message ? response->message : "none");
  } else {
    error = "Connection failed: no response data";
  }
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
  } else if (client_) {
    MQTTAsync_setCallbacks(client_.get(), nullptr, nullptr, nullptr, nullptr);
  }
}

HostApplication::HostApplication(HostApplication&& other) noexcept
    : config_(std::move(other.config_)), client_(std::move(other.client_)),
      is_connected_(other.is_connected_) {
  std::lock_guard<std::mutex> lock(other.mutex_);
  other.is_connected_ = false;
}

HostApplication& HostApplication::operator=(HostApplication&& other) noexcept {
  if (this != &other) {
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

void HostApplication::set_tls(std::optional<TlsOptions> tls) {
  std::lock_guard<std::mutex> lock(mutex_);
  config_.tls = std::move(tls);
}

void HostApplication::set_message_callback(MessageCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  config_.message_callback = std::move(callback);
}

void HostApplication::set_log_callback(LogCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  config_.log_callback = std::move(callback);
}

stdx::expected<void, std::string> HostApplication::connect() {
  std::lock_guard<std::mutex> lock(mutex_);

  MQTTAsync raw_client = nullptr;
  int rc = MQTTAsync_create(&raw_client, config_.broker_url.c_str(), config_.client_id.c_str(),
                            MQTTCLIENT_PERSISTENCE_NONE, nullptr);
  if (rc != MQTTASYNC_SUCCESS) {
    return stdx::unexpected(std::format("Failed to create client: {}", rc));
  }
  client_ = MQTTAsyncHandle(raw_client);

  rc = MQTTAsync_setCallbacks(client_.get(), this, on_connection_lost, on_message_arrived, nullptr);
  if (rc != MQTTASYNC_SUCCESS) {
    return stdx::unexpected(std::format("Failed to set callbacks: {}", rc));
  }

  MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;
  conn_opts.keepAliveInterval = config_.keep_alive_interval;
  conn_opts.cleansession = config_.clean_session;
  conn_opts.maxInflight = config_.max_inflight;

  if (config_.username.has_value()) {
    conn_opts.username = config_.username.value().c_str();
  }
  if (config_.password.has_value()) {
    conn_opts.password = config_.password.value().c_str();
  }

  ssl_opts_ = MQTTAsync_SSLOptions_initializer;
  if (config_.tls.has_value()) {
    const auto& tls = config_.tls.value();
    ssl_opts_.trustStore = tls.trust_store.c_str();
    ssl_opts_.keyStore = tls.key_store.empty() ? nullptr : tls.key_store.c_str();
    ssl_opts_.privateKey = tls.private_key.empty() ? nullptr : tls.private_key.c_str();
    ssl_opts_.privateKeyPassword =
        tls.private_key_password.empty() ? nullptr : tls.private_key_password.c_str();
    ssl_opts_.enabledCipherSuites =
        tls.enabled_cipher_suites.empty() ? nullptr : tls.enabled_cipher_suites.c_str();
    ssl_opts_.enableServerCertAuth = tls.enable_server_cert_auth;
    conn_opts.ssl = &ssl_opts_;
  }

  std::promise<void> connect_promise;
  auto connect_future = connect_promise.get_future();

  conn_opts.context = &connect_promise;
  conn_opts.onSuccess = on_connect_success;
  conn_opts.onFailure = on_connect_failure;

  rc = MQTTAsync_connect(client_.get(), &conn_opts);
  if (rc != MQTTASYNC_SUCCESS) {
    MQTTAsync_setCallbacks(client_.get(), nullptr, nullptr, nullptr, nullptr);
    return stdx::unexpected(std::format("Failed to connect: {}", rc));
  }

  auto status = connect_future.wait_for(std::chrono::milliseconds(CONNECTION_TIMEOUT_MS));
  if (status == std::future_status::timeout) {
    MQTTAsync_setCallbacks(client_.get(), nullptr, nullptr, nullptr, nullptr);
    MQTTAsync_disconnectOptions disc_opts = MQTTAsync_disconnectOptions_initializer;
    disc_opts.timeout = 1000;
    MQTTAsync_disconnect(client_.get(), &disc_opts);
    return stdx::unexpected("Connection timeout");
  }

  try {
    connect_future.get();
  } catch (const std::exception& e) {
    MQTTAsync_setCallbacks(client_.get(), nullptr, nullptr, nullptr, nullptr);
    return stdx::unexpected(e.what());
  }

  is_connected_ = true;
  return {};
}

stdx::expected<void, std::string> HostApplication::disconnect() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!client_) {
    return stdx::unexpected("Not connected");
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
    return stdx::unexpected(std::format("Failed to disconnect: {}", rc));
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

stdx::expected<void, std::string> HostApplication::publish_state_birth(uint64_t timestamp) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!is_connected_) {
    return stdx::unexpected("Not connected");
  }

  std::string json_payload = std::format("{{\"online\":true,\"timestamp\":{}}}", timestamp);

  std::string topic = std::format("{}/STATE/{}", NAMESPACE, config_.host_id);

  std::vector<uint8_t> payload_data(json_payload.begin(), json_payload.end());

  return publish_raw_message(topic, payload_data, config_.qos, true);
}

stdx::expected<void, std::string> HostApplication::publish_state_death(uint64_t timestamp) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!is_connected_) {
    return stdx::unexpected("Not connected");
  }

  std::string json_payload = std::format("{{\"online\":false,\"timestamp\":{}}}", timestamp);

  std::string topic = std::format("{}/STATE/{}", NAMESPACE, config_.host_id);

  std::vector<uint8_t> payload_data(json_payload.begin(), json_payload.end());

  return publish_raw_message(topic, payload_data, config_.qos, true);
}

stdx::expected<void, std::string> HostApplication::publish_node_command(
    std::string_view group_id, std::string_view target_edge_node_id, PayloadBuilder& payload) {
  std::string topic_str;
  std::vector<uint8_t> payload_data;
  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!is_connected_) {
      return stdx::unexpected("Not connected");
    }

    Topic topic{.group_id = std::string(group_id),
                .message_type = MessageType::NCMD,
                .edge_node_id = std::string(target_edge_node_id),
                .device_id = ""};

    topic_str = topic.to_string();
    payload_data = payload.build();
  }

  return publish_command_message(topic_str, payload_data);
}

stdx::expected<void, std::string> HostApplication::publish_device_command(
    std::string_view group_id, std::string_view target_edge_node_id,
    std::string_view target_device_id, PayloadBuilder& payload) {
  std::string topic_str;
  std::vector<uint8_t> payload_data;
  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!is_connected_) {
      return stdx::unexpected("Not connected");
    }

    Topic topic{.group_id = std::string(group_id),
                .message_type = MessageType::DCMD,
                .edge_node_id = std::string(target_edge_node_id),
                .device_id = std::string(target_device_id)};

    topic_str = topic.to_string();
    payload_data = payload.build();
  }

  return publish_command_message(topic_str, payload_data);
}

stdx::expected<void, std::string>
HostApplication::publish_raw_message(std::string_view topic, std::span<const uint8_t> payload_data,
                                     int qos, bool retain) {
  if (!client_ || !is_connected_) {
    return stdx::unexpected("Not connected");
  }

  MQTTAsync_message msg = MQTTAsync_message_initializer;
  msg.payload = const_cast<void*>(reinterpret_cast<const void*>(payload_data.data()));
  msg.payloadlen = static_cast<int>(payload_data.size());
  msg.qos = qos;
  msg.retained = retain ? 1 : 0;

  MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;

  int rc = MQTTAsync_sendMessage(client_.get(), std::string(topic).c_str(), &msg, &opts);
  if (rc != MQTTASYNC_SUCCESS) {
    return stdx::unexpected(std::format("Failed to publish: {}", rc));
  }

  return {};
}

stdx::expected<void, std::string>
HostApplication::publish_command_message(std::string_view topic,
                                         std::span<const uint8_t> payload_data) {
  if (!client_ || !is_connected_) {
    return stdx::unexpected("Not connected");
  }

  MQTTAsync_message msg = MQTTAsync_message_initializer;
  msg.payload = const_cast<void*>(reinterpret_cast<const void*>(payload_data.data()));
  msg.payloadlen = static_cast<int>(payload_data.size());
  msg.qos = config_.qos;
  msg.retained = 0;

  MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;

  int rc = MQTTAsync_sendMessage(client_.get(), std::string(topic).c_str(), &msg, &opts);
  if (rc != MQTTASYNC_SUCCESS) {
    return stdx::unexpected(std::format("Failed to publish: {}", rc));
  }

  return {};
}

stdx::expected<void, std::string> HostApplication::subscribe_all_groups() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!client_) {
    return stdx::unexpected("Not connected");
  }

  std::string topic = std::format("{}/#", NAMESPACE);

  MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;

  int rc = MQTTAsync_subscribe(client_.get(), topic.c_str(), config_.qos, &opts);
  if (rc != MQTTASYNC_SUCCESS) {
    return stdx::unexpected(std::format("Failed to subscribe: {}", rc));
  }

  return {};
}

stdx::expected<void, std::string> HostApplication::subscribe_group(std::string_view group_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!client_) {
    return stdx::unexpected("Not connected");
  }

  std::string topic = std::format("{}/{}/#", NAMESPACE, group_id);

  MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;

  int rc = MQTTAsync_subscribe(client_.get(), topic.c_str(), config_.qos, &opts);
  if (rc != MQTTASYNC_SUCCESS) {
    return stdx::unexpected(std::format("Failed to subscribe: {}", rc));
  }

  return {};
}

stdx::expected<void, std::string> HostApplication::subscribe_node(std::string_view group_id,
                                                                  std::string_view edge_node_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!client_) {
    return stdx::unexpected("Not connected");
  }

  std::string topic = std::format("{}/{}/+/{}/#", NAMESPACE, group_id, edge_node_id);

  MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;

  int rc = MQTTAsync_subscribe(client_.get(), topic.c_str(), config_.qos, &opts);
  if (rc != MQTTASYNC_SUCCESS) {
    return stdx::unexpected(std::format("Failed to subscribe: {}", rc));
  }

  return {};
}

stdx::expected<void, std::string> HostApplication::subscribe_state(std::string_view host_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!client_) {
    return stdx::unexpected("Not connected");
  }

  std::string topic = std::format("{}/STATE/{}", NAMESPACE, host_id);

  MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;

  int rc = MQTTAsync_subscribe(client_.get(), topic.c_str(), config_.qos, &opts);
  if (rc != MQTTASYNC_SUCCESS) {
    return stdx::unexpected(std::format("Failed to subscribe: {}", rc));
  }

  return {};
}

std::optional<std::reference_wrapper<const HostApplication::NodeState>>
HostApplication::get_node_state(std::string_view group_id, std::string_view edge_node_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = node_states_.find(std::make_pair(group_id, edge_node_id));
  if (it != node_states_.end()) {
    return std::cref(it->second);
  }
  return std::nullopt;
}

std::optional<std::string_view> HostApplication::get_metric_name(std::string_view group_id,
                                                                 std::string_view edge_node_id,
                                                                 std::string_view device_id,
                                                                 uint64_t alias) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = node_states_.find(std::make_pair(group_id, edge_node_id));
  if (it == node_states_.end()) {
    return std::nullopt;
  }

  const auto& node_state = it->second;

  if (!device_id.empty()) {
    auto device_it = node_state.devices.find(device_id);
    if (device_it == node_state.devices.end()) {
      return std::nullopt;
    }

    const auto& device_state = device_it->second;
    auto alias_it = device_state.alias_map.find(alias);
    if (alias_it != device_state.alias_map.end()) {
      return std::string_view(alias_it->second);
    }
    return std::nullopt;
  }

  auto alias_it = node_state.alias_map.find(alias);
  if (alias_it != node_state.alias_map.end()) {
    return std::string_view(alias_it->second);
  }
  return std::nullopt;
}

void HostApplication::log(LogLevel level, std::string_view message) const noexcept {
  if (config_.log_callback) {
    config_.log_callback(level, message);
  }
}

bool HostApplication::validate_message(const Topic& topic,
                                       const org::eclipse::tahu::protobuf::Payload& payload) {
  if (!config_.validate_sequence) {
    return true;
  }

  NodeKey key{topic.group_id, topic.edge_node_id};
  auto& state = node_states_[key];
  const std::string node_id = topic.group_id + "/" + topic.edge_node_id;

  switch (topic.message_type) {
  case MessageType::NBIRTH: {
    if (payload.has_seq() && payload.seq() != 0) {
      log(LogLevel::WARN,
          std::format("NBIRTH for {} has invalid seq: {} (expected 0)", node_id, payload.seq()));
      return false;
    }

    uint64_t bd_seq = 0;
    bool has_bdseq = false;
    for (const auto& metric : payload.metrics()) {
      if (metric.name() == "bdSeq") {
        bd_seq = metric.long_value();
        has_bdseq = true;
        break;
      }
    }

    if (!has_bdseq) {
      log(LogLevel::WARN, std::format("NBIRTH for {} missing required bdSeq metric", node_id));
      return false;
    }

    state.bd_seq = bd_seq;
    state.last_seq = 0;
    state.is_online = true;
    state.birth_received = true;
    state.birth_timestamp = payload.timestamp();

    state.alias_map.clear();
    for (const auto& metric : payload.metrics()) {
      if (metric.has_alias() && metric.has_name()) {
        state.alias_map[metric.alias()] = metric.name();
      }
    }

    return true;
  }

  case MessageType::NDEATH: {
    uint64_t bd_seq = 0;
    for (const auto& metric : payload.metrics()) {
      if (metric.name() == "bdSeq") {
        bd_seq = metric.long_value();
        break;
      }
    }

    if (state.birth_received && bd_seq != state.bd_seq) {
      log(LogLevel::WARN, std::format("NDEATH bdSeq mismatch for {} (NDEATH: {}, NBIRTH: {})",
                                      node_id, bd_seq, state.bd_seq));
    }

    state.is_online = false;
    return true;
  }

  case MessageType::NDATA: {
    if (!state.birth_received) {
      log(LogLevel::WARN, std::format("Received NDATA for {} before NBIRTH", node_id));
      return false;
    }

    if (payload.has_seq()) {
      uint64_t seq = payload.seq();
      uint64_t expected_seq = (state.last_seq + 1) % SEQ_NUMBER_MAX;

      if (seq != expected_seq) {
        log(LogLevel::WARN, std::format("Sequence number gap for {} (got {}, expected {})", node_id,
                                        seq, expected_seq));
      }

      state.last_seq = seq;
    }

    return true;
  }

  case MessageType::DBIRTH: {
    if (!state.birth_received) {
      log(LogLevel::WARN,
          std::format("Received DBIRTH for device on {} before node NBIRTH", node_id));
      return false;
    }

    if (payload.has_seq()) {
      uint64_t seq = payload.seq();
      uint64_t expected_seq = (state.last_seq + 1) % SEQ_NUMBER_MAX;

      if (seq != expected_seq) {
        log(LogLevel::WARN,
            std::format("Sequence number gap for DBIRTH device '{}' on {} (got {}, expected {})",
                        topic.device_id, node_id, seq, expected_seq));
      }

      state.last_seq = seq;
    }

    auto& device_state = state.devices[topic.device_id];
    device_state.is_online = true;
    device_state.birth_received = true;

    device_state.alias_map.clear();
    for (const auto& metric : payload.metrics()) {
      if (metric.has_alias() && metric.has_name()) {
        device_state.alias_map[metric.alias()] = metric.name();
      }
    }

    return true;
  }

  case MessageType::DDATA: {
    if (!state.birth_received) {
      log(LogLevel::WARN, std::format("Received DDATA for device '{}' on {} before node NBIRTH",
                                      topic.device_id, node_id));
      return false;
    }

    auto device_it = state.devices.find(topic.device_id);
    if (device_it == state.devices.end() || !device_it->second.birth_received) {
      log(LogLevel::WARN, std::format("Received DDATA for device '{}' on {} before DBIRTH",
                                      topic.device_id, node_id));
      return false;
    }

    if (payload.has_seq()) {
      uint64_t seq = payload.seq();
      uint64_t expected_seq = (state.last_seq + 1) % SEQ_NUMBER_MAX;

      if (seq != expected_seq) {
        log(LogLevel::WARN,
            std::format("Sequence number gap for device '{}' on {} (got {}, expected {})",
                        topic.device_id, node_id, seq, expected_seq));
      }

      state.last_seq = seq;
    }

    return true;
  }

  case MessageType::DDEATH: {
    auto device_it = state.devices.find(topic.device_id);
    if (device_it != state.devices.end()) {
      device_it->second.is_online = false;
    }
    return true;
  }

  case MessageType::NCMD:
  case MessageType::DCMD:
  case MessageType::STATE:
    return true;
  }
  std::unreachable();
}

int HostApplication::on_message_arrived(void* context, char* topicName, int topicLen,
                                        MQTTAsync_message* message) {
  auto* host_app = static_cast<HostApplication*>(context);

  if (!host_app || !topicName || !message) {
    if (message) {
      MQTTAsync_freeMessage(&message);
      MQTTAsync_free(topicName);
    }
    return 1;
  }

  std::string topic_str(topicName, topicLen > 0 ? topicLen : strlen(topicName));

  std::string state_prefix = std::format("{}/STATE/", NAMESPACE);
  if (topic_str.starts_with(state_prefix)) {
    std::string state_value(static_cast<char*>(message->payload), message->payloadlen);

    org::eclipse::tahu::protobuf::Payload dummy_payload;

    Topic state_topic{.group_id = "",
                      .message_type = MessageType::STATE,
                      .edge_node_id = topic_str.substr(state_prefix.length()),
                      .device_id = ""};

    if (host_app->config_.message_callback) {
      try {
        host_app->config_.message_callback(state_topic, dummy_payload);
      } catch (...) {
      }
    }

    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(topicName);
    return 1;
  }

  auto topic_result = Topic::parse(topic_str);

  if (!topic_result) {
    host_app->log(LogLevel::DEBUG, std::format("Ignoring non-Sparkplug topic: {}", topic_str));
    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(topicName);
    return 1;
  }

  org::eclipse::tahu::protobuf::Payload payload;
  if (!payload.ParseFromArray(message->payload, message->payloadlen)) {
    host_app->log(LogLevel::ERROR, "Failed to parse Sparkplug B payload");
    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(topicName);
    return 1;
  }

  {
    std::lock_guard<std::mutex> lock(host_app->mutex_);
    host_app->validate_message(*topic_result, payload);
  }

  if (host_app->config_.message_callback) {
    try {
      host_app->config_.message_callback(*topic_result, payload);
    } catch (...) {
    }
  }

  MQTTAsync_freeMessage(&message);
  MQTTAsync_free(topicName);
  return 1;
}

void HostApplication::on_connection_lost(void* context, char* cause) {
  auto* host_app = static_cast<HostApplication*>(context);
  if (!host_app) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(host_app->mutex_);
    host_app->is_connected_ = false;
  }

  if (cause) {
    host_app->log(LogLevel::WARN, std::format("Connection lost: {}", cause));
  } else {
    host_app->log(LogLevel::WARN, "Connection lost");
  }
}

} // namespace sparkplug
