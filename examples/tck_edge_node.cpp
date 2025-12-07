#include "tck_edge_node.hpp"

#include <chrono>
#include <format>
#include <iostream>
#include <thread>

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
      log("INFO", std::format("Published DBIRTH for {} device(s)", device_ids.size()));
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
        std::format(
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
        log("WARN", std::format("Failed to publish DDEATH for {}: {}", device_id,
                                result.error()));
      } else {
        log("INFO", std::format("DDEATH published for device: {}", device_id));
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
        std::format("Starting SendDataTest: host={}, group={}, node={}, devices={}",
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

      log("INFO", std::format("NDATA message {} published with aliases", i + 1));
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    log("INFO", std::format("Successfully sent {} NDATA messages", num_ndata_messages));

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
            log("ERROR", std::format("Failed to publish DDATA for {}: {}", device_id,
                                     ddata_result.error()));
            publish_result("OVERALL: FAIL");
            return;
          }

          log("INFO", std::format("DDATA message {} published for device {} with aliases",
                                  i + 1, device_id));
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        log("INFO", std::format("Successfully sent {} DDATA messages for device {}",
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

void TCKEdgeNode::run_send_complex_data_test(const std::vector<std::string>& params) {
  if (params.size() < 3) {
    log("ERROR", "Missing parameters for SendComplexDataTest");
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
            "Starting SendComplexDataTest: host={}, group={}, node={}, devices={}",
            host_id, group_id, edge_node_id, params.size() > 3 ? params[3] : "none"));

    log("INFO", "Creating Edge Node with complex data types");

    EdgeNode::Config edge_config{.broker_url = config_.broker_url,
                                 .client_id = edge_node_id + "_client",
                                 .group_id = group_id,
                                 .edge_node_id = edge_node_id};

    if (!config_.username.empty()) {
      edge_config.username = config_.username;
      edge_config.password = config_.password;
    }

    edge_config.command_callback = [this](const Topic& topic, const auto& /*payload*/) {
      log("INFO", std::format("Received command on topic: {}", topic.to_string()));
    };

    edge_config.primary_host_id = host_id;

    edge_node_ = std::make_unique<EdgeNode>(std::move(edge_config));
    current_group_id_ = group_id;
    current_edge_node_id_ = edge_node_id;

    log("INFO", "Connecting Edge Node to broker");
    auto connect_result = edge_node_->connect();
    if (!connect_result) {
      log("ERROR", "Failed to connect: " + connect_result.error());
      publish_result("OVERALL: FAIL");
      return;
    }

    if (!host_id.empty()) {
      log("INFO", std::format("Waiting for primary host '{}'", host_id));
      constexpr int max_wait_ms = 10000;
      constexpr int poll_interval_ms = 100;
      int waited_ms = 0;

      while (waited_ms < max_wait_ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
        waited_ms += poll_interval_ms;

        if (edge_node_->is_primary_host_online()) {
          log("INFO", "Primary host is online");
          break;
        }
      }

      if (!edge_node_->is_primary_host_online()) {
        log("ERROR", "Timeout waiting for primary host");
        publish_result("OVERALL: FAIL");
        return;
      }
    }

    log("INFO", "Publishing NBIRTH with complex data types");
    PayloadBuilder nbirth;
    auto& nbirth_payload = nbirth.mutable_payload();
    auto timestamp = get_timestamp();

    auto* temp_metric = nbirth_payload.add_metrics();
    temp_metric->set_name("Temperature");
    temp_metric->set_alias(1);
    temp_metric->set_datatype(std::to_underlying(DataType::Double));
    temp_metric->set_double_value(25.5);
    temp_metric->set_timestamp(timestamp);

    auto* bytes_metric = nbirth_payload.add_metrics();
    bytes_metric->set_name("BytesData");
    bytes_metric->set_alias(5);
    bytes_metric->set_datatype(std::to_underlying(DataType::Bytes));
    std::string initial_bytes = "Initial binary data";
    bytes_metric->set_bytes_value(initial_bytes);
    bytes_metric->set_timestamp(timestamp);

    auto* text_metric = nbirth_payload.add_metrics();
    text_metric->set_name("TextField");
    text_metric->set_alias(6);
    text_metric->set_datatype(std::to_underlying(DataType::Text));
    text_metric->set_string_value(
        "This is a text field with more content than a regular string");
    text_metric->set_timestamp(timestamp);

    auto* uuid_metric = nbirth_payload.add_metrics();
    uuid_metric->set_name("UUID");
    uuid_metric->set_alias(7);
    uuid_metric->set_datatype(std::to_underlying(DataType::UUID));
    uuid_metric->set_string_value("550e8400-e29b-41d4-a716-446655440000");
    uuid_metric->set_timestamp(timestamp);

    auto birth_result = edge_node_->publish_birth(nbirth);
    if (!birth_result) {
      log("ERROR", "Failed to publish NBIRTH: " + birth_result.error());
      publish_result("OVERALL: FAIL");
      return;
    }

    log("INFO", "NBIRTH published with complex data types");

    for (const auto& device_id : device_ids) {
      log("INFO", std::format("Publishing DBIRTH for device: {}", device_id));

      PayloadBuilder dbirth;
      auto& dbirth_payload = dbirth.mutable_payload();
      auto device_timestamp = get_timestamp();

      auto* device_bytes = dbirth_payload.add_metrics();
      device_bytes->set_name("DeviceBytes");
      device_bytes->set_alias(20);
      device_bytes->set_datatype(std::to_underlying(DataType::Bytes));
      device_bytes->set_bytes_value("Initial device data");
      device_bytes->set_timestamp(device_timestamp);

      auto device_result = edge_node_->publish_device_birth(device_id, dbirth);
      if (!device_result) {
        log("WARN", std::format("Failed to publish DBIRTH for {}: {}", device_id,
                                device_result.error()));
      } else {
        log("INFO", std::format("DBIRTH published for device: {}", device_id));
      }
    }

    device_ids_ = device_ids;
    log("INFO", "Sending NDATA updates using aliases");
    constexpr int num_updates = 5;
    for (int i = 0; i < num_updates; ++i) {
      PayloadBuilder update_data;
      auto& update_payload = update_data.mutable_payload();

      if (i % 3 == 0) {
        auto* bytes_update = update_payload.add_metrics();
        bytes_update->set_alias(5);
        bytes_update->set_datatype(std::to_underlying(DataType::Bytes));
        std::string update_bytes = std::format("Bytes update {}", i);
        bytes_update->set_bytes_value(update_bytes);
        bytes_update->set_timestamp(get_timestamp());
      }

      if (i % 3 == 1) {
        auto* text_update = update_payload.add_metrics();
        text_update->set_alias(6);
        text_update->set_datatype(std::to_underlying(DataType::Text));
        text_update->set_string_value(
            std::format("Text update {} with extended content", i));
        text_update->set_timestamp(get_timestamp());
      }

      if (i % 3 == 2) {
        auto* uuid_update = update_payload.add_metrics();
        uuid_update->set_alias(7);
        uuid_update->set_datatype(std::to_underlying(DataType::UUID));
        uuid_update->set_string_value(
            std::format("550e8400-e29b-41d4-a716-44665544000{}", i));
        uuid_update->set_timestamp(get_timestamp());
      }

      auto* temp_update = update_payload.add_metrics();
      temp_update->set_alias(1);
      temp_update->set_datatype(std::to_underlying(DataType::Double));
      temp_update->set_double_value(25.5 + static_cast<double>(i));
      temp_update->set_timestamp(get_timestamp());

      auto update_result = edge_node_->publish_data(update_data);
      if (!update_result) {
        log("ERROR", "Failed to publish update: " + update_result.error());
        publish_result("OVERALL: FAIL");
        return;
      }

      log("INFO", std::format("Sent NDATA update {}", i + 1));
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (!device_ids.empty()) {
      log("INFO", "Sending DDATA with complex types from devices");

      for (const auto& device_id : device_ids) {
        constexpr int num_ddata = 3;
        for (int j = 0; j < num_ddata; ++j) {
          PayloadBuilder ddata;
          auto& ddata_payload = ddata.mutable_payload();

          auto* device_bytes = ddata_payload.add_metrics();
          device_bytes->set_alias(20);
          device_bytes->set_datatype(std::to_underlying(DataType::Bytes));
          device_bytes->set_bytes_value(std::format("Device data update {}", j));
          device_bytes->set_timestamp(get_timestamp());

          auto ddata_result = edge_node_->publish_device_data(device_id, ddata);
          if (!ddata_result) {
            log("ERROR", std::format("Failed to publish DDATA for {}: {}", device_id,
                                     ddata_result.error()));
            publish_result("OVERALL: FAIL");
            return;
          }

          log("INFO", std::format("DDATA {} published for device {}", j + 1, device_id));
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
      }
    }

    log("INFO", "SendComplexDataTest completed successfully");
    publish_result("OVERALL: PASS");

  } catch (const std::exception& e) {
    log("ERROR", std::string("Exception: ") + e.what());
    publish_result("OVERALL: FAIL");
  }
}

void TCKEdgeNode::run_receive_command_test(const std::vector<std::string>& params) {
  if (params.size() < 3) {
    log("ERROR", "Missing parameters for ReceiveCommandTest");
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
        std::format("Starting ReceiveCommandTest: host={}, group={}, node={}, devices={}",
                    host_id, group_id, edge_node_id,
                    params.size() > 3 ? params[3] : "none"));

    log("INFO", "Creating Edge Node with command callback");

    std::atomic<int> ncmd_count{0};
    std::atomic<int> dcmd_count{0};

    EdgeNode::Config edge_config{.broker_url = config_.broker_url,
                                 .client_id = edge_node_id + "_client",
                                 .group_id = group_id,
                                 .edge_node_id = edge_node_id};

    if (!config_.username.empty()) {
      edge_config.username = config_.username;
      edge_config.password = config_.password;
    }

    edge_config.command_callback = [this, &ncmd_count, &dcmd_count](const Topic& topic,
                                                                    const auto& payload) {
      if (topic.message_type == MessageType::NCMD) {
        ncmd_count++;
        log("INFO", std::format("Received NCMD #{} with {} metrics", ncmd_count.load(),
                                payload.metrics_size()));

        for (const auto& metric : payload.metrics()) {
          if (metric.has_alias()) {
            log("INFO", std::format("  Alias {}: datatype={}", metric.alias(),
                                    metric.datatype()));
          } else if (metric.has_name()) {
            log("INFO", std::format("  Name '{}': datatype={}", metric.name(),
                                    metric.datatype()));
          }
        }
      } else if (topic.message_type == MessageType::DCMD) {
        dcmd_count++;
        const std::string& dev_id = topic.device_id.empty() ? "unknown" : topic.device_id;
        log("INFO", std::format("Received DCMD #{} for device '{}' with {} metrics",
                                dcmd_count.load(), dev_id, payload.metrics_size()));
      }
    };

    edge_config.primary_host_id = host_id;

    edge_node_ = std::make_unique<EdgeNode>(std::move(edge_config));
    current_group_id_ = group_id;
    current_edge_node_id_ = edge_node_id;

    auto connect_result = edge_node_->connect();
    if (!connect_result) {
      log("ERROR", "Failed to connect: " + connect_result.error());
      publish_result("OVERALL: FAIL");
      return;
    }

    if (!host_id.empty()) {
      constexpr int max_wait_ms = 10000;
      constexpr int poll_interval_ms = 100;
      int waited_ms = 0;

      while (waited_ms < max_wait_ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
        waited_ms += poll_interval_ms;

        if (edge_node_->is_primary_host_online()) {
          log("INFO", "Primary host is online");
          break;
        }
      }

      if (!edge_node_->is_primary_host_online()) {
        log("ERROR", "Timeout waiting for primary host");
        publish_result("OVERALL: FAIL");
        return;
      }
    }

    PayloadBuilder nbirth;
    auto timestamp = get_timestamp();

    nbirth.add_metric_with_alias("Temperature", 1, 25.5, timestamp);
    nbirth.add_metric_with_alias("Pressure", 2, 101.3, timestamp);
    nbirth.add_metric_with_alias("Status", 3, std::string("online"), timestamp);
    nbirth.add_metric_with_alias("Counter", 4, static_cast<int64_t>(0), timestamp);

    auto birth_result = edge_node_->publish_birth(nbirth);
    if (!birth_result) {
      log("ERROR", "Failed to publish NBIRTH: " + birth_result.error());
      publish_result("OVERALL: FAIL");
      return;
    }

    for (const auto& device_id : device_ids) {
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
      }
    }

    device_ids_ = device_ids;
    log("INFO", "Edge Node session established, waiting for commands");

    constexpr int max_wait_seconds = 30;
    log("INFO", std::format("Waiting up to {} seconds for commands", max_wait_seconds));

    for (int i = 0; i < max_wait_seconds * 10; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));

      if (ncmd_count > 0 || dcmd_count > 0) {
        log("INFO", std::format("Received {} NCMD and {} DCMD messages",
                                ncmd_count.load(), dcmd_count.load()));
        break;
      }
    }

    if (ncmd_count == 0 && dcmd_count == 0) {
      log("WARN", "No commands received during test period");
    }

    log("INFO", "ReceiveCommandTest completed successfully");
    publish_result("OVERALL: PASS");

  } catch (const std::exception& e) {
    log("ERROR", std::string("Exception: ") + e.what());
    publish_result("OVERALL: FAIL");
  }
}

