#pragma once

#include "tck_test_runner.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <sparkplug/host_application.hpp>
#include <sparkplug/payload_builder.hpp>

namespace sparkplug::tck {

struct TCKHostConfig {
  std::string broker_url = "tcp://localhost:1883";
  std::string username;
  std::string password;
  std::string client_id_prefix = "tck_host_app";
  std::string host_id = "scada_host_id";
  std::string namespace_prefix = "spBv1.0";
  int utc_window_ms = 5000;
};

class TCKHostApplication : public TCKTestRunner {
public:
  explicit TCKHostApplication(TCKHostConfig config);
  ~TCKHostApplication() override;

  auto start_with_session() -> stdx::expected<void, std::string>;

protected:
  void dispatch_test(const std::string& test_type,
                     const std::vector<std::string>& params) override;

  void handle_end_test() override;

  void handle_prompt_specific(const std::string& message) override;

private:
  void run_session_establishment_test(const std::vector<std::string>& params);
  void run_session_termination_test(const std::vector<std::string>& params);
  void run_send_command_test(const std::vector<std::string>& params);
  void run_receive_data_test(const std::vector<std::string>& params);
  void run_edge_session_termination_test(const std::vector<std::string>& params);
  void run_message_ordering_test(const std::vector<std::string>& params);
  void run_multiple_broker_test(const std::vector<std::string>& params);

  [[nodiscard]] auto
  establish_session(const std::string& host_id) -> stdx::expected<void, std::string>;

  std::string host_id_;
  std::string namespace_prefix_;
  std::unique_ptr<HostApplication> host_application_;
  std::string current_host_id_;

  struct MetricInfo {
    std::string name;
    uint32_t datatype;
  };
  std::unordered_map<std::string, std::unordered_map<std::string, MetricInfo>>
      device_metrics_;
};

} // namespace sparkplug::tck
