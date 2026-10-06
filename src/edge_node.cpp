// src/edge_node.cpp
#include "sparkplug/edge_node.hpp"

#include <cstring>
#include <format>
#include <future>
#include <utility>

#include <MQTTAsync.h>

namespace sparkplug {

namespace {
constexpr int CONNECTION_TIMEOUT_MS = 5000;
constexpr int DISCONNECT_TIMEOUT_MS = 11000;
constexpr int SUBSCRIBE_TIMEOUT_MS = 5000;
constexpr uint64_t SEQ_NUMBER_MAX = 256;

// Minimal JSON scanner for the STATE payload. Structure-only: string contents
// are not unescaped, but malformed input is rejected.
struct JsonCursor {
  std::string_view s;
  size_t pos = 0;

  void skip_ws() {
    while (pos < s.size()) {
      const char c = s[pos];
      if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
        break;
      }
      ++pos;
    }
  }

  bool consume(char c) {
    skip_ws();
    if (pos < s.size() && s[pos] == c) {
      ++pos;
      return true;
    }
    return false;
  }
};

bool skip_json_string(JsonCursor& cur, std::string_view* out = nullptr) {
  if (!cur.consume('"')) {
    return false;
  }
  const size_t start = cur.pos;
  while (cur.pos < cur.s.size()) {
    const char c = cur.s[cur.pos];
    if (c == '"') {
      if (out) {
        *out = cur.s.substr(start, cur.pos - start);
      }
      ++cur.pos;
      return true;
    }
    cur.pos += (c == '\\') ? 2 : 1; // skip escaped char without interpreting
  }
  return false;
}

bool skip_json_value(JsonCursor& cur) {
  cur.skip_ws();
  if (cur.pos >= cur.s.size()) {
    return false;
  }
  const char c = cur.s[cur.pos];
  if (c == '"') {
    return skip_json_string(cur);
  }
  if (c == '{' || c == '[') {
    const char open = c;
    const char close = (c == '{') ? '}' : ']';
    int depth = 0;
    while (cur.pos < cur.s.size()) {
      const char d = cur.s[cur.pos];
      if (d == '"') {
        if (!skip_json_string(cur)) {
          return false;
        }
        continue;
      }
      ++cur.pos;
      if (d == open) {
        ++depth;
      } else if (d == close && --depth == 0) {
        return true;
      }
    }
    return false;
  }
  // number / true / false / null token
  const size_t start = cur.pos;
  while (cur.pos < cur.s.size()) {
    const char d = cur.s[cur.pos];
    const bool in_token = (d >= 'a' && d <= 'z') || (d >= '0' && d <= '9') ||
                          d == '-' || d == '+' || d == '.' || d == 'e' || d == 'E';
    if (!in_token) {
      break;
    }
    ++cur.pos;
  }
  return cur.pos > start;
}

// Strict parser for the Sparkplug STATE payload: a well-formed JSON object
// whose "online" member is a JSON boolean. Replaces the substring scan that
// accepted non-JSON garbage and truncated literals (CWE-20).
std::optional<bool> parse_state_online(std::string_view json) {
  JsonCursor cur{json};
  if (!cur.consume('{')) {
    return std::nullopt;
  }

  std::optional<bool> online;
  if (!cur.consume('}')) {
    while (true) {
      std::string_view key;
      if (!skip_json_string(cur, &key) || !cur.consume(':')) {
        return std::nullopt;
      }
      if (key == "online") {
        cur.skip_ws();
        const auto match = [&](std::string_view token) {
          return cur.pos + token.size() <= cur.s.size() &&
                 cur.s.substr(cur.pos, token.size()) == token;
        };
        if (match("true")) {
          online = true;
          cur.pos += 4;
        } else if (match("false")) {
          online = false;
          cur.pos += 5;
        } else {
          return std::nullopt; // "online" must be a JSON boolean
        }
      } else if (!skip_json_value(cur)) {
        return std::nullopt;
      }
      if (!cur.consume(',')) {
        break;
      }
    }
    if (!cur.consume('}')) {
      return std::nullopt;
    }
  }

  if (!online.has_value()) {
    return std::nullopt;
  }
  cur.skip_ws();
  return (cur.pos == json.size()) ? online : std::nullopt;
}

// Heap-allocated so a late Paho response (broker-controlled timing) after a
// wait_for() timeout cannot touch a destroyed stack promise (CWE-416). The
// completing callback owns and deletes it; Paho fires exactly one of
// onSuccess/onFailure per command.
struct AsyncOp {
  std::promise<void> promise;
};

void complete_async_op(void* context) noexcept {
  auto* op = static_cast<AsyncOp*>(context);
  try {
    op->promise.set_value();
  } catch (...) {
  }
  delete op;
}

void fail_async_op(void* context, std::string_view what, int code) noexcept {
  auto* op = static_cast<AsyncOp*>(context);
  try {
    op->promise.set_exception(std::make_exception_ptr(
        std::runtime_error(std::format("{} failed: code={}", what, code))));
  } catch (...) {
  }
  delete op;
}

void on_connect_success(void* context, MQTTAsync_successData* response) {
  (void)response;
  complete_async_op(context);
}

void on_connect_failure(void* context, MQTTAsync_failureData* response) {
  fail_async_op(context, "Connection", response ? response->code : -1);
}

void on_disconnect_success(void* context, MQTTAsync_successData* response) {
  (void)response;
  complete_async_op(context);
}

void on_disconnect_failure(void* context, MQTTAsync_failureData* response) {
  fail_async_op(context, "Disconnect", response ? response->code : -1);
}

void on_subscribe_success(void* context, MQTTAsync_successData* response) {
  (void)response;
  complete_async_op(context);
}

void on_subscribe_failure(void* context, MQTTAsync_failureData* response) {
  fail_async_op(context, "Subscribe", response ? response->code : -1);
}

} // namespace

