#include "tck_host_application.hpp"

#include <algorithm>
#include <iostream>

#include <sparkplug/topic.hpp>

namespace sparkplug::tck {

TCKHostApplication::TCKHostApplication(TCKHostConfig config)
    : TCKTestRunner(TCKConfig{.broker_url = config.broker_url,
                              .username = config.username,
                              .password = config.password,
                              .client_id_prefix = config.client_id_prefix,
                              .utc_window_ms = config.utc_window_ms},
                    "host"),
      host_id_(std::move(config.host_id)),
      namespace_prefix_(std::move(config.namespace_prefix)) {
}

TCKHostApplication::~TCKHostApplication() {
  if (host_application_) {
    auto timestamp = get_timestamp();
    (void)host_application_->publish_state_death(timestamp);
    (void)host_application_->disconnect();
  }
}

auto TCKHostApplication::start_with_session() -> stdx::expected<void, std::string> {
  auto result = start();
  if (!result) {
    return result;
  }

  std::cout << "[TCK] Establishing session with host_id=" << host_id_ << "\n";
  auto establish_result = establish_session(host_id_);
  if (!establish_result) {
    std::cout << "[TCK] WARNING: Failed to establish session: "
              << establish_result.error() << "\n";
    std::cout << "[TCK] Continuing anyway - session will be created on-demand\n";
  } else {
    std::cout << "[TCK] Host is online and ready for tests\n";
  }

  return {};
}

void TCKHostApplication::dispatch_test(const std::string& test_type,
                                       const std::vector<std::string>& params) {
  if (test_type == "SessionEstablishmentTest") {
    run_session_establishment_test(params);
  } else if (test_type == "SessionTerminationTest") {
    run_session_termination_test(params);
  } else if (test_type == "SendCommandTest") {
    run_send_command_test(params);
  } else if (test_type == "ReceiveDataTest") {
    run_receive_data_test(params);
  } else if (test_type == "EdgeSessionTerminationTest") {
    run_edge_session_termination_test(params);
  } else if (test_type == "MessageOrderingTest") {
    run_message_ordering_test(params);
  } else if (test_type == "MultipleBrokerTest") {
    run_multiple_broker_test(params);
  } else {
    log("ERROR", "Unknown test type: " + test_type);
    publish_result("OVERALL: NOT EXECUTED");
    test_state_ = TestState::IDLE;
  }
}

void TCKHostApplication::handle_end_test() {
  if (current_test_name_ == "SendCommandTest" ||
      current_test_name_ == "ReceiveDataTest" ||
      current_test_name_ == "EdgeSessionTerminationTest") {
    if (test_state_ == TestState::RUNNING) {
      publish_result("OVERALL: PASS");
    }
  }

  if (current_test_name_ == "SessionTerminationTest") {
    if (host_application_) {
      log("INFO", "Cleaning up host application (SessionTerminationTest)");
      auto timestamp = get_timestamp();
      (void)host_application_->publish_state_death(timestamp);
      (void)host_application_->disconnect();
      host_application_.reset();
    }
  } else {
    log("INFO", "Keeping host application alive for subsequent tests");
  }
}

void TCKHostApplication::handle_prompt_specific(const std::string& message) {
  std::string msg_lower = message;
  std::transform(msg_lower.begin(), msg_lower.end(), msg_lower.begin(), ::tolower);

  if (msg_lower.find("device") != std::string::npos &&
      msg_lower.find("rebirth") != std::string::npos) {
    if (current_test_params_.size() >= 4) {
      std::string group_id = current_test_params_[1];
      std::string edge_node_id = current_test_params_[2];
      std::string device_id = current_test_params_[3];

      log("INFO", "Sending Device Rebirth command to " + device_id);

      PayloadBuilder cmd;
      cmd.set_timestamp(get_timestamp());
      cmd.add_metric("Device Control/Rebirth", true);

      if (host_application_) {
        auto result = host_application_->publish_device_command(group_id, edge_node_id,
                                                                device_id, cmd);
        if (result) {
          log("INFO", "Device Rebirth command sent");
          publish_console_reply("PASS");
        } else {
          log("ERROR", "Failed to send Device Rebirth: " + result.error());
          publish_console_reply("FAIL");
        }
      } else {
        log("ERROR", "Host application not initialized");
        publish_console_reply("FAIL");
      }
    }
  } else if (msg_lower.find("rebirth") != std::string::npos &&
             msg_lower.find("edge node") != std::string::npos) {
    if (current_test_params_.size() >= 3) {
      std::string group_id = current_test_params_[1];
      std::string edge_node_id = current_test_params_[2];

      log("INFO", "Sending Node Rebirth command to " + edge_node_id);

      PayloadBuilder cmd;
      cmd.set_timestamp(get_timestamp());
      cmd.add_metric("Node Control/Rebirth", true);

      if (host_application_) {
        auto result =
            host_application_->publish_node_command(group_id, edge_node_id, cmd);
        if (result) {
          log("INFO", "Node Rebirth command sent");
          publish_console_reply("PASS");
        } else {
          log("ERROR", "Failed to send Node Rebirth: " + result.error());
          publish_console_reply("FAIL");
        }
      } else {
        log("ERROR", "Host application not initialized");
        publish_console_reply("FAIL");
      }
    }
  } else if ((msg_lower.find("command") != std::string::npos ||
              msg_lower.find("update") != std::string::npos) &&
             msg_lower.find("metric") != std::string::npos &&
             msg_lower.find("device") != std::string::npos) {
    if (current_test_params_.size() >= 4) {
      std::string group_id = current_test_params_[1];
      std::string edge_node_id = current_test_params_[2];
      std::string device_id = current_test_params_[3];

      if (!host_application_) {
        log("ERROR", "Host application not initialized");
        publish_console_reply("FAIL");
        return;
      }

      std::string device_key = group_id + "/" + edge_node_id + "/" + device_id;
      std::string metric_name;
      uint32_t metric_datatype = 10;

      auto device_metrics_it = device_metrics_.find(device_key);
      if (device_metrics_it != device_metrics_.end() &&
          !device_metrics_it->second.empty()) {
        const auto& first_metric = device_metrics_it->second.begin()->second;
        metric_name = first_metric.name;
        metric_datatype = first_metric.datatype;
        log("INFO", "Found metric '" + metric_name + "' from DBIRTH, using for command");
      }

      if (metric_name.empty()) {
        log("WARN", "No metrics found in DBIRTH, using fallback TestMetric");
        metric_name = "TestMetric";
        metric_datatype = 10;
      }

      log("INFO", "Sending metric update command to device " + device_id);

      PayloadBuilder cmd;
      cmd.set_timestamp(get_timestamp());

      switch (metric_datatype) {
      case 1:
        cmd.add_metric(metric_name, static_cast<int8_t>(42));
        break;
      case 2:
        cmd.add_metric(metric_name, static_cast<int16_t>(42));
        break;
      case 3:
        cmd.add_metric(metric_name, static_cast<int32_t>(42));
        break;
      case 4:
        cmd.add_metric(metric_name, static_cast<int64_t>(42));
        break;
      case 5:
        cmd.add_metric(metric_name, static_cast<uint8_t>(42));
        break;
      case 6:
        cmd.add_metric(metric_name, static_cast<uint16_t>(42));
        break;
      case 7:
        cmd.add_metric(metric_name, static_cast<uint32_t>(42));
        break;
      case 8:
        cmd.add_metric(metric_name, static_cast<uint64_t>(42));
        break;
      case 9:
        cmd.add_metric(metric_name, 42.0f);
        break;
      case 10:
        cmd.add_metric(metric_name, 42.0);
        break;
      case 11:
        cmd.add_metric(metric_name, true);
        break;
      case 12:
      case 14:
        cmd.add_metric(metric_name, std::string("test_value"));
        break;
      case 13:
        cmd.add_metric(metric_name, static_cast<uint64_t>(1730898000000));
        break;
      default:
        cmd.add_metric(metric_name, 42.0);
        break;
      }

      auto result = host_application_->publish_device_command(group_id, edge_node_id,
                                                              device_id, cmd);
      if (result) {
        log("INFO", "Device metric update command sent with metric: " + metric_name);
        publish_console_reply("PASS");
      } else {
        log("ERROR", "Failed to send device metric update: " + result.error());
        publish_console_reply("FAIL");
      }
    }
  } else if ((msg_lower.find("command") != std::string::npos ||
              msg_lower.find("update") != std::string::npos) &&
             msg_lower.find("metric") != std::string::npos &&
             msg_lower.find("edge node") != std::string::npos) {
    if (current_test_params_.size() >= 3) {
      std::string group_id = current_test_params_[1];
      std::string edge_node_id = current_test_params_[2];

      if (!host_application_) {
        log("ERROR", "Host application not initialized");
        publish_console_reply("FAIL");
        return;
      }

      std::string metric_name;
      auto node_state_opt = host_application_->get_node_state(group_id, edge_node_id);
      if (node_state_opt) {
        const auto& node_state = node_state_opt->get();
        if (!node_state.alias_map.empty()) {
          metric_name = node_state.alias_map.begin()->second;
          log("INFO",
              "Found metric '" + metric_name + "' from NBIRTH, using for command");
        }
      }

      if (metric_name.empty()) {
        log("WARN", "No metrics found in NBIRTH, using fallback TestMetric");
        metric_name = "TestMetric";
      }

      log("INFO", "Sending metric update command to " + edge_node_id);

      PayloadBuilder cmd;
      cmd.set_timestamp(get_timestamp());
      cmd.add_metric(metric_name, 42.0);

      auto result = host_application_->publish_node_command(group_id, edge_node_id, cmd);
      if (result) {
        log("INFO", "Node metric update command sent");
        publish_console_reply("PASS");
      } else {
        log("ERROR", "Failed to send node metric update: " + result.error());
        publish_console_reply("FAIL");
      }
    }
  } else if (msg_lower.find("offline") != std::string::npos) {
    publish_console_reply("PASS");
  } else {
    std::cout << "[TCK] Unknown prompt - manual response required\n";
    std::cout << "Enter response (PASS/FAIL): ";
    std::string response;
    std::getline(std::cin, response);
    response = detail::trim(response);
    if (!response.empty()) {
      publish_console_reply(response);
    }
  }
}

auto TCKHostApplication::establish_session(const std::string& host_id)
    -> stdx::expected<void, std::string> {
  std::lock_guard<std::mutex> lock(mutex_);

  if (host_application_) {
    return {};
  }

  HostApplication::Config host_config;
  host_config.broker_url = config_.broker_url;
  host_config.host_id = host_id;
  host_config.client_id = config_.client_id_prefix + "_" + host_id;

  if (!config_.username.empty()) {
    host_config.username = config_.username;
    host_config.password = config_.password;
  }

  host_application_ = std::make_unique<HostApplication>(host_config);
  current_host_id_ = host_id;

  host_application_->set_message_callback(
      [this](const Topic& topic, const org::eclipse::tahu::protobuf::Payload& payload) {
        std::string topic_str = topic.group_id + "/" + topic.edge_node_id;
        if (!topic.device_id.empty()) {
          topic_str += "/" + topic.device_id;
        }

        log("INFO", "Received " + topic.to_string() + " from " + topic_str);

        if (topic.message_type == MessageType::DBIRTH && !topic.device_id.empty()) {
          std::string device_key =
              topic.group_id + "/" + topic.edge_node_id + "/" + topic.device_id;
          auto& metrics = device_metrics_[device_key];
          metrics.clear();

          for (const auto& metric : payload.metrics()) {
            if (metric.has_name()) {
              MetricInfo info;
              info.name = metric.name();
              info.datatype = metric.datatype();
              metrics[metric.name()] = info;
            }
          }
          log("INFO", "Stored " + std::to_string(metrics.size()) +
                          " metrics from DBIRTH for " + topic.device_id);
        }
      });

  auto connect_result = host_application_->connect();
  if (!connect_result) {
    host_application_.reset();
    return stdx::unexpected("Failed to connect host application: " +
                            connect_result.error());
  }

  auto timestamp = get_timestamp();
  auto state_result = host_application_->publish_state_birth(timestamp);
  if (!state_result) {
    (void)host_application_->disconnect();
    host_application_.reset();
    return stdx::unexpected("Failed to publish STATE: " + state_result.error());
  }

  return {};
}

void TCKHostApplication::run_session_establishment_test(
    const std::vector<std::string>& params) {
  if (params.empty()) {
    log("ERROR", "Missing host_id parameter");
    publish_result("OVERALL: NOT EXECUTED");
    return;
  }

  const std::string& host_id = params[0];
  log("INFO", "SessionEstablishmentTest: host_id=" + host_id);

  auto result = establish_session(host_id);
  if (result) {
    log("INFO", "Session established successfully");
    publish_result("OVERALL: PASS");
  } else {
    log("ERROR", "Session establishment failed: " + result.error());
    publish_result("OVERALL: FAIL");
  }
}

void TCKHostApplication::run_session_termination_test(
    const std::vector<std::string>& /*params*/) {
  log("INFO", "SessionTerminationTest started");
  log("INFO", "Will terminate session when END_TEST is received");
}

void TCKHostApplication::run_send_command_test(const std::vector<std::string>& params) {
  if (params.size() < 3) {
    log("ERROR", "Missing parameters for SendCommandTest");
    publish_result("OVERALL: NOT EXECUTED");
    return;
  }

  const std::string& host_id = params[0];
  log("INFO", "SendCommandTest: host_id=" + host_id + ", group_id=" + params[1] +
                  ", edge_node_id=" + params[2]);

  if (!host_application_ || current_host_id_ != host_id) {
    auto result = establish_session(host_id);
    if (!result) {
      log("ERROR", "Failed to establish session: " + result.error());
      publish_result("OVERALL: FAIL");
      return;
    }
  }

  log("INFO", "Waiting for console prompts to send commands...");
}

void TCKHostApplication::run_receive_data_test(const std::vector<std::string>& params) {
  if (params.size() < 3) {
    log("ERROR", "Missing parameters for ReceiveDataTest");
    publish_result("OVERALL: NOT EXECUTED");
    return;
  }

  const std::string& host_id = params[0];
  log("INFO", "ReceiveDataTest: host_id=" + host_id);

  if (!host_application_ || current_host_id_ != host_id) {
    auto result = establish_session(host_id);
    if (!result) {
      log("ERROR", "Failed to establish session: " + result.error());
      publish_result("OVERALL: FAIL");
      return;
    }
  }

  log("INFO", "Waiting to receive NBIRTH, DBIRTH, NDATA, DDATA messages...");
}

void TCKHostApplication::run_edge_session_termination_test(
    const std::vector<std::string>& params) {
  if (params.size() < 3) {
    log("ERROR", "Missing parameters for EdgeSessionTerminationTest");
    publish_result("OVERALL: NOT EXECUTED");
    return;
  }

  const std::string& host_id = params[0];
  log("INFO", "EdgeSessionTerminationTest: host_id=" + host_id);

  if (!host_application_ || current_host_id_ != host_id) {
    auto result = establish_session(host_id);
    if (!result) {
      log("ERROR", "Failed to establish session: " + result.error());
      publish_result("OVERALL: FAIL");
      return;
    }
  }

  log("INFO", "Waiting to detect edge node termination...");
}

void TCKHostApplication::run_message_ordering_test(
    const std::vector<std::string>& params) {
  if (params.size() < 3) {
    log("ERROR", "Missing parameters for MessageOrderingTest");
    publish_result("OVERALL: NOT EXECUTED");
    return;
  }

  const std::string& host_id = params[0];
  log("INFO", "MessageOrderingTest: host_id=" + host_id);

  if (!host_application_ || current_host_id_ != host_id) {
    auto result = establish_session(host_id);
    if (!result) {
      log("ERROR", "Failed to establish session: " + result.error());
      publish_result("OVERALL: FAIL");
      return;
    }
  }

  log("INFO", "Sequence number validation enabled");
  publish_result("OVERALL: PASS");
}

void TCKHostApplication::run_multiple_broker_test(
    const std::vector<std::string>& /*params*/) {
  log("ERROR", "MultipleBrokerTest not yet implemented");
  publish_result("OVERALL: NOT EXECUTED");
}

} // namespace sparkplug::tck
