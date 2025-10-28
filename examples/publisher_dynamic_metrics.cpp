// examples/publisher_dynamic_metrics.cpp
// Demonstrates adding new metrics at runtime via rebirth

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

  std::cout << "Sparkplug B - Dynamic Metrics Example\n";
  std::cout << "======================================\n\n";
  std::cout << "This example demonstrates how to add new metrics at runtime\n";
  std::cout << "by publishing a new NBIRTH message.\n\n";

  sparkplug::EdgeNode::Config config{.broker_url = "tcp://localhost:1883",
                                     .client_id = "dynamic_metrics_publisher",
                                     .group_id = "Energy",
                                     .edge_node_id = "DynamicNode",
                                     .data_qos = 0,
                                     .death_qos = 1,
                                     .clean_session = true,
                                     .keep_alive_interval = 60};

  sparkplug::EdgeNode publisher(std::move(config));

  auto connect_result = publisher.connect();
  if (!connect_result) {
    std::cerr << "Failed to connect: " << connect_result.error() << "\n";
    return 1;
  }

  std::cout << "✓ Connected to broker\n";
  std::cout << "  Initial bdSeq: " << publisher.get_bd_seq() << "\n\n";

  // ============================================================================
  // PHASE 1: Initial NBIRTH with 2 metrics
  // ============================================================================

  std::cout << "PHASE 1: Publishing initial NBIRTH with 2 metrics\n";
  std::cout << "---------------------------------------------------\n";

  sparkplug::PayloadBuilder initial_birth;
  initial_birth.add_metric("bdSeq", static_cast<uint64_t>(publisher.get_bd_seq()));
  initial_birth.add_metric_with_alias("Temperature", 1, 20.0);
  initial_birth.add_metric_with_alias("Voltage", 2, 230.0);

  auto birth_result = publisher.publish_birth(initial_birth);
  if (!birth_result) {
    std::cerr << "Failed to publish initial NBIRTH: " << birth_result.error() << "\n";
    return 1;
  }

  std::cout << "✓ Published NBIRTH\n";
  std::cout << "  Metrics: Temperature (alias:1), Voltage (alias:2)\n";
  std::cout << "  bdSeq: " << publisher.get_bd_seq() << "\n";
  std::cout << "  seq: " << publisher.get_seq() << "\n\n";

  // ============================================================================
  // PHASE 2: Publish NDATA with initial metrics
  // ============================================================================

  std::cout << "PHASE 2: Publishing NDATA with initial metrics\n";
  std::cout << "-----------------------------------------------\n";

  double temperature = 20.0;
  double voltage = 230.0;

  for (int i = 0; i < 10 && running; i++) {
    sparkplug::PayloadBuilder data;

    temperature += 0.1;
    voltage += 0.5;

    data.add_metric_by_alias(1, temperature); // Temperature
    data.add_metric_by_alias(2, voltage);     // Voltage

    auto data_result = publisher.publish_data(data);
    if (!data_result) {
      std::cerr << "Failed to publish NDATA: " << data_result.error() << "\n";
    } else {
      if ((i + 1) % 5 == 0) {
        std::cout << "✓ Published " << (i + 1) << " NDATA messages (seq: " << publisher.get_seq()
                  << ")\n";
      }
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  if (!running) {
    (void)publisher.disconnect();
    return 0;
  }

  // ============================================================================
  // PHASE 3: Simulate need for new metric
  // ============================================================================

  std::cout << "\n";
  std::cout << "PHASE 3: New metric required!\n";
  std::cout << "------------------------------\n";
  std::cout << "Simulating scenario: Pressure sensor just came online\n";
  std::cout << "We need to add 'Pressure' to our metric inventory\n\n";

  std::this_thread::sleep_for(std::chrono::seconds(2));

  // ============================================================================
  // PHASE 4: Publish NEW NBIRTH with old + new metrics
  // ============================================================================

  std::cout << "PHASE 4: Publishing NEW NBIRTH with expanded metrics\n";
  std::cout << "-----------------------------------------------------\n";

  sparkplug::PayloadBuilder expanded_birth;

  // IMPORTANT: Include ALL old metrics with same aliases
  expanded_birth.add_metric_with_alias("Temperature", 1, temperature);
  expanded_birth.add_metric_with_alias("Voltage", 2, voltage);

  // Add new metric with new alias
  expanded_birth.add_metric_with_alias("Pressure", 3, 101.3);

  // Add more metadata for the new session
  expanded_birth.add_metric("Properties/Sensors", "Temp,Voltage,Pressure");

  // Publishing a new NBIRTH while connected = REBIRTH
  // This automatically increments bdSeq
  auto rebirth_result = publisher.publish_birth(expanded_birth);
  if (!rebirth_result) {
    std::cerr << "Failed to publish expanded NBIRTH: " << rebirth_result.error() << "\n";
    return 1;
  }

  std::cout << "✓ Published NEW NBIRTH (this is a rebirth)\n";
  std::cout << "  Metrics: Temperature (alias:1), Voltage (alias:2), Pressure "
               "(alias:3)\n";
  std::cout << "  bdSeq: " << publisher.get_bd_seq() << " <- INCREMENTED (new session)\n";
  std::cout << "  seq: " << publisher.get_seq() << " <- RESET to 0\n\n";

  // ============================================================================
  // PHASE 5: Publish NDATA with ALL metrics
  // ============================================================================

  std::cout << "PHASE 5: Publishing NDATA with all 3 metrics\n";
  std::cout << "--------------------------------------------\n";

  double pressure = 101.3;

  for (int i = 0; i < 10 && running; i++) {
    sparkplug::PayloadBuilder data;

    temperature += 0.1;
    voltage += 0.5;
    pressure += 0.2;

    // Now we can use all 3 aliases
    data.add_metric_by_alias(1, temperature); // Temperature
    data.add_metric_by_alias(2, voltage);     // Voltage
    data.add_metric_by_alias(3, pressure);    // Pressure (NEW!)

    auto data_result = publisher.publish_data(data);
    if (!data_result) {
      std::cerr << "Failed to publish NDATA: " << data_result.error() << "\n";
    } else {
      if ((i + 1) % 5 == 0) {
        std::cout << "✓ Published " << (i + 1)
                  << " NDATA messages with 3 metrics (seq: " << publisher.get_seq() << ")\n";
      }
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  // ============================================================================
  // Shutdown
  // ============================================================================

  std::cout << "\n⏹ Shutting down...\n";

  auto disconnect_result = publisher.disconnect();
  if (!disconnect_result) {
    std::cerr << "Failed to disconnect: " << disconnect_result.error() << "\n";
  } else {
    std::cout << "✓ Disconnected (NDEATH sent via MQTT Will)\n";
  }

  std::cout << "\n";
  std::cout << "═══════════════════════════════════════════════════════\n";
  std::cout << "SUMMARY - What SCADA/Subscribers Saw:\n";
  std::cout << "═══════════════════════════════════════════════════════\n";
  std::cout << "1. NBIRTH (bdSeq=0): Temperature, Voltage\n";
  std::cout << "2. NDATA (seq 1-10): Temperature, Voltage changing\n";
  std::cout << "3. NBIRTH (bdSeq=1): Temperature, Voltage, Pressure <- NEW "
               "SESSION\n";
  std::cout << "4. NDATA (seq 1-10): All 3 metrics changing\n";
  std::cout << "5. NDEATH: Node offline\n\n";
  std::cout << "Key Point: bdSeq increment signals 'new birth certificate'\n";
  std::cout << "           SCADA knows to update its data model\n";
  std::cout << "═══════════════════════════════════════════════════════\n";

  return 0;
}