void EdgeNode::on_connection_lost(void* context, char* cause) {
  auto* edge_node = static_cast<EdgeNode*>(context);
  if (!edge_node) {
    return;
  }

  edge_node->is_connected_.store(false, std::memory_order_relaxed);
  (void)cause;
}

MQTTAsyncHandle::~MQTTAsyncHandle() noexcept {
  reset();
}

void MQTTAsyncHandle::reset() noexcept {
  if (client_) {
    MQTTAsync_destroy(&client_);
    client_ = nullptr;
  }
}

EdgeNode::EdgeNode(Config config) : config_(std::move(config)) {
  will_opts_ = MQTTAsync_willOptions_initializer;
  adopt_secrets(); // construction: no other thread can race us
}

int EdgeNode::on_message_arrived(void* context,
                                 char* topicName,
                                 int topicLen,
                                 MQTTAsync_message* message) {
  auto* edge_node = static_cast<EdgeNode*>(context);
  if (!edge_node || !topicName || !message) {
    if (message) {
      MQTTAsync_freeMessage(&message);
      MQTTAsync_free(topicName);
    }
    return 1;
  }

  std::string topic_str;
  if (topicLen > 0) {
    topic_str = std::string(topicName, static_cast<size_t>(topicLen));
  } else {
    topic_str = std::string(topicName);
  }

  // Copy config under the lock: this read must not race concurrent set_*()
  // calls, and user callbacks must not run while mutex_ is held (CWE-362).
  std::optional<CommandCallback> command_callback;
  size_t max_payload_bytes = 0;
  {
    std::scoped_lock lock(edge_node->mutex_);
    command_callback = edge_node->config_.command_callback;
    max_payload_bytes = edge_node->config_.max_payload_bytes;
  }

  // Drop empty or oversized payloads before any allocation (CWE-400).
  if (message->payloadlen <= 0 ||
      static_cast<size_t>(message->payloadlen) > max_payload_bytes) {
    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(topicName);
    return 1;
  }

  if (topic_str.starts_with("spBv1.0/STATE/")) {
    std::string payload_str(static_cast<const char*>(message->payload),
                            static_cast<size_t>(message->payloadlen));

    if (auto online = parse_state_online(payload_str)) {
      edge_node->primary_host_online_.store(*online, std::memory_order_relaxed);
    }

    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(topicName);
    return 1;
  }

  auto topic_result = Topic::parse(topic_str);
  if (!topic_result) {
    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(topicName);
    return 1;
  }

  const auto& topic = topic_result.value();

  if ((topic.message_type == MessageType::NCMD ||
       topic.message_type == MessageType::DCMD) &&
      command_callback) {
    org::eclipse::tahu::protobuf::Payload payload;
    if (payload.ParseFromArray(message->payload, message->payloadlen)) {
      // Exception barrier: a throw from user code must not unwind through
      // Paho's C stack frames (CWE-248).
      try {
        command_callback.value()(topic, payload);
      } catch (...) {
      }
    }
  }

  MQTTAsync_freeMessage(&message);
  MQTTAsync_free(topicName);
  return 1;
}

