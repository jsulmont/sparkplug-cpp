// examples/command_handling_example.cpp - Command handling demonstration
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

#include <sparkplug/payload_builder.hpp>
#include <sparkplug/publisher.hpp>

std::atomic<bool> running{true};
std::atomic<bool> do_rebirth{false};
std::atomic<int64_t> scan_rate_ms{1000};

void signal_handler(int signal) {
  (void)signal;
  running = false;
}

// Simulates a SCADA host sending commands
void scada_host_thread() {
  std::this_thread::sleep_for(std::chrono::seconds(2));

  sparkplug::Publisher::Config scada_config{.broker_url = "tcp://localhost:1883",
                                            .client_id = "scada_host",
                                            .group_id = "Factory",
                                            .edge_node_id = "ScadaHost",
                                            .data_qos = 0,
                                            .death_qos = 1};

  sparkplug::Publisher scada(std::move(scada_config));

  auto connect_result = scada.connect();
  if (!connect_result) {
    std::cerr << "[SCADA] Failed to connect: " << connect_result.error() << "\n";
    return;
  }

  std::cout << "[SCADA] Connected to broker\n";

  // Publish SCADA NBIRTH (minimal, SCADA doesn't usually have metrics)
  sparkplug::PayloadBuilder scada_birth;
  scada_birth.add_metric("Host Type", "SCADA Primary");
  if (!scada.publish_birth(scada_birth)) {
    std::cerr << "[SCADA] Failed to publish NBIRTH\n";
    return;
  }

  std::cout << "[SCADA] Published NBIRTH\n";

  std::this_thread::sleep_for(std::chrono::seconds(5));
  std::cout << "\n[SCADA] Sending REBIRTH command to Gateway01...\n";

  sparkplug::PayloadBuilder rebirth_cmd;
  rebirth_cmd.add_metric("Node Control/Rebirth", true);

  auto cmd_result = scada.publish_node_command("Gateway01", rebirth_cmd);
  if (!cmd_result) {
    std::cerr << "[SCADA] Failed to send rebirth command: " << cmd_result.error() << "\n";
  } else {
    std::cout << "[SCADA] Rebirth command sent\n";
  }

  std::this_thread::sleep_for(std::chrono::seconds(5));
  std::cout << "\n[SCADA] Sending SCAN RATE command to Gateway01...\n";

  sparkplug::PayloadBuilder scan_cmd;
  scan_cmd.add_metric("Node Control/Scan Rate", static_cast<int64_t>(500)); // 500ms

  cmd_result = scada.publish_node_command("Gateway01", scan_cmd);
  if (!cmd_result) {
    std::cerr << "[SCADA] Failed to send scan rate command: " << cmd_result.error() << "\n";
  } else {
    std::cout << "[SCADA] Scan rate command sent\n";
  }

  std::this_thread::sleep_for(std::chrono::seconds(5));
  std::cout << "\n[SCADA] Sending DEVICE COMMAND to Motor01...\n";

  sparkplug::PayloadBuilder device_cmd;
  device_cmd.add_metric("SetRPM", 2000.0);

  auto dcmd_result = scada.publish_device_command("Gateway01", "Motor01", device_cmd);
  if (!dcmd_result) {
    std::cerr << "[SCADA] Failed to send device command: " << dcmd_result.error() << "\n";
  } else {
    std::cout << "[SCADA] Device command sent\n";
  }

  std::this_thread::sleep_for(std::chrono::seconds(5));
  (void)scada.disconnect();
  std::cout << "[SCADA] Disconnected\n";
}

