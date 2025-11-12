#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <MQTTAsync.h>
#include <sparkplug/detail/compat.hpp>

namespace sparkplug::tck {

struct TCKConfig {
  std::string broker_url = "tcp://localhost:1883";
  std::string username;
  std::string password;
  std::string client_id_prefix;
  int utc_window_ms = 5000;
};

enum class TestState { IDLE, RUNNING, COMPLETED, FAILED };

class TCKTestRunner {
public:
  explicit TCKTestRunner(TCKConfig config, std::string profile);
  virtual ~TCKTestRunner();

  TCKTestRunner(const TCKTestRunner&) = delete;
  TCKTestRunner& operator=(const TCKTestRunner&) = delete;
  TCKTestRunner(TCKTestRunner&&) = delete;
  TCKTestRunner& operator=(TCKTestRunner&&) = delete;

  [[nodiscard]] auto start() -> stdx::expected<void, std::string>;
  void stop();
  [[nodiscard]] auto is_running() const -> bool;

protected:
  virtual void dispatch_test(const std::string& test_type,
                             const std::vector<std::string>& params) = 0;

  virtual void handle_end_test() = 0;

  virtual void handle_prompt_specific(const std::string& message) = 0;

  void log(const std::string& level, const std::string& message);
  void publish_result(const std::string& result);
  void publish_console_reply(const std::string& reply);

  [[nodiscard]] auto publish_tck(const std::string& topic,
                                 const std::string& payload,
                                 int qos) -> stdx::expected<void, std::string>;

  [[nodiscard]] static auto get_timestamp() -> uint64_t;

  TCKConfig config_;
  std::atomic<bool> running_{false};
  std::atomic<bool> connected_{false};
  std::mutex mutex_;

  TestState test_state_{TestState::IDLE};
  std::string current_test_name_;
  std::vector<std::string> current_test_params_;

private:
  static void on_connection_lost(void* context, char* cause);
  static int on_message_arrived(void* context,
                                char* topicName,
                                int topicLen,
                                MQTTAsync_message* message);
  static void on_delivery_complete(void* context, MQTTAsync_token token);
  static void on_connect_success(void* context, MQTTAsync_successData* response);
  static void on_connect_failure(void* context, MQTTAsync_failureData* response);
  static void on_subscribe_success(void* context, MQTTAsync_successData* response);
  static void on_subscribe_failure(void* context, MQTTAsync_failureData* response);

  void handle_test_control(const std::string& message);
  void handle_console_prompt(const std::string& message);
  void handle_config(const std::string& message);
  void handle_result_config(const std::string& message);

  MQTTAsync tck_client_;
  std::string profile_;
};

namespace detail {
[[nodiscard]] auto split(const std::string& str, char delim) -> std::vector<std::string>;
[[nodiscard]] auto trim(const std::string& str) -> std::string;
} // namespace detail

} // namespace sparkplug::tck