EdgeNode::~EdgeNode() {
  if (client_) {
    // Clear callbacks first to prevent callbacks during destruction
    MQTTAsync_setCallbacks(client_.get(), nullptr, nullptr, nullptr, nullptr);
    // Always attempt disconnect — is_connected_ may be stale if on_connection_lost
    // raced with disconnect(). MQTTAsync_disconnect handles already-disconnected
    // clients gracefully.
    MQTTAsync_disconnectOptions opts = MQTTAsync_disconnectOptions_initializer;
    opts.timeout = 1000;
    (void)MQTTAsync_disconnect(client_.get(), &opts);
  }
}

EdgeNode::EdgeNode(EdgeNode&& other) noexcept {
  std::scoped_lock lock(other.mutex_);
  config_ = std::move(other.config_);
  client_ = std::move(other.client_);
  seq_num_ = other.seq_num_;
  bd_seq_num_ = other.bd_seq_num_;
  death_payload_data_ = std::move(other.death_payload_data_);
  last_birth_payload_ = std::move(other.last_birth_payload_);
  device_states_ = std::move(other.device_states_);
  password_ = std::move(other.password_);
  tls_key_password_ = std::move(other.tls_key_password_);
  is_connected_.store(other.is_connected_.load(std::memory_order_relaxed),
                      std::memory_order_relaxed);
  primary_host_online_.store(other.primary_host_online_.load(std::memory_order_relaxed),
                             std::memory_order_relaxed);
  other.is_connected_.store(false, std::memory_order_relaxed);
  other.primary_host_online_.store(false, std::memory_order_relaxed);

  // Rebind Paho callbacks: the client handle still carries the moved-from
  // object as its context (CWE-416).
  if (client_) {
    MQTTAsync_setCallbacks(client_.get(), this, on_connection_lost,
                           on_message_arrived, nullptr);
  }
}

EdgeNode& EdgeNode::operator=(EdgeNode&& other) noexcept {
  if (this != &other) {
    // Lock both mutexes with automatic deadlock avoidance
    std::scoped_lock lock(mutex_, other.mutex_);

    config_ = std::move(other.config_);
    client_ = std::move(other.client_);
    seq_num_ = other.seq_num_;
    bd_seq_num_ = other.bd_seq_num_;
    death_payload_data_ = std::move(other.death_payload_data_);
    last_birth_payload_ = std::move(other.last_birth_payload_);
    device_states_ = std::move(other.device_states_);
    password_ = std::move(other.password_);
    tls_key_password_ = std::move(other.tls_key_password_);
    is_connected_.store(other.is_connected_.load(std::memory_order_relaxed),
                        std::memory_order_relaxed);
    primary_host_online_.store(other.primary_host_online_.load(std::memory_order_relaxed),
                               std::memory_order_relaxed);
    other.is_connected_.store(false, std::memory_order_relaxed);
    other.primary_host_online_.store(false, std::memory_order_relaxed);

    // Rebind Paho callbacks: the client handle still carries the moved-from
    // object as its context (CWE-416).
    if (client_) {
      MQTTAsync_setCallbacks(client_.get(), this, on_connection_lost,
                             on_message_arrived, nullptr);
    }
  }
  return *this;
}

void EdgeNode::set_credentials(std::optional<std::string> username,
                               std::optional<std::string> password) {
  std::scoped_lock lock(mutex_);
  config_.username = std::move(username);
  config_.password = std::move(password);
  adopt_secrets();
}

void EdgeNode::set_tls(std::optional<TlsOptions> tls) {
  std::scoped_lock lock(mutex_);
  config_.tls = std::move(tls);
  adopt_secrets();
}

void EdgeNode::adopt_secrets() {
  if (config_.password.has_value()) {
    password_.assign(*config_.password);
    detail::SecureBuffer::scrub_string(*config_.password);
    config_.password.reset();
  }
  if (config_.tls.has_value()) {
    auto& key_password = config_.tls->private_key_password;
    if (!key_password.empty()) {
      tls_key_password_.assign(key_password);
      detail::SecureBuffer::scrub_string(key_password);
    } else {
      tls_key_password_.clear();
    }
  }
}

void EdgeNode::set_log_callback(std::optional<LogCallback> callback) {
  std::scoped_lock lock(mutex_);
  config_.log_callback = std::move(callback);
}

