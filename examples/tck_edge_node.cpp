#include "tck_edge_node.hpp"

#include <chrono>
#include <format>
#include <iostream>
#include <thread>

#include <fmt/format.h>

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
        fmt::format(
            "Starting SessionEstablishmentTest: host={}, group={}, node={}, devices={}",
            host_id, group_id, edge_node_id, params.size() > 3 ? params[3] : "none"));

    auto result = create_edge_node(host_id, group_id, edge_node_id, device_ids);
    if (!result) {
      log("ERROR", result.error());
      publish_result("OVERALL: FAIL");
      return;
    }

    device_ids_ = device_ids;
    log("INFO", "Edge Node session established successfully");
    log("INFO", "NBIRTH published with bdSeq and metrics");

    if (!device_ids.empty()) {
      log("INFO", fmt::format("Published DBIRTH for {} device(s)", device_ids.size()));
    }

  } catch (const std::exception& e) {
    log("ERROR", std::string("Exception: ") + e.what());
    publish_result("OVERALL: FAIL");
  }
}

void TCKEdgeNode::run_session_termination_test(const std::vector<std::string>& params) {
  if (params.size() < 3) {
    log("ERROR", "Missing parameters for SessionTerminationTest");
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
        fmt::format(
            "Starting SessionTerminationTest: host={}, group={}, node={}, devices={}",
            host_id, group_id, edge_node_id, params.size() > 3 ? params[3] : "none"));

    if (!edge_node_) {
      auto result = create_edge_node(host_id, group_id, edge_node_id, device_ids);
      if (!result) {
        log("ERROR", result.error());
        publish_result("OVERALL: FAIL");
        return;
      }
      device_ids_ = device_ids;
      log("INFO", "Edge Node session established");
    }

    log("INFO", "Publishing device deaths before node termination");
    for (const auto& device_id : device_ids_) {
      auto result = edge_node_->publish_device_death(device_id);
      if (!result) {
        log("WARN", fmt::format("Failed to publish DDEATH for {}: {}", device_id,
                                result.error()));
      } else {
        log("INFO", fmt::format("DDEATH published for device: {}", device_id));
      }
    }

    log("INFO", "Publishing NDEATH and disconnecting from broker");
    auto death_result = edge_node_->publish_death();
    if (!death_result) {
      log("ERROR", "Failed to publish NDEATH and disconnect: " + death_result.error());
      publish_result("OVERALL: FAIL");
      return;
    }
    log("INFO", "NDEATH published and disconnected successfully");

    edge_node_.reset();
    current_group_id_.clear();
    current_edge_node_id_.clear();
    device_ids_.clear();

    log("INFO", "Edge Node session terminated successfully");
    publish_result("OVERALL: PASS");

  } catch (const std::exception& e) {
    log("ERROR", std::string("Exception: ") + e.what());
    publish_result("OVERALL: FAIL");
  }
}

