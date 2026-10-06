// examples/test_auth_password.cpp - Test username/password authentication
#include <atomic>
#include <csignal>
#include <cstdlib>
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

  std::cout << "Sparkplug B Username/Password Authentication Test\n";
  std::cout << "==================================================\n\n";

  // Credentials come from MQTT_USERNAME/MQTT_PASSWORD; admin/admin is a
  // clearly-marked test-only fallback for the local Mosquitto test broker.
  const char* mqtt_user = std::getenv("MQTT_USERNAME");
  const char* mqtt_pass = std::getenv("MQTT_PASSWORD");
  sparkplug::EdgeNode::Config config{.broker_url = "tcp://localhost:1883",
                                     .client_id = "test_auth_client",
                                     .group_id = "TestGroup",
                                     .edge_node_id = "TestNode",
                                     .data_qos = 0,
                                     .death_qos = 1,
                                     .clean_session = true,
                                     .keep_alive_interval = 60,
                                     .tls = std::nullopt,
                                     .username = mqtt_user ? mqtt_user : "admin",
                                     .password = mqtt_pass ? mqtt_pass : "admin"};

  std::cout << "Configuration:\n";
  std::cout << "  Broker URL: " << config.broker_url << "\n";
  std::cout << "  Client ID: " << config.client_id << "\n";
  std::cout << "  Username: " << config.username.value_or("(none)") << "\n";
  std::cout << "  Password: " << (config.password.has_value() ? "***" : "(none)")
            << "\n\n";

  sparkplug::EdgeNode publisher(std::move(config));

  std::cout << "Connecting with username/password authentication...\n";
  auto connect_result = publisher.connect();
  if (!connect_result) {
    std::cerr << "FAILED to connect: " << connect_result.error() << "\n";
    std::cerr << "\nTroubleshooting:\n";
    std::cerr << "  1. Verify Mosquitto is running: brew services list\n";
    std::cerr << "  2. Check credentials are correct (admin/admin)\n";
    std::cerr << "  3. Verify passwordfile exists at "
                 "/opt/homebrew/etc/mosquitto/passwordfile\n";
    return 1;
  }

  std::cout << "SUCCESS: Connected with username/password authentication\n";
  std::cout << "  Initial bdSeq: " << publisher.get_bd_seq() << "\n\n";

  sparkplug::PayloadBuilder birth;
  birth.add_metric("bdSeq", static_cast<uint64_t>(publisher.get_bd_seq()));
  birth.add_node_control_rebirth(false);
  birth.add_metric("Test/AuthMethod", "Username/Password");
  birth.add_metric_with_alias("Temperature", 1, 22.5);

  auto birth_result = publisher.publish_birth(birth);
  if (!birth_result) {
    std::cerr << "FAILED to publish NBIRTH: " << birth_result.error() << "\n";
    return 1;
  }

  std::cout << "SUCCESS: Published NBIRTH message\n";
  std::cout << "  Sequence: " << publisher.get_seq() << "\n\n";

  std::cout << "Publishing test data messages...\n";
  for (int i = 0; i < 3 && running; i++) {
    sparkplug::PayloadBuilder data;
    data.add_metric_by_alias(1, 22.5 + i * 0.5);

    auto data_result = publisher.publish_data(data);
    if (!data_result) {
      std::cerr << "FAILED to publish NDATA: " << data_result.error() << "\n";
    } else {
      std::cout << "  Published NDATA #" << (i + 1) << " (seq: " << publisher.get_seq()
                << ")\n";
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  std::cout << "\nDisconnecting...\n";
  auto disconnect_result = publisher.disconnect();
  if (!disconnect_result) {
    std::cerr << "FAILED to disconnect: " << disconnect_result.error() << "\n";
    return 1;
  }

  std::cout << "SUCCESS: Disconnected cleanly (NDEATH sent)\n";
  std::cout << "\n========================================\n";
  std::cout << "Username/Password Authentication: PASS\n";
  std::cout << "========================================\n";

  return 0;
}