stdx::expected<void, std::string> EdgeNode::connect() {
  // Phase 1: Prepare client and initiate async connect under lock.
  // Lock is released before any blocking waits to prevent deadlock
  // with on_connection_lost callback.
  auto* connect_op = new AsyncOp{}; // heap op: survives late callbacks after timeout
  auto connect_future = connect_op->promise.get_future();
  bool plaintext_creds = false;
  MQTTAsync client_handle = nullptr;
  std::string group_id;
  std::string edge_node_id;
  std::optional<std::string> primary_host_id;

  {
    std::scoped_lock lock(mutex_);

    // Reuse the client handle across reconnects: recreating it invalidates
    // raw handles captured by in-flight publish calls (CWE-367).
    int rc = MQTTASYNC_SUCCESS;
    if (!client_) {
      MQTTAsync raw_client = nullptr;
      rc = MQTTAsync_create(&raw_client, config_.broker_url.c_str(),
                            config_.client_id.c_str(), MQTTCLIENT_PERSISTENCE_NONE,
                            nullptr);
      if (rc != MQTTASYNC_SUCCESS) {
        return stdx::unexpected(std::format("Failed to create client: {}", rc));
      }
      client_ = MQTTAsyncHandle(raw_client);
    }

    rc = MQTTAsync_setCallbacks(client_.get(), this, on_connection_lost,
                                on_message_arrived, nullptr);
    if (rc != MQTTASYNC_SUCCESS) {
      return stdx::unexpected(std::format("Failed to set callbacks: {}", rc));
    }

    // Increment bdSeq for this session
    bd_seq_num_++;

    PayloadBuilder death_payload;
    death_payload.add_metric("bdSeq", bd_seq_num_);
    death_payload_data_ = death_payload.build();

    MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;
    conn_opts.keepAliveInterval = config_.keep_alive_interval;
    conn_opts.cleansession = config_.clean_session;

    if (config_.username.has_value()) {
      conn_opts.username = config_.username.value().c_str();
    }
    // Password comes from the scrubbed buffer; Paho retains its own copy for
    // the connection (CWE-316).
    conn_opts.password = password_.c_str();

    // Credentials on a plaintext transport are sniffable (CWE-319).
    plaintext_creds = config_.username.has_value() &&
                      !config_.broker_url.starts_with("ssl://") &&
                      !config_.broker_url.starts_with("wss://");

    ssl_opts_ = MQTTAsync_SSLOptions_initializer;
    if (config_.tls.has_value()) {
      const auto& tls = config_.tls.value();
      ssl_opts_.trustStore = tls.trust_store.c_str();
      ssl_opts_.keyStore = tls.key_store.empty() ? nullptr : tls.key_store.c_str();
      ssl_opts_.privateKey = tls.private_key.empty() ? nullptr : tls.private_key.c_str();
      ssl_opts_.privateKeyPassword = tls_key_password_.c_str();
      ssl_opts_.enabledCipherSuites =
          tls.enabled_cipher_suites.empty() ? nullptr : tls.enabled_cipher_suites.c_str();
      ssl_opts_.enableServerCertAuth = tls.enable_server_cert_auth;
      // Paho defaults `verify` to 0 (chain check only); enabling it ties the
      // certificate identity to broker_url's host (CWE-297).
      ssl_opts_.verify = tls.verify_hostname ? 1 : 0;
      conn_opts.ssl = &ssl_opts_;
    }

    will_opts_ = MQTTAsync_willOptions_initializer;

    Topic death_topic{.group_id = config_.group_id,
                      .message_type = MessageType::NDEATH,
                      .edge_node_id = config_.edge_node_id,
                      .device_id = ""};

    death_topic_str_ = death_topic.to_string();
    will_opts_.topicName = death_topic_str_.c_str();
    will_opts_.payload.data = death_payload_data_.data();
    will_opts_.payload.len = static_cast<int>(death_payload_data_.size());
    will_opts_.retained = 0;
    will_opts_.qos = config_.death_qos;

    conn_opts.will = &will_opts_;
    conn_opts.context = connect_op;
    conn_opts.onSuccess = on_connect_success;
    conn_opts.onFailure = on_connect_failure;

    rc = MQTTAsync_connect(client_.get(), &conn_opts);
    if (rc != MQTTASYNC_SUCCESS) {
      delete connect_op; // callbacks never fire on synchronous failure
      return stdx::unexpected(std::format("Failed to connect: {}", rc));
    }

    // Extract values needed outside the lock
    client_handle = client_.get();
    group_id = config_.group_id;
    edge_node_id = config_.edge_node_id;
    primary_host_id = config_.primary_host_id;
  }
  // Paho copies conn_opts internally; local opts can safely go out of scope.
  // Member variables (will_opts_, ssl_opts_, death_payload_data_, death_topic_str_)
  // remain valid for the async operation.
  if (plaintext_creds) {
    log(LogLevel::WARN,
        "MQTT username/password configured for a non-TLS broker URL; "
        "credentials will be sent in cleartext");
  }

  // Phase 2: Wait for connect completion (no lock held)
  auto status = connect_future.wait_for(std::chrono::milliseconds(CONNECTION_TIMEOUT_MS));
  if (status == std::future_status::timeout) {
    // Best-effort teardown; connect_op stays alive for the late callback.
    MQTTAsync_disconnectOptions disc_opts = MQTTAsync_disconnectOptions_initializer;
    disc_opts.timeout = 1000;
    (void)MQTTAsync_disconnect(client_handle, &disc_opts);
    return stdx::unexpected("Connection timeout");
  }

  try {
    connect_future.get();
  } catch (const std::exception& e) {
    return stdx::unexpected(e.what());
  }

  // Phase 3: Update connected state atomically.
  // on_connection_lost() may have fired between Phase 2 and now.
  if (!MQTTAsync_isConnected(client_handle)) {
    return stdx::unexpected("Connection lost during setup");
  }
  is_connected_.store(true, std::memory_order_relaxed);
  if (!primary_host_id.has_value()) {
    primary_host_online_.store(true, std::memory_order_relaxed);
  }

  // Phase 4: Subscribe to NCMD (no lock held)
  Topic ncmd_topic{.group_id = group_id,
                   .message_type = MessageType::NCMD,
                   .edge_node_id = edge_node_id,
                   .device_id = ""};

  auto ncmd_topic_str = ncmd_topic.to_string();

  auto* subscribe_op = new AsyncOp{};
  auto subscribe_future = subscribe_op->promise.get_future();

  MQTTAsync_responseOptions sub_opts = MQTTAsync_responseOptions_initializer;
  sub_opts.context = subscribe_op;
  sub_opts.onSuccess = on_subscribe_success;
  sub_opts.onFailure = on_subscribe_failure;

  int rc = MQTTAsync_subscribe(client_handle, ncmd_topic_str.c_str(), 1, &sub_opts);
  if (rc != MQTTASYNC_SUCCESS) {
    delete subscribe_op;
    return stdx::unexpected(std::format("Failed to subscribe to NCMD: {}", rc));
  }

  auto sub_status =
      subscribe_future.wait_for(std::chrono::milliseconds(SUBSCRIBE_TIMEOUT_MS));
  if (sub_status == std::future_status::timeout) {
    return stdx::unexpected("NCMD subscription timeout");
  }

  try {
    subscribe_future.get();
  } catch (const std::exception& e) {
    return stdx::unexpected(std::format("NCMD subscription failed: {}", e.what()));
  }

  // Phase 5: Subscribe to STATE if primary host configured (no lock held)
  if (primary_host_id.has_value()) {
    std::string state_topic = "spBv1.0/STATE/" + primary_host_id.value();

    auto* state_subscribe_op = new AsyncOp{};
    auto state_subscribe_future = state_subscribe_op->promise.get_future();

    MQTTAsync_responseOptions state_sub_opts = MQTTAsync_responseOptions_initializer;
    state_sub_opts.context = state_subscribe_op;
    state_sub_opts.onSuccess = on_subscribe_success;
    state_sub_opts.onFailure = on_subscribe_failure;

    rc = MQTTAsync_subscribe(client_handle, state_topic.c_str(), 1, &state_sub_opts);
    if (rc != MQTTASYNC_SUCCESS) {
      delete state_subscribe_op;
      return stdx::unexpected(std::format("Failed to subscribe to STATE: {}", rc));
    }

    auto state_sub_status =
        state_subscribe_future.wait_for(std::chrono::milliseconds(SUBSCRIBE_TIMEOUT_MS));
    if (state_sub_status == std::future_status::timeout) {
      return stdx::unexpected("STATE subscription timeout");
    }

    try {
      state_subscribe_future.get();
    } catch (const std::exception& e) {
      return stdx::unexpected(std::format("STATE subscription failed: {}", e.what()));
    }
  }

  return {};
}