void TCKEdgeNode::run_send_data_test(const std::vector<std::string>& params) {
  if (params.size() < 3) {
    log("ERROR", "Missing parameters for SendDataTest");
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
        fmt::format("Starting SendDataTest: host={}, group={}, node={}, devices={}",
                    host_id, group_id, edge_node_id,
                    params.size() > 3 ? params[3] : "none"));

    auto result = create_edge_node(host_id, group_id, edge_node_id, device_ids);
    if (!result) {
      log("ERROR", result.error());
      publish_result("OVERALL: FAIL");
      return;
    }

    device_ids_ = device_ids;
    log("INFO", "Edge Node session established successfully");

    log("INFO", "Sending NDATA messages from Edge Node");
    constexpr int num_ndata_messages = 5;
    for (int i = 0; i < num_ndata_messages; ++i) {
      PayloadBuilder ndata;

      ndata.add_metric_by_alias(1, 25.5 + static_cast<double>(i));
      ndata.add_metric_by_alias(2, 101.3 + static_cast<double>(i) * 0.1);
      ndata.add_metric_by_alias(3, std::string("online"));
      ndata.add_metric_by_alias(4, static_cast<int64_t>(i));

      auto ndata_result = edge_node_->publish_data(ndata);
      if (!ndata_result) {
        log("ERROR", "Failed to publish NDATA: " + ndata_result.error());
        publish_result("OVERALL: FAIL");
        return;
      }

      log("INFO", fmt::format("NDATA message {} published with aliases", i + 1));
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    log("INFO", fmt::format("Successfully sent {} NDATA messages", num_ndata_messages));

    if (!device_ids.empty()) {
      log("INFO", "Sending DDATA messages from devices");

      for (const auto& device_id : device_ids) {
        constexpr int num_ddata_messages = 3;
        for (int i = 0; i < num_ddata_messages; ++i) {
          PayloadBuilder ddata;

          ddata.add_metric_by_alias(10, 22.0 + static_cast<double>(i));
          ddata.add_metric_by_alias(11, std::string("ready"));
          ddata.add_metric_by_alias(12, static_cast<int32_t>(100 + i * 10));

          auto ddata_result = edge_node_->publish_device_data(device_id, ddata);
          if (!ddata_result) {
            log("ERROR", fmt::format("Failed to publish DDATA for {}: {}", device_id,
                                     ddata_result.error()));
            publish_result("OVERALL: FAIL");
            return;
          }

          log("INFO", fmt::format("DDATA message {} published for device {} with aliases",
                                  i + 1, device_id));
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        log("INFO", fmt::format("Successfully sent {} DDATA messages for device {}",
                                num_ddata_messages, device_id));
      }
    }

    log("INFO", "SendDataTest completed successfully");
    publish_result("OVERALL: PASS");

  } catch (const std::exception& e) {
    log("ERROR", std::string("Exception: ") + e.what());
    publish_result("OVERALL: FAIL");
  }
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

auto TCKEdgeNode::create_edge_node(const std::string& host_id,
                                   const std::string& group_id,
                                   const std::string& edge_node_id,
                                   const std::vector<std::string>& device_ids)
    -> stdx::expected<void, std::string> {
  std::lock_guard<std::mutex> lock(mutex_);

  if (edge_node_) {
    return stdx::unexpected("Edge Node already exists");
  }

  log("INFO", fmt::format("Creating Edge Node group_id={}, edge_node_id={}, devices={}",
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

    edge_config.command_callback = [this](const Topic& topic, const auto& /*payload*/) {
      log("INFO", fmt::format("Received command on topic: {}", topic.to_string()));
    };

    edge_config.primary_host_id = host_id;

    edge_node_ = std::make_unique<EdgeNode>(std::move(edge_config));

    log("INFO", "Connecting Edge Node to broker");
    auto connect_result = edge_node_->connect();
    if (!connect_result) {
      return stdx::unexpected("Failed to connect: " + connect_result.error());
    }

    if (!host_id.empty()) {
      log("INFO", fmt::format("Waiting for primary host '{}' to be online", host_id));
      constexpr int max_wait_ms = 10000;
      constexpr int poll_interval_ms = 100;
      int waited_ms = 0;

      while (waited_ms < max_wait_ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
        waited_ms += poll_interval_ms;

        if (edge_node_->is_primary_host_online()) {
          log("INFO", "Primary host is now online");
          break;
        }
      }

      if (!edge_node_->is_primary_host_online()) {
        return stdx::unexpected(
            fmt::format("Timeout waiting for primary host '{}' to be online", host_id));
      }
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
      log("INFO", fmt::format("Publishing DBIRTH for device: {}", device_id));

      PayloadBuilder dbirth;
      auto device_timestamp = get_timestamp();

      dbirth.add_metric_with_alias("DeviceTemp", 10, 22.0, device_timestamp);
      dbirth.add_metric_with_alias("DeviceStatus", 11, std::string("ready"),
                                   device_timestamp);
      dbirth.add_metric_with_alias("DeviceValue", 12, static_cast<int32_t>(100),
                                   device_timestamp);

      auto device_result = edge_node_->publish_device_birth(device_id, dbirth);
      if (!device_result) {
        log("WARN", fmt::format("Failed to publish DBIRTH for {}: {}", device_id,
                                device_result.error()));
      } else {
        log("INFO", fmt::format("DBIRTH published for device: {}", device_id));
      }
    }

    log("INFO", "Edge Node session establishment complete");
    return {};

  } catch (const std::exception& e) {
    return stdx::unexpected(std::string("Exception: ") + e.what());
  }
}

} // namespace sparkplug::tck
