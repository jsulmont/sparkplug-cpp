// src/publisher.cpp
#include "sparkplug/publisher.hpp"

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
constexpr int SUBSCRIBE_TIMEOUT_MS = 5000;
constexpr uint64_t SEQ_NUMBER_MAX = 256;

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

void on_subscribe_success(void* context, MQTTAsync_successData* response) {
  (void)response;
  auto* promise = static_cast<std::promise<void>*>(context);
  promise->set_value();
}

void on_subscribe_failure(void* context, MQTTAsync_failureData* response) {
  auto* promise = static_cast<std::promise<void>*>(context);
  auto error = std::format("Subscribe failed: code={}", response ? response->code : -1);
  promise->set_exception(std::make_exception_ptr(std::runtime_error(error)));
}

} // namespace

MQTTAsyncHandle::~MQTTAsyncHandle() noexcept {
  reset();
}

void MQTTAsyncHandle::reset() noexcept {
  if (client_) {
    MQTTAsync_destroy(&client_);
    client_ = nullptr;
  }
}

Publisher::Publisher(Config config) : config_(std::move(config)) {
}

int Publisher::on_message_arrived(void* context, char* topicName, int topicLen,
                                  MQTTAsync_message* message) {
  auto* publisher = static_cast<Publisher*>(context);

  std::string topic_str;
  if (topicLen > 0) {
    topic_str = std::string(topicName, static_cast<size_t>(topicLen));
  } else {
    topic_str = std::string(topicName);
  }

  auto topic_result = Topic::parse(topic_str);
  if (!topic_result) {
    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(topicName);
    return 1;
  }

  const auto& topic = topic_result.value();

  if (topic.message_type == MessageType::NCMD && publisher->config_.command_callback) {
    org::eclipse::tahu::protobuf::Payload payload;
    if (payload.ParseFromArray(message->payload, message->payloadlen)) {
      publisher->config_.command_callback.value()(topic, payload);
    }
  }

  MQTTAsync_freeMessage(&message);
  MQTTAsync_free(topicName);
  return 1;
}

Publisher::~Publisher() {
  if (client_ && is_connected_) {
    (void)disconnect();
  }
}

Publisher::Publisher(Publisher&& other) noexcept
    : config_(std::move(other.config_)), client_(std::move(other.client_)),
      seq_num_(other.seq_num_), bd_seq_num_(other.bd_seq_num_),
      death_payload_data_(std::move(other.death_payload_data_)),
      last_birth_payload_(std::move(other.last_birth_payload_)),
      device_states_(std::move(other.device_states_)), is_connected_(other.is_connected_)
// mutex_ is default-constructed (mutexes are not moveable)
{
  std::lock_guard<std::mutex> lock(other.mutex_);
  other.is_connected_ = false;
}

Publisher& Publisher::operator=(Publisher&& other) noexcept {
  if (this != &other) {
    // Lock both mutexes in consistent order to avoid deadlock
    std::lock(mutex_, other.mutex_);
    std::lock_guard<std::mutex> lock1(mutex_, std::adopt_lock);
    std::lock_guard<std::mutex> lock2(other.mutex_, std::adopt_lock);

    config_ = std::move(other.config_);
    client_ = std::move(other.client_);
    seq_num_ = other.seq_num_;
    bd_seq_num_ = other.bd_seq_num_;
    death_payload_data_ = std::move(other.death_payload_data_);
    last_birth_payload_ = std::move(other.last_birth_payload_);
    device_states_ = std::move(other.device_states_);
    is_connected_ = other.is_connected_;
    other.is_connected_ = false;
  }
  return *this;
}

void Publisher::set_credentials(std::optional<std::string> username,
                                std::optional<std::string> password) {
  std::lock_guard<std::mutex> lock(mutex_);
  config_.username = std::move(username);
  config_.password = std::move(password);
}

void Publisher::set_tls(std::optional<TlsOptions> tls) {
  std::lock_guard<std::mutex> lock(mutex_);
  config_.tls = std::move(tls);
}