stdx::expected<void, std::string> EdgeNode::disconnect() {
  // Phase 1: Initiate disconnect under lock
  MQTTAsync client_handle = nullptr;
  {
    std::scoped_lock lock(mutex_);
    if (!client_) {
      return stdx::unexpected("Not connected");
    }
    client_handle = client_.get();
  }

  // Phase 2: Wait for disconnect completion (no lock held)
  // Lock is released to prevent deadlock with on_connection_lost callback.
  auto* disconnect_op = new AsyncOp{};
  auto disconnect_future = disconnect_op->promise.get_future();

  MQTTAsync_disconnectOptions opts = MQTTAsync_disconnectOptions_initializer;
  opts.timeout = DISCONNECT_TIMEOUT_MS;
  opts.context = disconnect_op;
  opts.onSuccess = on_disconnect_success;
  opts.onFailure = on_disconnect_failure;

  int rc = MQTTAsync_disconnect(client_handle, &opts);
  if (rc != MQTTASYNC_SUCCESS) {
    delete disconnect_op;
    return stdx::unexpected(std::format("Failed to disconnect: {}", rc));
  }

  auto status =
      disconnect_future.wait_for(std::chrono::milliseconds(DISCONNECT_TIMEOUT_MS));
  if (status != std::future_status::timeout) {
    try {
      disconnect_future.get();
    } catch (const std::exception&) {
    }
  }

  is_connected_.store(false, std::memory_order_relaxed);
  return {};
}

