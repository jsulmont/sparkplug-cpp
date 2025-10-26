// examples/publisher_example.cpp - Sparkplug 2.2 Compliant
#include <atomic>
#include <csignal>
#include <iostream>
#include <thread>

#include <sparkplug/payload_builder.hpp>
#include <sparkplug/publisher.hpp>

std::atomic<bool> running{true};
std::atomic<bool> do_rebirth{false};

void signal_handler(int signal) {
  (void)signal;
  running = false;
}

int main() {
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  sparkplug::Publisher::Config config{
      .broker_url = "tcp://localhost:1883",
      .client_id = "sparkplug_publisher_example",
      .group_id = "Energy",
      .edge_node_id = "Gateway01",
      .data_qos = 0,  // Sparkplug requires QoS 0 for data messages
      .death_qos = 1, // Sparkplug requires QoS 1 for NDEATH
      .clean_session = true,
      .keep_alive_interval = 60 // Sparkplug recommends 60 seconds
  };

  sparkplug::Publisher publisher(std::move(config));

  auto connect_result = publisher.connect();
  if (!connect_result) {
    std::cerr << "Failed to connect: " << connect_result.error() << "\n";
    return 1;
  }

  std::cout << "✓ Connected to broker\n";
  std::cout << "  Initial bdSeq: " << publisher.get_bd_seq() << "\n";

  // Build NBIRTH payload
  // NBIRTH must include ALL metrics the node will ever report
  sparkplug::PayloadBuilder birth;

  // REQUIRED: bdSeq metric (will be auto-added if missing, but explicit is
  // better)
  birth.add_metric("bdSeq", static_cast<uint64_t>(publisher.get_bd_seq()));

  // RECOMMENDED: Node Control metrics
  birth.add_node_control_rebirth(false);
  birth.add_node_control_reboot(false);
  birth.add_node_control_next_server(false);
  birth.add_node_control_scan_rate(static_cast<int64_t>(1000)); // 1 second default

  // Properties - metadata about the node
  birth.add_metric("Properties/Hardware", "ARM64");
  birth.add_metric("Properties/OS", "macOS");
  birth.add_metric("Properties/Software Version", "1.0.0");

  // All metrics that will be reported in NDATA
  // These MUST be defined in NBIRTH with name, datatype, and initial value
  birth.add_metric_with_alias("Temperature", 1,
                              20.5);                // Alias 1 for Temperature
  birth.add_metric_with_alias("Voltage", 2, 230.0); // Alias 2 for Voltage
  birth.add_metric_with_alias("Active", 3, true);   // Alias 3 for Active
  birth.add_metric_with_alias("Uptime", 4,
                              static_cast<int64_t>(0)); // Alias 4 for Uptime

  // Publish NBIRTH
  // NBIRTH must be the FIRST message after connect
  // Sequence will be automatically set to 0
  auto birth_result = publisher.publish_birth(birth);
  if (!birth_result) {
    std::cerr << "Failed to publish NBIRTH: " << birth_result.error() << "\n";
    return 1;
  }

  std::cout << "✓ Published NBIRTH\n";
  std::cout << "  Sequence: " << publisher.get_seq() << "\n";
  std::cout << "  bdSeq: " << publisher.get_bd_seq() << "\n";

  // Publish NDATA messages
  int count = 0;
  double temperature = 20.5;
  int64_t uptime = 0;

  std::cout << "\nPublishing NDATA messages (Ctrl+C to stop)...\n";

  while (running) {
    // Check for rebirth command (simulated)
    if (do_rebirth) {
      std::cout << "\n⟳ Rebirth requested, publishing new NBIRTH...\n";
      auto rebirth_result = publisher.rebirth();
      if (!rebirth_result) {
        std::cerr << "Failed to rebirth: " << rebirth_result.error() << "\n";
      } else {
        std::cout << "✓ Rebirth complete\n";
        std::cout << "  New bdSeq: " << publisher.get_bd_seq() << "\n";
      }
      do_rebirth = false;
    }

    // Create NDATA payload
    // BEST PRACTICE: Only include metrics that changed
    // Use aliases to reduce bandwidth
    sparkplug::PayloadBuilder data;

    // Simulate some changing values
    temperature += 0.1;
    uptime += 1;

    // Use alias instead of name (bandwidth optimization)
    data.add_metric_by_alias(1, temperature); // Temperature
    data.add_metric_by_alias(4, uptime);      // Uptime

    // Voltage and Active unchanged, so don't include them (Report by Exception)

    // Sequence number will be automatically incremented and set
    auto data_result = publisher.publish_data(data);
    if (!data_result) {
      std::cerr << "Failed to publish NDATA: " << data_result.error() << "\n";
    } else {
      count++;
      if (count % 10 == 0) {
        std::cout << "✓ Published " << count << " NDATA messages"
                  << " (seq: " << publisher.get_seq() << ")\n";
      }
    }

    // Simulate rebirth after 50 messages
    if (count == 50) {
      do_rebirth = true;
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  std::cout << "\n⏹ Shutting down...\n";

  // Graceful disconnect will trigger NDEATH via MQTT Will
  auto disconnect_result = publisher.disconnect();
  if (!disconnect_result) {
    std::cerr << "Failed to disconnect: " << disconnect_result.error() << "\n";
  } else {
    std::cout << "✓ Disconnected (NDEATH sent via MQTT Will)\n";
  }

  std::cout << "\nSession Statistics:\n";
  std::cout << "  Total NDATA messages: " << count << "\n";
  std::cout << "  Final sequence: " << publisher.get_seq() << "\n";
  std::cout << "  Final bdSeq: " << publisher.get_bd_seq() << "\n";

  return 0;
}