std::expected<void, std::string> Publisher::connect() {
  std::lock_guard<std::mutex> lock(mutex_);

  MQTTAsync raw_client = nullptr;
  int rc = MQTTAsync_create(&raw_client, config_.broker_url.c_str(), config_.client_id.c_str(),
                            MQTTCLIENT_PERSISTENCE_NONE, nullptr);
  if (rc != MQTTASYNC_SUCCESS) {
    return std::unexpected(std::format("Failed to create client: {}", rc));
  }
  client_ = MQTTAsyncHandle(raw_client);

  // Prepare NDEATH payload BEFORE connecting
  PayloadBuilder death_payload;
  death_payload.add_metric("bdSeq", bd_seq_num_);
  death_payload_data_ = death_payload.build();

  MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;
  conn_opts.keepAliveInterval = config_.keep_alive_interval;
  conn_opts.cleansession = config_.clean_session;

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

  // Setup Last Will and Testament (NDEATH)
  // Initialize will options as member variable (must outlive async connect)
  will_opts_ = MQTTAsync_willOptions_initializer;

  Topic death_topic{.group_id = config_.group_id,
                    .message_type = MessageType::NDEATH,
                    .edge_node_id = config_.edge_node_id,
                    .device_id = ""};

  // Store as member variable to keep string alive for async MQTT operations
  death_topic_str_ = death_topic.to_string();
  will_opts_.topicName = death_topic_str_.c_str();

  // Use payload.data/len for binary protobuf data
  will_opts_.payload.data = death_payload_data_.data();
  will_opts_.payload.len = static_cast<int>(death_payload_data_.size());
  will_opts_.retained = 0;
  will_opts_.qos = config_.death_qos;

  conn_opts.will = &will_opts_;

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

  if (config_.command_callback.has_value()) {
    rc = MQTTAsync_setCallbacks(client_.get(), this, nullptr, on_message_arrived, nullptr);
    if (rc != MQTTASYNC_SUCCESS) {
      return std::unexpected(std::format("Failed to set message callback: {}", rc));
    }

    Topic ncmd_topic{.group_id = config_.group_id,
                     .message_type = MessageType::NCMD,
                     .edge_node_id = config_.edge_node_id,
                     .device_id = ""};

    auto ncmd_topic_str = ncmd_topic.to_string();

    std::promise<void> subscribe_promise;
    auto subscribe_future = subscribe_promise.get_future();

    MQTTAsync_responseOptions sub_opts = MQTTAsync_responseOptions_initializer;
    sub_opts.context = &subscribe_promise;
    sub_opts.onSuccess = on_subscribe_success;
    sub_opts.onFailure = on_subscribe_failure;

    rc = MQTTAsync_subscribe(client_.get(), ncmd_topic_str.c_str(), 1, &sub_opts);
    if (rc != MQTTASYNC_SUCCESS) {
      return std::unexpected(std::format("Failed to subscribe to NCMD: {}", rc));
    }

    auto sub_status = subscribe_future.wait_for(std::chrono::milliseconds(SUBSCRIBE_TIMEOUT_MS));
    if (sub_status == std::future_status::timeout) {
      return std::unexpected("NCMD subscription timeout");
    }

    try {
      subscribe_future.get();
    } catch (const std::exception& e) {
      return std::unexpected(std::format("NCMD subscription failed: {}", e.what()));
    }
  }

  return {};
}

std::expected<void, std::string> Publisher::disconnect() {
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

std::expected<void, std::string> Publisher::publish_message(const Topic& topic,
                                                            std::span<const uint8_t> payload_data) {
  // Note: mutex already held by caller
  if (!client_ || !is_connected_) {
    return std::unexpected("Not connected");
  }

  auto topic_str = topic.to_string();

  MQTTAsync_message msg = MQTTAsync_message_initializer;
  // Note: const_cast required due to Paho MQTT C API (non-const payload pointer).
  // This is safe because MQTTAsync_sendMessage does not modify the payload.
  msg.payload = const_cast<void*>(reinterpret_cast<const void*>(payload_data.data()));
  msg.payloadlen = static_cast<int>(payload_data.size());
  msg.qos = config_.data_qos;
  msg.retained = 0;

  MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;

  int rc = MQTTAsync_sendMessage(client_.get(), topic_str.c_str(), &msg, &opts);
  if (rc != MQTTASYNC_SUCCESS) {
    return std::unexpected(std::format("Failed to publish: {}", rc));
  }

  return {};
}

std::expected<void, std::string> Publisher::publish_birth(PayloadBuilder& payload) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!is_connected_) {
    return std::unexpected("Not connected");
  }

  payload.set_seq(0);

  bool has_bdseq = false;
  auto& proto_payload = payload.mutable_payload();

  for (const auto& metric : proto_payload.metrics()) {
    if (metric.name() == "bdSeq") {
      has_bdseq = true;
      break;
    }
  }

  if (!has_bdseq) {
    auto* metric = proto_payload.add_metrics();
    metric->set_name("bdSeq");
    metric->set_datatype(std::to_underlying(DataType::UInt64));
    metric->set_long_value(bd_seq_num_);
  }

  Topic topic{.group_id = config_.group_id,
              .message_type = MessageType::NBIRTH,
              .edge_node_id = config_.edge_node_id,
              .device_id = ""};

  auto payload_data = payload.build();

  auto result = publish_message(topic, payload_data);
  if (!result) {
    return result;
  }

  last_birth_payload_ = std::move(payload_data);

  seq_num_ = 0;
  bd_seq_num_++;

  return {};
}

