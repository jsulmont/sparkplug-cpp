#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <MQTTAsync.h>
#include <sparkplug/host_application.hpp>
#include <sparkplug/payload_builder.hpp>

namespace sparkplug::tck {

struct TCKConfig {
  std::string broker_url = "tcp://localhost:1883";
  std::string username;
  std::string password;
  std::string client_id_prefix = "tck_host_app";
  std::string host_id = "scada_host_id";
  std::string namespace_prefix = "spBv1.0";
  int utc_window_ms = 5000;
};

enum class TestState { IDLE, RUNNING, COMPLETED, FAILED };

class TCKHostApplication {
public:
  explicit TCKHostApplication(TCKConfig config);
  ~TCKHostApplication();

  // Start the TCK application (connect and subscribe to control topics)
  [[nodiscard]] auto start() -> stdx::expected<void, std::string>;

  // Stop the TCK application
  void stop();

  // Check if running
  [[nodiscard]] auto is_running() const -> bool;

private:
  // MQTT callbacks for TCK control client
  static void on_connection_lost(void* context, char* cause);
  static int on_message_arrived(void* context, char* topicName, int topicLen,
                                MQTTAsync_message* message);
  static void on_delivery_complete(void* context, MQTTAsync_token token);
  static void on_connect_success(void* context, MQTTAsync_successData* response);
  static void on_connect_failure(void* context, MQTTAsync_failureData* response);
  static void on_subscribe_success(void* context, MQTTAsync_successData* response);
  static void on_subscribe_failure(void* context, MQTTAsync_failureData* response);

  // Message handlers for TCK control topics
  void handle_test_control(const std::string& message);
  void handle_console_prompt(const std::string& message);
  void handle_config(const std::string& message);
  void handle_result_config(const std::string& message);

  // Test handlers (7 tests from the spec)
  void run_session_establishment_test(const std::vector<std::string>& params);
  void run_session_termination_test(const std::vector<std::string>& params);
  void run_send_command_test(const std::vector<std::string>& params);
  void run_receive_data_test(const std::vector<std::string>& params);
  void run_edge_session_termination_test(const std::vector<std::string>& params);
  void run_message_ordering_test(const std::vector<std::string>& params);
  void run_multiple_broker_test(const std::vector<std::string>& params);

  // Utility functions for TCK communication
  void log(const std::string& level, const std::string& message);
  void publish_result(const std::string& result);
  void publish_console_reply(const std::string& reply);

  // MQTT publish helper for TCK topics
  [[nodiscard]] auto publish_tck(const std::string& topic, const std::string& payload, int qos)
      -> stdx::expected<void, std::string>;

  // Helper to get current UTC timestamp in milliseconds
  [[nodiscard]] static auto get_timestamp() -> uint64_t;

  TCKConfig config_;
  MQTTAsync tck_client_; // TCK control MQTT client
  std::atomic<bool> running_{false};
  std::atomic<bool> connected_{false};
  std::mutex mutex_;

  // Test state
  TestState test_state_{TestState::IDLE};
  std::string current_test_name_;
  std::vector<std::string> current_test_params_;

  // Sparkplug Host Application instance (created per test)
  std::unique_ptr<HostApplication> host_application_;
  std::string current_host_id_;
};

} // namespace sparkplug::tck
