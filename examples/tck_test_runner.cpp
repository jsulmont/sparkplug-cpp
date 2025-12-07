#include "tck_test_runner.hpp"

#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <thread>

namespace sparkplug::tck {

namespace detail {

auto split(const std::string& str, char delim) -> std::vector<std::string> {
  std::vector<std::string> tokens;
  std::stringstream ss(str);
  std::string token;
  while (std::getline(ss, token, delim)) {
    tokens.push_back(token);
  }
  return tokens;
}

auto trim(const std::string& str) -> std::string {
  size_t start = str.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return "";
  }
  size_t end = str.find_last_not_of(" \t\r\n");
  return str.substr(start, end - start + 1);
}

} // namespace detail

TCKTestRunner::TCKTestRunner(TCKConfig config, std::string profile)
    : config_(std::move(config)), tck_client_(nullptr), profile_(std::move(profile)) {
  std::string client_id = config_.client_id_prefix + "_control";
  int rc = MQTTAsync_create(&tck_client_, config_.broker_url.c_str(), client_id.c_str(),
                            MQTTCLIENT_PERSISTENCE_NONE, nullptr);
  if (rc != MQTTASYNC_SUCCESS) {
    throw std::runtime_error("Failed to create TCK MQTT client: " + std::to_string(rc));
  }

  MQTTAsync_setCallbacks(tck_client_, this, on_connection_lost, on_message_arrived,
                         on_delivery_complete);
}

TCKTestRunner::~TCKTestRunner() {
  stop();
  if (tck_client_) {
    MQTTAsync_destroy(&tck_client_);
  }
}