std::expected<void, std::string> Publisher::publish_data(PayloadBuilder& payload) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!is_connected_) {
    return std::unexpected("Not connected");
  }

  seq_num_ = (seq_num_ + 1) % SEQ_NUMBER_MAX;

  if (!payload.has_seq()) {
    payload.set_seq(seq_num_);
  }

  Topic topic{.group_id = config_.group_id,
              .message_type = MessageType::NDATA,
              .edge_node_id = config_.edge_node_id,
              .device_id = ""};

  auto payload_data = payload.build();
  return publish_message(topic, payload_data);
}

std::expected<void, std::string> Publisher::publish_death() {
  std::unique_lock<std::mutex> lock(mutex_);

  if (!is_connected_) {
    return std::unexpected("Not connected");
  }

  Topic topic{.group_id = config_.group_id,
              .message_type = MessageType::NDEATH,
              .edge_node_id = config_.edge_node_id,
              .device_id = ""};

  auto result = publish_message(topic, death_payload_data_);
  if (!result) {
    return result;
  }

  // Unlock before calling disconnect() to avoid deadlock
  lock.unlock();
  return disconnect();
}

std::expected<void, std::string> Publisher::rebirth() {
  std::unique_lock<std::mutex> lock(mutex_);

  if (!is_connected_) {
    return std::unexpected("Not connected");
  }

  if (last_birth_payload_.empty()) {
    return std::unexpected("No previous birth payload stored");
  }

  org::eclipse::tahu::protobuf::Payload proto_payload;
  if (!proto_payload.ParseFromArray(last_birth_payload_.data(),
                                    static_cast<int>(last_birth_payload_.size()))) {
    return std::unexpected("Failed to parse stored birth payload");
  }

  bd_seq_num_++;

  for (auto& metric : *proto_payload.mutable_metrics()) {
    if (metric.name() == "bdSeq") {
      metric.set_long_value(bd_seq_num_);
      break;
    }
  }

  proto_payload.set_seq(0);

  std::vector<uint8_t> payload_data(proto_payload.ByteSizeLong());
  proto_payload.SerializeToArray(payload_data.data(), static_cast<int>(payload_data.size()));
  last_birth_payload_ = payload_data;

  // Disconnect (sends old NDEATH), then reconnect (sets new NDEATH with new bdSeq)
  Topic topic{.group_id = config_.group_id,
              .message_type = MessageType::NBIRTH,
              .edge_node_id = config_.edge_node_id,
              .device_id = ""};

  // Unlock before calling disconnect/connect to avoid deadlock
  lock.unlock();

  auto result = disconnect()
                    .and_then([this]() { return connect(); })
                    .and_then([this, &topic, &payload_data]() {
                      std::lock_guard<std::mutex> lock(mutex_);
                      return publish_message(topic, payload_data);
                    });

  if (!result) {
    return result;
  }

  // Reacquire lock to modify seq_num_
  lock.lock();
  seq_num_ = 0;

  return {};
}

std::expected<void, std::string> Publisher::publish_device_birth(std::string_view device_id,
                                                                 PayloadBuilder& payload) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!is_connected_) {
    return std::unexpected("Not connected");
  }

  if (last_birth_payload_.empty()) {
    return std::unexpected("Must publish NBIRTH before DBIRTH");
  }

  payload.set_seq(0);

  Topic topic{.group_id = config_.group_id,
              .message_type = MessageType::DBIRTH,
              .edge_node_id = config_.edge_node_id,
              .device_id = std::string(device_id)};

  auto payload_data = payload.build();

  auto result = publish_message(topic, payload_data);
  if (!result) {
    return result;
  }

  auto& device_state = device_states_[std::string(device_id)];
  device_state.seq_num = 0;
  device_state.last_birth_payload = std::move(payload_data);
  device_state.is_online = true;

  return {};
}