stdx::expected<void, std::string>
EdgeNode::publish_message(MQTTAsync client,
                          const std::string& topic_str,
                          std::span<const uint8_t> payload_data,
                          int qos,
                          bool retain) {
  if (!client) {
    return stdx::unexpected("Not connected");
  }

  MQTTAsync_message msg = MQTTAsync_message_initializer;
  msg.payload = const_cast<void*>(reinterpret_cast<const void*>(payload_data.data()));
  msg.payloadlen = static_cast<int>(payload_data.size());
  msg.qos = qos;
  msg.retained = retain ? 1 : 0;

  MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;

  int rc = MQTTAsync_sendMessage(client, topic_str.c_str(), &msg, &opts);
  if (rc != MQTTASYNC_SUCCESS) {
    return stdx::unexpected(std::format("Failed to publish: {}", rc));
  }

  return {};
}

stdx::expected<void, std::string> EdgeNode::publish_birth(PayloadBuilder& payload) {
  MQTTAsync client = nullptr;
  std::string topic_str;
  std::vector<uint8_t> payload_data;
  int qos = 0;

  {
    std::scoped_lock lock(mutex_);

    if (!is_connected_) {
      return stdx::unexpected("Not connected");
    }

    if (!primary_host_online_) {
      return stdx::unexpected("Primary host is not online");
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
      if (proto_payload.has_timestamp()) {
        metric->set_timestamp(proto_payload.timestamp());
      }
    }

    Topic topic{.group_id = config_.group_id,
                .message_type = MessageType::NBIRTH,
                .edge_node_id = config_.edge_node_id,
                .device_id = ""};

    topic_str = topic.to_string();
    payload_data = payload.build();
    client = client_.get();
    qos = config_.data_qos;
  }

  auto result = publish_message(client, topic_str, payload_data, qos, false);
  if (!result) {
    return result;
  }

  {
    std::scoped_lock lock(mutex_);
    last_birth_payload_ = std::move(payload_data);
    seq_num_ = 0;
  }

  return {};
}

stdx::expected<void, std::string> EdgeNode::publish_data(PayloadBuilder& payload) {
  MQTTAsync client = nullptr;
  std::string topic_str;
  std::vector<uint8_t> payload_data;
  int qos = 0;

  {
    std::scoped_lock lock(mutex_);

    if (!is_connected_) {
      return stdx::unexpected("Not connected");
    }

    seq_num_ = (seq_num_ + 1) % SEQ_NUMBER_MAX;

    if (!payload.has_seq()) {
      payload.set_seq(seq_num_);
    }

    Topic topic{.group_id = config_.group_id,
                .message_type = MessageType::NDATA,
                .edge_node_id = config_.edge_node_id,
                .device_id = ""};

    topic_str = topic.to_string();
    payload_data = payload.build();
    client = client_.get();
    qos = config_.data_qos;
  }

  return publish_message(client, topic_str, payload_data, qos, false);
}

stdx::expected<void, std::string> EdgeNode::publish_death() {
  MQTTAsync client = nullptr;
  std::string topic_str;
  std::vector<uint8_t> payload_data;
  int qos = 0;

  {
    std::scoped_lock lock(mutex_);

    if (!is_connected_) {
      return stdx::unexpected("Not connected");
    }

    seq_num_ = (seq_num_ + 1) % SEQ_NUMBER_MAX;

    PayloadBuilder death_payload;
    death_payload.add_metric("bdSeq", bd_seq_num_);
    death_payload.set_seq(seq_num_);
    death_payload.set_timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count());

    Topic topic{.group_id = config_.group_id,
                .message_type = MessageType::NDEATH,
                .edge_node_id = config_.edge_node_id,
                .device_id = ""};

    topic_str = topic.to_string();
    payload_data = death_payload.build();
    client = client_.get();
    qos = config_.death_qos;
  }

  auto result = publish_message(client, topic_str, payload_data, qos, false);
  if (!result) {
    return result;
  }

  return disconnect();
}

