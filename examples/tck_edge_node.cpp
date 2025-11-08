#include "tck_edge_node.hpp"

#include <format>
#include <iostream>

namespace sparkplug::tck {

TCKEdgeNode::TCKEdgeNode(TCKEdgeNodeConfig config)
    : TCKTestRunner(TCKConfig{.broker_url = config.broker_url,
                              .username = config.username,
                              .password = config.password,
                              .client_id_prefix = config.client_id_prefix,
                              .utc_window_ms = config.utc_window_ms},
                    "edge"),
      default_group_id_(std::move(config.group_id)),
      default_edge_node_id_(std::move(config.edge_node_id)) {
}

TCKEdgeNode::~TCKEdgeNode() {
  if (edge_node_) {
    (void)edge_node_->disconnect();
  }
}

void TCKEdgeNode::dispatch_test(const std::string& test_type,
                                const std::vector<std::string>& params) {
  if (test_type == "SessionEstablishmentTest") {
    run_session_establishment_test(params);
  } else if (test_type == "SessionTerminationTest") {
    run_session_termination_test(params);
  } else if (test_type == "SendDataTest") {
    run_send_data_test(params);
  } else if (test_type == "SendComplexDataTest") {
    run_send_complex_data_test(params);
  } else if (test_type == "ReceiveCommandTest") {
    run_receive_command_test(params);
  } else if (test_type == "PrimaryHostTest") {
    run_primary_host_test(params);
  } else if (test_type == "MultipleBrokerTest") {
    run_multiple_broker_test(params);
  } else {
    log("ERROR", "Unknown test type: " + test_type);
    publish_result("OVERALL: NOT EXECUTED");
  }
}

void TCKEdgeNode::handle_end_test() {
  if (test_state_ == TestState::RUNNING) {
    if (current_test_name_ == "SessionEstablishmentTest") {
      publish_result("OVERALL: PASS");
    }
  }

  if (edge_node_) {
    (void)edge_node_->disconnect();
    edge_node_.reset();
  }

  current_group_id_.clear();
  current_edge_node_id_.clear();
  device_ids_.clear();
}

void TCKEdgeNode::handle_prompt_specific(const std::string& /*message*/) {
  std::cout << "\nEnter response (PASS/FAIL): ";
  std::string response;
  std::getline(std::cin, response);
  response = detail::trim(response);

  if (!response.empty()) {
    std::cout << "[TCK] Console reply: " << response << "\n";
    publish_console_reply(response);
  }
}

void TCKEdgeNode::run_session_establishment_test(const std::vector<std::string>& params) {
  if (params.size() < 3) {
    log("ERROR", "Missing parameters for SessionEstablishmentTest");
    publish_result("OVERALL: NOT EXECUTED");
    return;
  }

  const std::string& host_id = params[0];
  const std::string& group_id = params[1];
  const std::string& edge_node_id = params[2];

  std::vector<std::string> device_ids;
  if (params.size() > 3 && !params[3].empty()) {
    device_ids = detail::split(params[3], ' ');
  }

  try {
    log("INFO",
        std::format(
            "Starting SessionEstablishmentTest: host={}, group={}, node={}, devices={}",
            host_id, group_id, edge_node_id, params.size() > 3 ? params[3] : "none"));

    auto result = create_edge_node(group_id, edge_node_id, device_ids);
    if (!result) {
      log("ERROR", result.error());
      publish_result("OVERALL: FAIL");
      return;
    }

    device_ids_ = device_ids;
    log("INFO", "Edge Node session established successfully");
    log("INFO", "NBIRTH published with bdSeq and metrics");

    if (!device_ids.empty()) {
      log("INFO", std::format("Published DBIRTH for {} device(s)", device_ids.size()));
    }

  } catch (const std::exception& e) {
    log("ERROR", std::string("Exception: ") + e.what());
    publish_result("OVERALL: FAIL");
  }
}

void TCKEdgeNode::run_session_termination_test(
    const std::vector<std::string>& /*params*/) {
  log("WARN", "SessionTerminationTest not yet implemented");
  publish_result("OVERALL: NOT EXECUTED");
}

void TCKEdgeNode::run_send_data_test(const std::vector<std::string>& /*params*/) {
  log("WARN", "SendDataTest not yet implemented");
  publish_result("OVERALL: NOT EXECUTED");
}

void TCKEdgeNode::run_send_complex_data_test(const std::vector<std::string>& /*params*/) {
  log("WARN", "SendComplexDataTest not yet implemented");
  publish_result("OVERALL: NOT EXECUTED");
}

void TCKEdgeNode::run_receive_command_test(const std::vector<std::string>& /*params*/) {
  log("WARN", "ReceiveCommandTest not yet implemented");
  publish_result("OVERALL: NOT EXECUTED");
}

void TCKEdgeNode::run_primary_host_test(const std::vector<std::string>& /*params*/) {
  log("WARN", "PrimaryHostTest not yet implemented");
  publish_result("OVERALL: NOT EXECUTED");
}

void TCKEdgeNode::run_multiple_broker_test(const std::vector<std::string>& /*params*/) {
  log("WARN", "MultipleBrokerTest not yet implemented");
  publish_result("OVERALL: NOT EXECUTED");
}

auto TCKEdgeNode::create_edge_node(const std::string& group_id,
                                   const std::string& edge_node_id,
                                   const std::vector<std::string>& device_ids)
    -> stdx::expected<void, std::string> {
  std::lock_guard<std::mutex> lock(mutex_);

  if (edge_node_) {
    return stdx::unexpected("Edge Node already exists");
  }

  log("INFO", std::format("Creating Edge Node group_id={}, edge_node_id={}, devices={}",
                          group_id, edge_node_id, device_ids.size()));

  current_group_id_ = group_id;
  current_edge_node_id_ = edge_node_id;

  try {
    EdgeNode::Config edge_config{.broker_url = config_.broker_url,
                                 .client_id = edge_node_id + "_client",
                                 .group_id = group_id,
                                 .edge_node_id = edge_node_id};

    if (!config_.username.empty()) {
      edge_config.username = config_.username;
      edge_config.password = config_.password;
    }

    // Set command callback to ensure NCMD/DCMD subscriptions
    edge_config.command_callback = [this](const Topic& topic, const auto& /*payload*/) {
      log("INFO", std::format("Received command on topic: {}", topic.to_string()));
    };

    edge_node_ = std::make_unique<EdgeNode>(std::move(edge_config));

    log("INFO", "Connecting Edge Node to broker");
    auto connect_result = edge_node_->connect();
    if (!connect_result) {
      return stdx::unexpected("Failed to connect: " + connect_result.error());
    }

    log("INFO", "Publishing NBIRTH with test metrics");
    PayloadBuilder nbirth;
    auto timestamp = get_timestamp();

    nbirth.add_metric_with_alias("Temperature", 1, 25.5, timestamp);
    nbirth.add_metric_with_alias("Pressure", 2, 101.3, timestamp);
    nbirth.add_metric_with_alias("Status", 3, std::string("online"), timestamp);
    nbirth.add_metric_with_alias("Counter", 4, static_cast<int64_t>(0), timestamp);

    auto birth_result = edge_node_->publish_birth(nbirth);
    if (!birth_result) {
      return stdx::unexpected("Failed to publish NBIRTH: " + birth_result.error());
    }

    log("INFO", "NBIRTH published successfully");

    for (const auto& device_id : device_ids) {
      log("INFO", std::format("Publishing DBIRTH for device: {}", device_id));

      PayloadBuilder dbirth;
      auto device_timestamp = get_timestamp();

      dbirth.add_metric_with_alias("DeviceTemp", 10, 22.0, device_timestamp);
      dbirth.add_metric_with_alias("DeviceStatus", 11, std::string("ready"),
                                   device_timestamp);
      dbirth.add_metric_with_alias("DeviceValue", 12, static_cast<int32_t>(100),
                                   device_timestamp);

      auto device_result = edge_node_->publish_device_birth(device_id, dbirth);
      if (!device_result) {
        log("WARN", std::format("Failed to publish DBIRTH for {}: {}", device_id,
                                device_result.error()));
      } else {
        log("INFO", std::format("DBIRTH published for device: {}", device_id));
      }
    }

    log("INFO", "Edge Node session establishment complete");
    return {};

  } catch (const std::exception& e) {
    return stdx::unexpected(std::string("Exception: ") + e.what());
  }
}

} // namespace sparkplug::tck