std::expected<void, std::string> Publisher::publish_device_data(std::string_view device_id,
                                                                PayloadBuilder& payload) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!is_connected_) {
    return std::unexpected("Not connected");
  }

  auto it = device_states_.find(device_id);
  if (it == device_states_.end() || !it->second.is_online) {
    return std::unexpected(
        std::format("Must publish DBIRTH for device '{}' before DDATA", device_id));
  }

  auto& device_state = it->second;
  device_state.seq_num = (device_state.seq_num + 1) % SEQ_NUMBER_MAX;

  if (!payload.has_seq()) {
    payload.set_seq(device_state.seq_num);
  }

  Topic topic{.group_id = config_.group_id,
              .message_type = MessageType::DDATA,
              .edge_node_id = config_.edge_node_id,
              .device_id = std::string(device_id)};

  auto payload_data = payload.build();
  return publish_message(topic, payload_data);
}

std::expected<void, std::string> Publisher::publish_device_death(std::string_view device_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!is_connected_) {
    return std::unexpected("Not connected");
  }

  auto it = device_states_.find(device_id);
  if (it == device_states_.end()) {
    return std::unexpected(std::format("Unknown device: '{}'", device_id));
  }

  PayloadBuilder death_payload;

  Topic topic{.group_id = config_.group_id,
              .message_type = MessageType::DDEATH,
              .edge_node_id = config_.edge_node_id,
              .device_id = std::string(device_id)};

  auto payload_data = death_payload.build();
  auto result = publish_message(topic, payload_data);
  if (!result) {
    return result;
  }

  it->second.is_online = false;
  return {};
}

std::expected<void, std::string>
Publisher::publish_node_command(std::string_view target_edge_node_id, PayloadBuilder& payload) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!is_connected_) {
    return std::unexpected("Not connected");
  }

  Topic topic{.group_id = config_.group_id,
              .message_type = MessageType::NCMD,
              .edge_node_id = std::string(target_edge_node_id),
              .device_id = ""};

  auto payload_data = payload.build();
  return publish_message(topic, payload_data);
}

std::expected<void, std::string>
Publisher::publish_device_command(std::string_view target_edge_node_id,
                                  std::string_view target_device_id, PayloadBuilder& payload) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!is_connected_) {
    return std::unexpected("Not connected");
  }

  Topic topic{.group_id = config_.group_id,
              .message_type = MessageType::DCMD,
              .edge_node_id = std::string(target_edge_node_id),
              .device_id = std::string(target_device_id)};

  auto payload_data = payload.build();
  return publish_message(topic, payload_data);
}

std::expected<void, std::string>
Publisher::publish_raw_message(std::string_view topic, std::span<const uint8_t> payload_data,
                               int qos, bool retain) {
  // Note: mutex already held by caller
  if (!client_ || !is_connected_) {
    return std::unexpected("Not connected");
  }

  MQTTAsync_message msg = MQTTAsync_message_initializer;
  // Note: const_cast required due to Paho MQTT C API (non-const payload pointer).
  // This is safe because MQTTAsync_sendMessage does not modify the payload.
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

std::expected<void, std::string> Publisher::publish_state_birth(std::string_view host_id,
                                                                uint64_t timestamp) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!is_connected_) {
    return std::unexpected("Not connected");
  }

  std::string json_payload = std::format("{{\"online\":true,\"timestamp\":{}}}", timestamp);

  std::string topic = std::format("STATE/{}", host_id);

  std::vector<uint8_t> payload_data(json_payload.begin(), json_payload.end());

  return publish_raw_message(topic, payload_data, 1, true);
}

std::expected<void, std::string> Publisher::publish_state_death(std::string_view host_id,
                                                                uint64_t timestamp) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!is_connected_) {
    return std::unexpected("Not connected");
  }

  std::string json_payload = std::format("{{\"online\":false,\"timestamp\":{}}}", timestamp);

  std::string topic = std::format("STATE/{}", host_id);

  std::vector<uint8_t> payload_data(json_payload.begin(), json_payload.end());

  return publish_raw_message(topic, payload_data, 1, true);
}

} // namespace sparkplug