stdx::expected<void, std::string> EdgeNode::rebirth() {
  std::vector<uint8_t> payload_data;
  std::string topic_str;
  int qos = 0;

  {
    std::scoped_lock lock(mutex_);

    if (!is_connected_) {
      return stdx::unexpected("Not connected");
    }

    if (last_birth_payload_.empty()) {
      return stdx::unexpected("No previous birth payload stored");
    }

    org::eclipse::tahu::protobuf::Payload proto_payload;
    if (!proto_payload.ParseFromArray(last_birth_payload_.data(),
                                      static_cast<int>(last_birth_payload_.size()))) {
      return stdx::unexpected("Failed to parse stored birth payload");
    }

    uint64_t new_bdseq = bd_seq_num_ + 1;

    for (auto& metric : *proto_payload.mutable_metrics()) {
      if (metric.name() == "bdSeq") {
        metric.set_long_value(new_bdseq);
        break;
      }
    }

    proto_payload.set_seq(0);

    payload_data.resize(proto_payload.ByteSizeLong());
    (void)proto_payload.SerializeToArray(payload_data.data(),
                                         static_cast<int>(payload_data.size()));
    last_birth_payload_ = payload_data;

    Topic topic{.group_id = config_.group_id,
                .message_type = MessageType::NBIRTH,
                .edge_node_id = config_.edge_node_id,
                .device_id = ""};

    topic_str = topic.to_string();
    qos = config_.data_qos;
  }

  auto result = disconnect()
                    .and_then([this]() { return connect(); })
                    .and_then([this, &topic_str, &payload_data, qos]() {
                      MQTTAsync client = nullptr;
                      {
                        std::scoped_lock lock(mutex_);
                        client = client_.get();
                      }
                      return publish_message(client, topic_str, payload_data, qos, false);
                    });

  if (!result) {
    return result;
  }

  {
    std::scoped_lock lock(mutex_);
    seq_num_ = 0;
  }

  return {};
}

stdx::expected<void, std::string>
EdgeNode::publish_device_birth(std::string_view device_id, PayloadBuilder& payload) {
  MQTTAsync client = nullptr;
  std::string topic_str;
  std::vector<uint8_t> payload_data;
  int qos = 0;

  {
    std::scoped_lock lock(mutex_);

    if (!is_connected_) {
      return stdx::unexpected("Not connected");
    }

    if (!primary_host_online_) {
      return stdx::unexpected("Primary host is not online");
    }

    if (last_birth_payload_.empty()) {
      return stdx::unexpected("Must publish NBIRTH before DBIRTH");
    }

    seq_num_ = (seq_num_ + 1) % SEQ_NUMBER_MAX;
    payload.set_seq(seq_num_);

    Topic topic{.group_id = config_.group_id,
                .message_type = MessageType::DBIRTH,
                .edge_node_id = config_.edge_node_id,
                .device_id = std::string(device_id)};

    topic_str = topic.to_string();
    payload_data = payload.build();
    client = client_.get();
    qos = config_.data_qos;
  }

  // Subscribe to DCMD for this device BEFORE publishing DBIRTH (required by Sparkplug
  // spec)
  Topic dcmd_topic{.group_id = config_.group_id,
                   .message_type = MessageType::DCMD,
                   .edge_node_id = config_.edge_node_id,
                   .device_id = std::string(device_id)};

  auto dcmd_topic_str = dcmd_topic.to_string();

  auto* subscribe_op = new AsyncOp{};
  auto subscribe_future = subscribe_op->promise.get_future();

  MQTTAsync_responseOptions sub_opts = MQTTAsync_responseOptions_initializer;
  sub_opts.context = subscribe_op;
  sub_opts.onSuccess = on_subscribe_success;
  sub_opts.onFailure = on_subscribe_failure;

  int rc = MQTTAsync_subscribe(client, dcmd_topic_str.c_str(), 1, &sub_opts);
  if (rc != MQTTASYNC_SUCCESS) {
    delete subscribe_op;
    return stdx::unexpected(std::format("Failed to subscribe to DCMD: {}", rc));
  }

  auto sub_status =
      subscribe_future.wait_for(std::chrono::milliseconds(SUBSCRIBE_TIMEOUT_MS));
  if (sub_status == std::future_status::timeout) {
    return stdx::unexpected("DCMD subscription timeout");
  }

  try {
    subscribe_future.get();
  } catch (const std::exception& e) {
    return stdx::unexpected(std::format("DCMD subscription failed: {}", e.what()));
  }

  auto result = publish_message(client, topic_str, payload_data, qos, false);
  if (!result) {
    return result;
  }

  {
    std::scoped_lock lock(mutex_);
    auto& device_state = device_states_[std::string(device_id)];
    device_state.last_birth_payload = std::move(payload_data);
    device_state.is_online = true;
  }

  return {};
}