int main() {
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  // Create command callback for listening to commands
  auto command_callback = [](const sparkplug::Topic& topic,
                             const org::eclipse::tahu::protobuf::Payload& payload) {
    std::cout << "\n[EDGE NODE] Received command: " << topic.to_string() << "\n";

    if (topic.message_type == sparkplug::MessageType::NCMD) {
      for (const auto& metric : payload.metrics()) {
        std::cout << "[EDGE NODE]   Command: " << metric.name() << "\n";

        if (metric.name() == "Node Control/Rebirth" && metric.boolean_value()) {
          std::cout << "[EDGE NODE]   -> Executing REBIRTH\n";
          do_rebirth = true;
        } else if (metric.name() == "Node Control/Scan Rate") {
          auto new_rate = static_cast<int64_t>(metric.long_value());
          std::cout << "[EDGE NODE]   -> Changing scan rate to " << new_rate << "ms\n";
          scan_rate_ms = new_rate;
        } else if (metric.name() == "Node Control/Reboot" && metric.boolean_value()) {
          std::cout << "[EDGE NODE]   -> REBOOT requested (not implemented in this example)\n";
        }
      }
    } else if (topic.message_type == sparkplug::MessageType::DCMD) {
      std::cout << "[EDGE NODE]   Device: " << topic.device_id << "\n";
      for (const auto& metric : payload.metrics()) {
        std::cout << "[EDGE NODE]   Command: " << metric.name();
        if (metric.name() == "SetRPM") {
          std::cout << " = " << metric.double_value() << "\n";
          std::cout << "[EDGE NODE]   -> Setting motor RPM\n";
        }
      }
    }
  };

  sparkplug::Publisher::Config pub_config{.broker_url = "tcp://localhost:1883",
                                          .client_id = "gateway_publisher",
                                          .group_id = "Factory",
                                          .edge_node_id = "Gateway01",
                                          .data_qos = 0,
                                          .death_qos = 1,
                                          .clean_session = true,
                                          .keep_alive_interval = 60,
                                          .tls = {},
                                          .command_callback = command_callback};

  auto publisher = std::make_shared<sparkplug::Publisher>(std::move(pub_config));

  auto connect_result = publisher->connect();
  if (!connect_result) {
    std::cerr << "[EDGE NODE] Failed to connect: " << connect_result.error() << "\n";
    return 1;
  }

  std::cout << "[EDGE NODE] Publisher connected (NCMD subscribed)\n";

  sparkplug::PayloadBuilder node_birth;
  node_birth.add_metric("bdSeq", static_cast<uint64_t>(publisher->get_bd_seq()));
  node_birth.add_node_control_rebirth(false);
  node_birth.add_node_control_scan_rate(scan_rate_ms.load());
  node_birth.add_metric_with_alias("Temperature", 1, 20.0);

  auto birth_result = publisher->publish_birth(node_birth);
  if (!birth_result) {
    std::cerr << "[EDGE NODE] Failed to publish NBIRTH: " << birth_result.error() << "\n";
    return 1;
  }

  std::cout << "[EDGE NODE] Published NBIRTH\n";

  sparkplug::PayloadBuilder device_birth;
  device_birth.add_metric_with_alias("RPM", 1, 1500.0);
  device_birth.add_metric_with_alias("Running", 2, true);

  auto device_birth_result = publisher->publish_device_birth("Motor01", device_birth);
  if (!device_birth_result) {
    std::cerr << "[EDGE NODE] Failed to publish DBIRTH: " << device_birth_result.error() << "\n";
    return 1;
  }

  std::cout << "[EDGE NODE] Published DBIRTH for Motor01\n";

  // Start SCADA host simulation in separate thread
  std::thread scada_thread(scada_host_thread);

  // Main data publishing loop
  int count = 0;
  double temperature = 20.0;

  std::cout << "\n[EDGE NODE] Publishing data (Ctrl+C to stop)...\n";

  while (running) {
    if (do_rebirth) {
      std::cout << "\n[EDGE NODE] *** REBIRTH IN PROGRESS ***\n";
      auto rebirth_result = publisher->rebirth();
      if (!rebirth_result) {
        std::cerr << "[EDGE NODE] Rebirth failed: " << rebirth_result.error() << "\n";
      } else {
        std::cout << "[EDGE NODE] Rebirth complete (new bdSeq: " << publisher->get_bd_seq()
                  << ")\n";
      }
      do_rebirth = false;
    }

    temperature += 0.5;
    sparkplug::PayloadBuilder data;
    data.add_metric_by_alias(1, temperature);

    auto data_result = publisher->publish_data(data);
    if (data_result) {
      count++;
      if (count % 5 == 0) {
        std::cout << "[EDGE NODE] Published " << count << " NDATA messages (temp=" << temperature
                  << ")\n";
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(scan_rate_ms.load()));
  }

  std::cout << "\n[EDGE NODE] Shutting down...\n";

  running = false;
  scada_thread.join();

  (void)publisher->disconnect();

  std::cout << "[EDGE NODE] Disconnected\n";
  std::cout << "\nSession Statistics:\n";
  std::cout << "  Total NDATA messages: " << count << "\n";
  std::cout << "  Final bdSeq: " << publisher->get_bd_seq() << "\n";

  return 0;
}
