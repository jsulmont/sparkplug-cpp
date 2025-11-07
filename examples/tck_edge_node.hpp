#pragma once

#include "tck_test_runner.hpp"

#include <memory>
#include <string>
#include <vector>

#include <sparkplug/edge_node.hpp>
#include <sparkplug/payload_builder.hpp>

namespace sparkplug::tck {

struct TCKEdgeNodeConfig {
  std::string broker_url = "tcp://localhost:1883";
  std::string username;
  std::string password;
  std::string client_id_prefix = "tck_edge_node";
  std::string group_id = "tck_group";
  std::string edge_node_id = "tck_edge";
  std::string namespace_prefix = "spBv1.0";
  int utc_window_ms = 5000;
};

class TCKEdgeNode : public TCKTestRunner {
public:
  explicit TCKEdgeNode(TCKEdgeNodeConfig config);
  ~TCKEdgeNode() override;

protected:
  void dispatch_test(const std::string& test_type,
                     const std::vector<std::string>& params) override;

  void handle_end_test() override;

  void handle_prompt_specific(const std::string& message) override;

private:
  void run_session_establishment_test(const std::vector<std::string>& params);
  void run_session_termination_test(const std::vector<std::string>& params);
  void run_send_data_test(const std::vector<std::string>& params);
  void run_send_complex_data_test(const std::vector<std::string>& params);
  void run_receive_command_test(const std::vector<std::string>& params);
  void run_primary_host_test(const std::vector<std::string>& params);
  void run_multiple_broker_test(const std::vector<std::string>& params);

  [[nodiscard]] auto create_edge_node(const std::string& group_id,
                                      const std::string& edge_node_id,
                                      const std::vector<std::string>& device_ids = {})
      -> stdx::expected<void, std::string>;

  std::string default_group_id_;
  std::string default_edge_node_id_;
  std::unique_ptr<EdgeNode> edge_node_;
  std::string current_group_id_;
  std::string current_edge_node_id_;
  std::vector<std::string> device_ids_;
};

} // namespace sparkplug::tck
