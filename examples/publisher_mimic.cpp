// examples/publisher_mimic.cpp
// Publishes to the public MIMIC MQTT Lab broker to test Host Application rebirth commands
//
// MIMIC Lab: https://mqttlab.iotsim.io/sparkplug/
// Broker: broker.hivemq.com:1883
// Topics: spBv1.0/# (100 EON nodes with Sparkplug B sensors)
//
// This publisher listens for NCMD commands (especially Node Control/Rebirth)
// and responds appropriately.

#include <atomic>
#include <csignal>
#include <iostream>
#include <thread>

#include <sparkplug/edge_node.hpp>
#include <sparkplug/payload_builder.hpp>

std::atomic<bool> running{true};

void signal_handler(int signal) {
  (void)signal;
  running = false;
}

int main() {
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  std::cout << "=================================================================\n";
  std::cout << "  MIMIC MQTT Lab Sparkplug Publisher\n";
  std::cout << "=================================================================\n";
  std::cout << "Connecting to: broker.hivemq.com:1883\n";
  std::cout << "Publishing as: spBv1.0/TestGroup/NBIRTH/SparkplugCPP\n";
  std::cout << "Info: https://mqttlab.iotsim.io/sparkplug/\n";
  std::cout << "=================================================================\n\n";

  // Command callback to handle NCMD messages (rebirth, reboot, etc.)
  // Use atomic flag to signal rebirth request to main thread
  std::atomic<bool> rebirth_requested{false};

  auto command_callback = [&rebirth_requested](
                              const sparkplug::Topic& topic,
                              const org::eclipse::tahu::protobuf::Payload& payload) {
    std::cout << "\n>>> Received NCMD command from Host Application <<<\n";
    std::cout << "    Topic: " << topic.to_string() << "\n";
    std::cout << "    Metrics: " << payload.metrics_size() << "\n";

    for (const auto& metric : payload.metrics()) {
      std::cout << "    - " << metric.name();

      if (metric.name() == "Node Control/Rebirth" && metric.has_boolean_value() &&
          metric.boolean_value()) {
        std::cout << " = true (REBIRTH REQUESTED)\n";
        rebirth_requested.store(true);
      } else if (metric.name() == "Node Control/Reboot" && metric.has_boolean_value() &&
                 metric.boolean_value()) {
        std::cout << " = true (REBOOT REQUESTED - simulating)\n";
        std::cout << "    (In production, this would reboot the device)\n";
      } else if (metric.name() == "Node Control/Next Server" && metric.has_boolean_value() &&
                 metric.boolean_value()) {
        std::cout << " = true (NEXT SERVER REQUESTED - not implemented)\n";
      } else if (metric.name() == "Node Control/Scan Rate" && metric.has_long_value()) {
        std::cout << " = " << metric.long_value() << " ms (SCAN RATE CHANGE - not implemented)\n";
      } else {
        std::cout << " (value not displayed)\n";
      }
    }

    std::cout << "\n";
  };

  sparkplug::EdgeNode::Config config{
      .broker_url = "tcp://broker.hivemq.com:1883",
      .client_id = "sparkplug_cpp_mimic_publisher",
      .group_id = "TestGroup",
      .edge_node_id = "SparkplugCPP",
      .data_qos = 0,
      .death_qos = 1,
      .clean_session = true,
      .keep_alive_interval = 60,
      .command_callback = command_callback // Subscribe to NCMD commands
  };

  sparkplug::EdgeNode publisher(std::move(config));

  std::cout << "Connecting to broker...\n";
  auto connect_result = publisher.connect();
  if (!connect_result) {
    std::cerr << "Failed to connect: " << connect_result.error() << "\n";
    std::cerr << "\nNote: This requires internet access to broker.hivemq.com\n";
    return 1;
  }

  std::cout << "Connected to broker\n";
  std::cout << "  Initial bdSeq: " << publisher.get_bd_seq() << "\n";

  // Build NBIRTH payload
  sparkplug::PayloadBuilder birth;

  birth.add_metric("bdSeq", static_cast<uint64_t>(publisher.get_bd_seq()));

  // Node Control metrics - these enable Host Applications to send commands
  birth.add_node_control_rebirth(false);
  birth.add_node_control_reboot(false);
  birth.add_node_control_next_server(false);
  birth.add_node_control_scan_rate(static_cast<int64_t>(1000));

  // Properties
  birth.add_metric("Properties/Hardware", "x86_64");
  birth.add_metric("Properties/OS", "macOS");
  birth.add_metric("Properties/Software Version", "sparkplug-cpp-1.0.0");

  // Simulated sensor metrics
  birth.add_metric_with_alias("Temperature", 1, 20.5);
  birth.add_metric_with_alias("Humidity", 2, 45.0);
  birth.add_metric_with_alias("Pressure", 3, 1013.25);
  birth.add_metric_with_alias("Active", 4, true);
  birth.add_metric_with_alias("Uptime", 5, static_cast<int64_t>(0));

  // Publish NBIRTH
  std::cout << "\nPublishing NBIRTH...\n";
  auto birth_result = publisher.publish_birth(birth);
  if (!birth_result) {
    std::cerr << "Failed to publish NBIRTH: " << birth_result.error() << "\n";
    return 1;
  }

  std::cout << "Published NBIRTH\n";
  std::cout << "  Sequence: " << publisher.get_seq() << "\n";
  std::cout << "  bdSeq: " << publisher.get_bd_seq() << "\n";

  // Publish NDATA messages
  int count = 0;
  double temperature = 20.5;
  double humidity = 45.0;
  double pressure = 1013.25;
  int64_t uptime = 0;

  std::cout << "\nPublishing NDATA messages (Ctrl+C to stop)...\n";
  std::cout << "Waiting for NCMD rebirth commands from Host Applications...\n\n";

  while (running) {
    // Check if rebirth was requested by NCMD callback
    if (rebirth_requested.exchange(false)) {
      std::cout << "\n!!! Executing rebirth sequence !!!\n";
      auto result = publisher.rebirth();
      if (!result) {
        std::cerr << "Failed to rebirth: " << result.error() << "\n";
      } else {
        std::cout << "Rebirth complete!\n";
        std::cout << "  New bdSeq: " << publisher.get_bd_seq() << "\n";
        std::cout << "  Sequence reset to: " << publisher.get_seq() << "\n\n";
      }
    }

    // Create NDATA payload with changing values
    sparkplug::PayloadBuilder data;

    // Simulate sensor changes
    temperature += (static_cast<int>(arc4random_uniform(10)) - 5) * 0.1; // Random walk
    humidity += (static_cast<int>(arc4random_uniform(10)) - 5) * 0.5;
    pressure += (static_cast<int>(arc4random_uniform(10)) - 5) * 0.1;
    uptime += 1;

    // Use aliases for bandwidth efficiency
    data.add_metric_by_alias(1, temperature); // Temperature
    data.add_metric_by_alias(2, humidity);    // Humidity
    data.add_metric_by_alias(3, pressure);    // Pressure
    data.add_metric_by_alias(5, uptime);      // Uptime

    auto data_result = publisher.publish_data(data);
    if (!data_result) {
      std::cerr << "Failed to publish NDATA: " << data_result.error() << "\n";
    } else {
      count++;
      if (count % 10 == 0) {
        std::cout << "Published " << count << " NDATA messages (seq: " << publisher.get_seq()
                  << ", bdSeq: " << publisher.get_bd_seq() << ")\n";
      }
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  std::cout << "\n\nShutting down...\n";

  auto disconnect_result = publisher.disconnect();
  if (!disconnect_result) {
    std::cerr << "Failed to disconnect: " << disconnect_result.error() << "\n";
  } else {
    std::cout << "Disconnected (NDEATH sent via MQTT Will)\n";
  }

  std::cout << "\nSession Statistics:\n";
  std::cout << "  Total NDATA messages: " << count << "\n";
  std::cout << "  Final sequence: " << publisher.get_seq() << "\n";
  std::cout << "  Final bdSeq: " << publisher.get_bd_seq() << "\n";

  return 0;
}