void TCKEdgeNode::run_primary_host_test(const std::vector<std::string>& params) {
  if (params.size() < 3) {
    log("ERROR", "Missing parameters for PrimaryHostTest");
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
        std::format("Starting PrimaryHostTest: host={}, group={}, node={}, devices={}",
                    host_id, group_id, edge_node_id,
                    params.size() > 3 ? params[3] : "none"));

    std::atomic<bool> rebirth_received{false};

    EdgeNode::Config edge_config{.broker_url = config_.broker_url,
                                 .client_id = edge_node_id + "_client",
                                 .group_id = group_id,
                                 .edge_node_id = edge_node_id};

    if (!config_.username.empty()) {
      edge_config.username = config_.username;
      edge_config.password = config_.password;
    }

    edge_config.command_callback = [this, &rebirth_received](const Topic& topic,
                                                             const auto& payload) {
      if (topic.message_type == MessageType::NCMD) {
        log("INFO", std::format("Received NCMD with {} metrics", payload.metrics_size()));

        for (const auto& metric : payload.metrics()) {
          if (metric.has_name() && metric.name() == "Node Control/Rebirth") {
            if (metric.has_boolean_value() && metric.boolean_value()) {
              log("INFO", "Rebirth command received from primary host");
              rebirth_received = true;
            }
          }
        }
      }
    };

    edge_config.primary_host_id = host_id;

    edge_node_ = std::make_unique<EdgeNode>(std::move(edge_config));
    current_group_id_ = group_id;
    current_edge_node_id_ = edge_node_id;

    log("INFO", "Connecting to broker");
    auto connect_result = edge_node_->connect();
    if (!connect_result) {
      log("ERROR", "Failed to connect: " + connect_result.error());
      publish_result("OVERALL: FAIL");
      return;
    }

    log("INFO", std::format("Waiting for primary host '{}'", host_id));
    constexpr int max_wait_ms = 15000;
    constexpr int poll_interval_ms = 100;
    int waited_ms = 0;

    while (waited_ms < max_wait_ms) {
      std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
      waited_ms += poll_interval_ms;

      if (edge_node_->is_primary_host_online()) {
        log("INFO", "Primary host is online, proceeding with NBIRTH");
        break;
      }
    }

    if (!edge_node_->is_primary_host_online()) {
      log("ERROR", "Timeout waiting for primary host");
      publish_result("OVERALL: FAIL");
      return;
    }

    PayloadBuilder nbirth;
    auto timestamp = get_timestamp();

    nbirth.add_metric_with_alias("Temperature", 1, 25.5, timestamp);
    nbirth.add_metric_with_alias("Pressure", 2, 101.3, timestamp);
    nbirth.add_metric_with_alias("Status", 3, std::string("online"), timestamp);

    auto birth_result = edge_node_->publish_birth(nbirth);
    if (!birth_result) {
      log("ERROR", "Failed to publish NBIRTH: " + birth_result.error());
      publish_result("OVERALL: FAIL");
      return;
    }

    log("INFO", "NBIRTH published, monitoring primary host STATE");

    for (const auto& device_id : device_ids) {
      PayloadBuilder dbirth;
      auto device_timestamp = get_timestamp();

      dbirth.add_metric_with_alias("DeviceTemp", 10, 22.0, device_timestamp);
      dbirth.add_metric_with_alias("DeviceStatus", 11, std::string("ready"),
                                   device_timestamp);

      auto device_result = edge_node_->publish_device_birth(device_id, dbirth);
      if (!device_result) {
        log("WARN", std::format("Failed to publish DBIRTH for {}: {}", device_id,
                                device_result.error()));
      }
    }

    device_ids_ = device_ids;

    log("INFO", "Publishing NDATA messages");
    for (int i = 0; i < 3; ++i) {
      PayloadBuilder ndata;
      ndata.add_metric_by_alias(1, 25.5 + static_cast<double>(i));

      auto ndata_result = edge_node_->publish_data(ndata);
      if (!ndata_result) {
        log("ERROR", "Failed to publish NDATA: " + ndata_result.error());
      } else {
        log("INFO", std::format("Published NDATA message {}", i + 1));
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    log("INFO", "Monitoring for primary host STATE changes and rebirth commands");
    constexpr int monitor_seconds = 120;
    bool was_online = true;

    for (int i = 0; i < monitor_seconds * 10; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));

      bool is_online = edge_node_->is_primary_host_online();

      if (was_online && !is_online) {
        log("INFO", "Primary host went offline - initiating disconnect/reconnect");

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        auto disconnect_result = edge_node_->disconnect();
        if (!disconnect_result) {
          log("WARN", "Disconnect failed: " + disconnect_result.error());
        } else {
          log("INFO", "Disconnected from broker");
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto reconnect_result = edge_node_->connect();
        if (!reconnect_result) {
          log("ERROR", "Failed to reconnect: " + reconnect_result.error());
          publish_result("OVERALL: FAIL");
          return;
        }

        log("INFO", "Reconnected to broker, waiting for primary host");

        constexpr int wait_for_host_ms = 20000;
        int waited = 0;
        bool host_came_online = false;

        while (waited < wait_for_host_ms) {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          waited += 100;

          if (edge_node_->is_primary_host_online()) {
            log("INFO", "Primary host detected online");
            host_came_online = true;
            break;
          }
        }

        if (!host_came_online) {
          log("WARN",
              "Primary host did not come online within timeout, proceeding with NBIRTH");
        }

        PayloadBuilder new_birth;
        auto birth_ts = get_timestamp();
        new_birth.add_metric_with_alias("Temperature", 1, 26.0, birth_ts);
        new_birth.add_metric_with_alias("Pressure", 2, 102.0, birth_ts);
        new_birth.add_metric_with_alias("Status", 3, std::string("online"), birth_ts);

        if (host_came_online || edge_node_->is_primary_host_online()) {
          auto birth_result = edge_node_->publish_birth(new_birth);
          if (!birth_result) {
            log("WARN", "Failed to publish NBIRTH: " + birth_result.error());
          } else {
            log("INFO", "Published NBIRTH after reconnection with new bdSeq");
          }
        } else {
          log("INFO",
              "Skipping NBIRTH - primary host not online yet, will wait for next cycle");
        }

        was_online = edge_node_->is_primary_host_online();
      } else if (!was_online && is_online) {
        log("INFO", "Primary host came back online - publishing NBIRTH");

        PayloadBuilder late_birth;
        auto late_ts = get_timestamp();
        late_birth.add_metric_with_alias("Temperature", 1, 27.0, late_ts);
        late_birth.add_metric_with_alias("Pressure", 2, 103.0, late_ts);
        late_birth.add_metric_with_alias("Status", 3, std::string("online"), late_ts);

        auto late_birth_result = edge_node_->publish_birth(late_birth);
        if (!late_birth_result) {
          log("WARN", "Failed to publish late NBIRTH: " + late_birth_result.error());
        } else {
          log("INFO", "Published NBIRTH after host came back online");
        }

        was_online = true;
      } else {
        was_online = is_online;
      }

      if (rebirth_received) {
        log("INFO", "Received rebirth command - executing rebirth");

        auto rebirth_result = edge_node_->rebirth();
        if (!rebirth_result) {
          log("ERROR", "Rebirth failed: " + rebirth_result.error());
          publish_result("OVERALL: FAIL");
          return;
        }

        log("INFO", "Rebirth completed successfully");
        rebirth_received = false;
      }
    }

    log("INFO", "Monitoring period complete");

    log("INFO", "PrimaryHostTest completed successfully");
    publish_result("OVERALL: PASS");

  } catch (const std::exception& e) {
    log("ERROR", std::string("Exception: ") + e.what());
    publish_result("OVERALL: FAIL");
  }
}

