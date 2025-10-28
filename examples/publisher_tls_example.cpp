// examples/publisher_tls_example.cpp - TLS/SSL Secure Connection Example
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

  std::cout << "Sparkplug B TLS/SSL Publisher Example\n";
  std::cout << "======================================\n\n";

  // NOTE: You need to set up your MQTT broker with TLS/SSL first
  // For Mosquitto, see: https://mosquitto.org/man/mosquitto-tls-7.html
  //
  // To start test broker: ./certs/start_mosquitto_test.sh
  sparkplug::EdgeNode::TlsOptions tls{
      .trust_store = "certs/ca.crt",     // CA certificate (REQUIRED)
      .key_store = "certs/client.crt",   // Client certificate (optional for mutual TLS)
      .private_key = "certs/client.key", // Client private key (optional)
      .private_key_password = "",        // Password for encrypted key (optional)
      .enabled_cipher_suites = "",       // Custom cipher suites (optional)
      .enable_server_cert_auth = true    // Verify server certificate (default: true)
  };

  sparkplug::EdgeNode::Config config{
      .broker_url = "ssl://localhost:8883", // Use ssl:// prefix for TLS
      .client_id = "sparkplug_tls_publisher",
      .group_id = "Energy",
      .edge_node_id = "SecureGateway01",
      .data_qos = 0,
      .death_qos = 1,
      .clean_session = true,
      .keep_alive_interval = 60,
      .tls = tls // Enable TLS
  };

  std::cout << "Configuration:\n";
  std::cout << "  Broker URL: " << config.broker_url << "\n";
  std::cout << "  Client ID: " << config.client_id << "\n";
  std::cout << "  Group ID: " << config.group_id << "\n";
  std::cout << "  Edge Node ID: " << config.edge_node_id << "\n";
  std::cout << "  TLS Enabled: Yes\n";
  std::cout << "  CA Certificate: " << tls.trust_store << "\n";
  if (!tls.key_store.empty()) {
    std::cout << "  Client Certificate: " << tls.key_store << " (mutual TLS)\n";
  }
  std::cout << "\n";

  sparkplug::EdgeNode publisher(std::move(config));

  std::cout << "Connecting to TLS-enabled broker...\n";
  auto connect_result = publisher.connect();
  if (!connect_result) {
    std::cerr << "Failed to connect: " << connect_result.error() << "\n";
    std::cerr << "\nTroubleshooting:\n";
    std::cerr << "  1. Verify MQTT broker is running with TLS enabled\n";
    std::cerr << "  2. Check CA certificate path is correct\n";
    std::cerr << "  3. Ensure server certificate is valid and trusted\n";
    std::cerr << "  4. For Mosquitto, check mosquitto.conf for TLS settings\n";
    return 1;
  }

  std::cout << "Connected to broker securely via TLS\n";
  std::cout << "  Initial bdSeq: " << publisher.get_bd_seq() << "\n\n";

  sparkplug::PayloadBuilder birth;
  birth.add_metric("bdSeq", static_cast<uint64_t>(publisher.get_bd_seq()));
  birth.add_node_control_rebirth(false);
  birth.add_metric("Properties/Security", "TLS 1.2+");
  birth.add_metric_with_alias("Temperature", 1, 20.5);
  birth.add_metric_with_alias("Voltage", 2, 230.0);

  auto birth_result = publisher.publish_birth(birth);
  if (!birth_result) {
    std::cerr << "Failed to publish NBIRTH: " << birth_result.error() << "\n";
    return 1;
  }

  std::cout << "Published NBIRTH over secure connection\n";
  std::cout << "  Sequence: " << publisher.get_seq() << "\n\n";

  int count = 0;
  double temperature = 20.5;

  std::cout << "Publishing NDATA messages (Ctrl+C to stop)...\n";

  while (running) {
    sparkplug::PayloadBuilder data;
    temperature += 0.1;
    data.add_metric_by_alias(1, temperature);

    auto data_result = publisher.publish_data(data);
    if (!data_result) {
      std::cerr << "Failed to publish NDATA: " << data_result.error() << "\n";
    } else {
      count++;
      if (count % 10 == 0) {
        std::cout << "Published " << count << " secure NDATA messages"
                  << " (seq: " << publisher.get_seq() << ")\n";
      }
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  std::cout << "\nShutting down...\n";

  auto disconnect_result = publisher.disconnect();
  if (!disconnect_result) {
    std::cerr << "Failed to disconnect: " << disconnect_result.error() << "\n";
  } else {
    std::cout << "Disconnected securely (NDEATH sent)\n";
  }

  std::cout << "\nSession Statistics:\n";
  std::cout << "  Total NDATA messages: " << count << "\n";
  std::cout << "  Final sequence: " << publisher.get_seq() << "\n";

  return 0;
}