stdx::expected<void, std::string>
EdgeNode::publish_device_data(std::string_view device_id, PayloadBuilder& payload) {
  MQTTAsync client = nullptr;
  std::string topic_str;
  std::vector<uint8_t> payload_data;
  int qos = 0;

  {
    std::scoped_lock lock(mutex_);

    if (!is_connected_) {
      return stdx::unexpected("Not connected");
    }

    auto it = device_states_.find(device_id);
    if (it == device_states_.end() || !it->second.is_online) {
      return stdx::unexpected(
          std::format("Must publish DBIRTH for device '{}' before DDATA", device_id));
    }

    seq_num_ = (seq_num_ + 1) % SEQ_NUMBER_MAX;

    if (!payload.has_seq()) {
      payload.set_seq(seq_num_);
    }

    Topic topic{.group_id = config_.group_id,
                .message_type = MessageType::DDATA,
                .edge_node_id = config_.edge_node_id,
                .device_id = std::string(device_id)};

    topic_str = topic.to_string();
    payload_data = payload.build();
    client = client_.get();
    qos = config_.data_qos;
  }

  return publish_message(client, topic_str, payload_data, qos, false);
}

stdx::expected<void, std::string>
EdgeNode::publish_device_death(std::string_view device_id) {
  MQTTAsync client = nullptr;
  std::string topic_str;
  std::vector<uint8_t> payload_data;
  int qos = 0;

  {
    std::scoped_lock lock(mutex_);

    if (!is_connected_) {
      return stdx::unexpected("Not connected");
    }

    auto it = device_states_.find(device_id);
    if (it == device_states_.end()) {
      return stdx::unexpected(std::format("Unknown device: '{}'", device_id));
    }

    seq_num_ = (seq_num_ + 1) % SEQ_NUMBER_MAX;

    PayloadBuilder death_payload;
    death_payload.set_seq(seq_num_);
    death_payload.set_timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count());

    Topic topic{.group_id = config_.group_id,
                .message_type = MessageType::DDEATH,
                .edge_node_id = config_.edge_node_id,
                .device_id = std::string(device_id)};

    topic_str = topic.to_string();
    payload_data = death_payload.build();
    client = client_.get();
    qos = config_.data_qos;
  }

  auto result = publish_message(client, topic_str, payload_data, qos, false);
  if (!result) {
    return result;
  }

  {
    std::scoped_lock lock(mutex_);
    auto it = device_states_.find(device_id);
    if (it != device_states_.end()) {
      it->second.is_online = false;
    }
  }

  return {};
}

stdx::expected<void, std::string>
EdgeNode::publish_node_command(std::string_view target_edge_node_id,
                               PayloadBuilder& payload) {
  MQTTAsync client = nullptr;
  std::string topic_str;
  std::vector<uint8_t> payload_data;
  int qos = 0;

  {
    std::scoped_lock lock(mutex_);

    if (!is_connected_) {
      return stdx::unexpected("Not connected");
    }

    Topic topic{.group_id = config_.group_id,
                .message_type = MessageType::NCMD,
                .edge_node_id = std::string(target_edge_node_id),
                .device_id = ""};

    topic_str = topic.to_string();
    payload_data = payload.build();
    client = client_.get();
    qos = config_.data_qos;
  }

  return publish_message(client, topic_str, payload_data, qos, false);
}

stdx::expected<void, std::string>
EdgeNode::publish_device_command(std::string_view target_edge_node_id,
                                 std::string_view target_device_id,
                                 PayloadBuilder& payload) {
  MQTTAsync client = nullptr;
  std::string topic_str;
  std::vector<uint8_t> payload_data;
  int qos = 0;

  {
    std::scoped_lock lock(mutex_);

    if (!is_connected_) {
      return stdx::unexpected("Not connected");
    }

    Topic topic{.group_id = config_.group_id,
                .message_type = MessageType::DCMD,
                .edge_node_id = std::string(target_edge_node_id),
                .device_id = std::string(target_device_id)};

    topic_str = topic.to_string();
    payload_data = payload.build();
    client = client_.get();
    qos = config_.data_qos;
  }

  return publish_message(client, topic_str, payload_data, qos, false);
}

void EdgeNode::log(LogLevel level, std::string_view message) const noexcept {
  std::optional<LogCallback> cb;
  {
    std::scoped_lock lock(mutex_);
    cb = config_.log_callback;
  }
  if (cb) {
    try {
      // Sanitized: attacker-controlled topics/IDs must not inject log forgeries
      // or terminal escapes; barrier: a throwing callback is noexcept-fatal.
      cb.value()(level, sanitize_log_message(message));
    } catch (...) {
    }
  }
}

} // namespace sparkplug