void TCKEdgeNode::run_multiple_broker_test(const std::vector<std::string>& params) {
  if (params.size() < 4) {
    log("ERROR", "Missing parameters for MultipleBrokerTest");
    publish_result("OVERALL: NOT EXECUTED");
    return;
  }

  const std::string& host_id = params[0];
  const std::string& group_id = params[1];
  const std::string& edge_node_id = params[2];
  const std::string& broker_urls = params[3];

  std::vector<std::string> device_ids;
  if (params.size() > 4 && !params[4].empty()) {
    device_ids = detail::split(params[4], ' ');
  }

  try {
    log("INFO",
        std::format(
            "Starting MultipleBrokerTest: host={}, group={}, node={}, brokers={}, "
            "devices={}",
            host_id, group_id, edge_node_id, broker_urls,
            params.size() > 4 ? params[4] : "none"));

    auto broker_list = detail::split(broker_urls, ',');
    if (broker_list.empty()) {
      log("ERROR", "No broker URLs provided");
      publish_result("OVERALL: FAIL");
      return;
    }

    log("INFO", std::format("Testing with {} broker(s)", broker_list.size()));

    for (size_t i = 0; i < broker_list.size(); ++i) {
      const auto& broker_url = broker_list[i];
      log("INFO", std::format("Connecting to broker {}: {}", i + 1, broker_url));

      EdgeNode::Config edge_config{.broker_url = broker_url,
                                   .client_id = edge_node_id + "_client",
                                   .group_id = group_id,
                                   .edge_node_id = edge_node_id};

      if (!config_.username.empty()) {
        edge_config.username = config_.username;
        edge_config.password = config_.password;
      }

      edge_config.command_callback = [this](const Topic& topic, const auto& payload) {
        log("INFO", std::format("Received command on {} with {} metrics",
                                topic.to_string(), payload.metrics_size()));
      };

      edge_config.primary_host_id = host_id;

      edge_node_ = std::make_unique<EdgeNode>(std::move(edge_config));
      current_group_id_ = group_id;
      current_edge_node_id_ = edge_node_id;

      auto connect_result = edge_node_->connect();
      if (!connect_result) {
        log("ERROR", std::format("Failed to connect to broker {}: {}", broker_url,
                                 connect_result.error()));

        if (i == broker_list.size() - 1) {
          log("ERROR", "Failed to connect to all brokers");
          publish_result("OVERALL: FAIL");
          return;
        }

        log("INFO", "Attempting failover to next broker");
        edge_node_.reset();
        continue;
      }

      log("INFO", std::format("Connected successfully to broker: {}", broker_url));

      if (!host_id.empty()) {
        constexpr int max_wait_ms = 10000;
        constexpr int poll_interval_ms = 100;
        int waited_ms = 0;

        while (waited_ms < max_wait_ms) {
          std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
          waited_ms += poll_interval_ms;

          if (edge_node_->is_primary_host_online()) {
            log("INFO", "Primary host is online");
            break;
          }
        }
      }

      PayloadBuilder nbirth;
      auto timestamp = get_timestamp();

      nbirth.add_metric_with_alias("Temperature", 1, 25.5, timestamp);
      nbirth.add_metric_with_alias("Pressure", 2, 101.3, timestamp);
      nbirth.add_metric_with_alias("Status", 3, std::string("online"), timestamp);

      auto birth_result = edge_node_->publish_birth(nbirth);
      if (!birth_result) {
        log("ERROR", "Failed to publish NBIRTH: " + birth_result.error());
        publish_result("OVERALL: FAIL");
        return;
      }

      log("INFO", "NBIRTH published successfully");

      for (const auto& device_id : device_ids) {
        PayloadBuilder dbirth;
        auto device_timestamp = get_timestamp();

        dbirth.add_metric_with_alias("DeviceTemp", 10, 22.0, device_timestamp);
        dbirth.add_metric_with_alias("DeviceStatus", 11, std::string("ready"),
                                     device_timestamp);

        auto device_result = edge_node_->publish_device_birth(device_id, dbirth);
        if (!device_result) {
          log("WARN", std::format("Failed to publish DBIRTH for {}: {}", device_id,
                                  device_result.error()));
        }
      }

      device_ids_ = device_ids;

      log("INFO", "Publishing test NDATA messages");
      for (int j = 0; j < 3; ++j) {
        PayloadBuilder ndata;
        ndata.add_metric_by_alias(1, 25.5 + static_cast<double>(j));

        auto ndata_result = edge_node_->publish_data(ndata);
        if (!ndata_result) {
          log("ERROR", "Failed to publish NDATA: " + ndata_result.error());
          publish_result("OVERALL: FAIL");
          return;
        }

        log("INFO", std::format("Published NDATA message {}", j + 1));
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }

      log("INFO", std::format("Disconnecting from broker {}", i + 1));
      auto disconnect_result = edge_node_->disconnect();
      if (!disconnect_result) {
        log("WARN", "Failed to disconnect gracefully: " + disconnect_result.error());
      }

      edge_node_.reset();

      if (i < broker_list.size() - 1) {
        log("INFO", "Testing failover to next broker");
        std::this_thread::sleep_for(std::chrono::seconds(2));
      }
    }

    log("INFO", "MultipleBrokerTest completed successfully");
    publish_result("OVERALL: PASS");

  } catch (const std::exception& e) {
    log("ERROR", std::string("Exception: ") + e.what());
    publish_result("OVERALL: FAIL");
  }
}

auto TCKEdgeNode::create_edge_node(const std::string& host_id,
                                   const std::string& group_id,
                                   const std::string& edge_node_id,
                                   const std::vector<std::string>& device_ids)
    -> stdx::expected<void, std::string> {
  std::scoped_lock lock(mutex_);

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

    edge_config.command_callback = [this](const Topic& topic, const auto& /*payload*/) {
      log("INFO", std::format("Received command on topic: {}", topic.to_string()));
    };

    edge_config.primary_host_id = host_id;

    edge_node_ = std::make_unique<EdgeNode>(std::move(edge_config));

    log("INFO", "Connecting Edge Node to broker");
    auto connect_result = edge_node_->connect();
    if (!connect_result) {
      return stdx::unexpected("Failed to connect: " + connect_result.error());
    }

    if (!host_id.empty()) {
      log("INFO", std::format("Waiting for primary host '{}' to be online", host_id));
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
            std::format("Timeout waiting for primary host '{}' to be online", host_id));
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