auto TCKTestRunner::start() -> stdx::expected<void, std::string> {
  std::scoped_lock lock(mutex_);

  if (running_) {
    return stdx::unexpected("TCK test runner is already running");
  }

  MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;
  conn_opts.keepAliveInterval = 60;
  conn_opts.cleansession = 1;
  conn_opts.onSuccess = on_connect_success;
  conn_opts.onFailure = on_connect_failure;
  conn_opts.context = this;

  if (!config_.username.empty()) {
    conn_opts.username = config_.username.c_str();
    conn_opts.password = config_.password.c_str();
  }

  int rc = MQTTAsync_connect(tck_client_, &conn_opts);
  if (rc != MQTTASYNC_SUCCESS) {
    return stdx::unexpected("Failed to start connect: " + std::to_string(rc));
  }

  for (int i = 0; i < 50 && !connected_; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  if (!connected_) {
    return stdx::unexpected("Connection timeout");
  }

  running_ = true;
  std::cout << "[TCK] TCK test runner ready (" << profile_ << " profile)\n";
  std::cout << "[TCK] Waiting for test commands from TCK Console...\n";

  return {};
}

void TCKTestRunner::stop() {
  std::scoped_lock lock(mutex_);

  if (!running_) {
    return;
  }

  if (connected_) {
    MQTTAsync_disconnectOptions disc_opts = MQTTAsync_disconnectOptions_initializer;
    disc_opts.timeout = 1000;
    MQTTAsync_disconnect(tck_client_, &disc_opts);
    connected_ = false;
  }

  running_ = false;
  std::cout << "[TCK] TCK test runner stopped\n";
}

auto TCKTestRunner::is_running() const -> bool {
  return running_;
}

void TCKTestRunner::on_connection_lost(void* context, char* cause) {
  auto* runner = static_cast<TCKTestRunner*>(context);
  std::cout << "[TCK] Connection lost: " << (cause ? cause : "unknown") << "\n";
  runner->connected_ = false;
}

int TCKTestRunner::on_message_arrived(void* context,
                                      char* topicName,
                                      int /*topicLen*/,
                                      MQTTAsync_message* message) {
  auto* runner = static_cast<TCKTestRunner*>(context);

  std::string topic = topicName;
  std::string payload(static_cast<char*>(message->payload), message->payloadlen);

  std::cout << "[TCK] Received: " << topic << " -> " << payload << "\n";

  std::thread([runner, topic, payload]() {
    if (topic == "SPARKPLUG_TCK/TEST_CONTROL") {
      runner->handle_test_control(payload);
    } else if (topic == "SPARKPLUG_TCK/CONSOLE_PROMPT") {
      runner->handle_console_prompt(payload);
    } else if (topic == "SPARKPLUG_TCK/CONFIG") {
      runner->handle_config(payload);
    } else if (topic == "SPARKPLUG_TCK/RESULT_CONFIG") {
      runner->handle_result_config(payload);
    }
  }).detach();

  MQTTAsync_freeMessage(&message);
  MQTTAsync_free(topicName);
  return 1;
}

void TCKTestRunner::on_delivery_complete(void* /*context*/, MQTTAsync_token /*token*/) {
}

void TCKTestRunner::on_connect_success(void* context,
                                       MQTTAsync_successData* /*response*/) {
  auto* runner = static_cast<TCKTestRunner*>(context);
  std::cout << "[TCK] Connected to broker\n";
  runner->connected_ = true;

  const char* topics[] = {"SPARKPLUG_TCK/TEST_CONTROL", "SPARKPLUG_TCK/CONSOLE_PROMPT",
                          "SPARKPLUG_TCK/CONFIG", "SPARKPLUG_TCK/RESULT_CONFIG"};
  int qos[] = {1, 1, 1, 1};

  MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
  opts.onSuccess = on_subscribe_success;
  opts.onFailure = on_subscribe_failure;
  opts.context = runner;

  int rc = MQTTAsync_subscribeMany(runner->tck_client_, 4, const_cast<char**>(topics),
                                   qos, &opts);
  if (rc != MQTTASYNC_SUCCESS) {
    std::cerr << "[TCK] Failed to subscribe: " << rc << "\n";
  }
}

void TCKTestRunner::on_connect_failure(void* /*context*/,
                                       MQTTAsync_failureData* response) {
  std::cerr << "[TCK] Connection failed: " << (response ? response->code : -1) << "\n";
}

void TCKTestRunner::on_subscribe_success(void* /*context*/,
                                         MQTTAsync_successData* /*response*/) {
  std::cout << "[TCK] Subscribed to TCK control topics\n";
}

void TCKTestRunner::on_subscribe_failure(void* /*context*/,
                                         MQTTAsync_failureData* response) {
  std::cerr << "[TCK] Subscribe failed: " << (response ? response->code : -1) << "\n";
}

void TCKTestRunner::handle_test_control(const std::string& message) {
  auto parts = detail::split(message, ' ');
  if (parts.empty()) {
    return;
  }

  std::string command = parts[0];

  if (command == "NEW_TEST" && parts.size() >= 3) {
    std::string message_profile = parts[1];
    std::string test_type = parts[2];

    if (message_profile != profile_) {
      return;
    }

    std::vector<std::string> params(parts.begin() + 3, parts.end());

    current_test_name_ = test_type;
    current_test_params_ = params;
    test_state_ = TestState::RUNNING;

    log("INFO", "Starting test: " + test_type);

    dispatch_test(test_type, params);

  } else if (command == "END_TEST") {
    log("INFO", "Test ended by TCK");

    handle_end_test();

    test_state_ = TestState::IDLE;
    current_test_name_.clear();
    current_test_params_.clear();
  }
}

void TCKTestRunner::handle_console_prompt(const std::string& message) {
  std::cout << "\n=== CONSOLE PROMPT ===\n";
  std::cout << message << "\n";
  std::cout << "======================\n";

  handle_prompt_specific(message);
}

void TCKTestRunner::handle_config(const std::string& message) {
  auto parts = detail::split(message, ' ');
  if (parts.size() >= 2 && parts[0] == "UTCwindow") {
    config_.utc_window_ms = std::stoi(parts[1]);
    log("INFO", "UTC window set to " + std::to_string(config_.utc_window_ms) + " ms");
  }
}

void TCKTestRunner::handle_result_config(const std::string& message) {
  auto parts = detail::split(message, ' ');
  if (parts.size() >= 2 && parts[0] == "NEW_RESULT-LOG") {
    log("INFO", "Result config: " + message);
  }
}

void TCKTestRunner::log(const std::string& level, const std::string& message) {
  std::string log_message = "[" + level + "] " + message;
  std::cout << "[TCK] " << log_message << "\n";
  (void)publish_tck("SPARKPLUG_TCK/LOG", log_message, 0);
}

void TCKTestRunner::publish_result(const std::string& result) {
  std::cout << "[TCK] Publishing result: " << result << "\n";
  (void)publish_tck("SPARKPLUG_TCK/RESULT", result, 1);
}

void TCKTestRunner::publish_console_reply(const std::string& reply) {
  std::cout << "[TCK] Console reply: " << reply << "\n";
  (void)publish_tck("SPARKPLUG_TCK/CONSOLE_REPLY", reply, 1);
}

auto TCKTestRunner::publish_tck(const std::string& topic,
                                const std::string& payload,
                                int qos) -> stdx::expected<void, std::string> {
  if (!connected_) {
    return stdx::unexpected("Not connected to broker");
  }

  MQTTAsync_message pubmsg = MQTTAsync_message_initializer;
  pubmsg.payload = const_cast<char*>(payload.c_str());
  pubmsg.payloadlen = static_cast<int>(payload.length());
  pubmsg.qos = qos;
  pubmsg.retained = 0;

  MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;

  int rc = MQTTAsync_sendMessage(tck_client_, topic.c_str(), &pubmsg, &opts);
  if (rc != MQTTASYNC_SUCCESS) {
    return stdx::unexpected("Failed to publish: " + std::to_string(rc));
  }

  return {};
}

auto TCKTestRunner::get_timestamp() -> uint64_t {
  auto now = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
  return static_cast<uint64_t>(ms.count());
}

} // namespace sparkplug::tck
