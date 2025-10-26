// examples/subscriber_tls_example.cpp - TLS/SSL Secure Subscriber Example
#include <atomic>
#include <csignal>
#include <iostream>
#include <thread>

#include <sparkplug/subscriber.hpp>

std::atomic<bool> running{true};

void signal_handler(int signal) {
  (void)signal;
  running = false;
}

int main() {
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  std::cout << "Sparkplug B TLS/SSL Subscriber Example\n";
  std::cout << "=======================================\n\n";

  auto callback = [](const sparkplug::Topic& topic,
                     const org::eclipse::tahu::protobuf::Payload& payload) {
    std::cout << "\nReceived secure message:\n";
    std::cout << "  Topic: " << topic.to_string() << "\n";
    std::cout << "  Type: " << static_cast<int>(topic.message_type) << "\n";

    if (payload.has_seq()) {
      std::cout << "  Sequence: " << payload.seq() << "\n";
    }

    std::cout << "  Metrics:\n";
    for (const auto& metric : payload.metrics()) {
      std::cout << "    " << metric.name();
      if (metric.has_alias()) {
        std::cout << " (alias: " << metric.alias() << ")";
      }
      std::cout << " = ";

      switch (metric.datatype()) {
      case 1:
        std::cout << metric.int_value();
        break;
      case 2:
        std::cout << metric.long_value();
        break;
      case 3:
        std::cout << metric.float_value();
        break;
      case 4:
        std::cout << metric.double_value();
        break;
      case 11:
        std::cout << (metric.boolean_value() ? "true" : "false");
        break;
      case 12:
        std::cout << "\"" << metric.string_value() << "\"";
        break;
      default:
        std::cout << "(type: " << metric.datatype() << ")";
      }
      std::cout << "\n";
    }
  };

  // To start test broker: ./certs/start_mosquitto_test.sh
  sparkplug::Subscriber::TlsOptions tls{
      .trust_store = "certs/ca.crt",     // CA certificate (REQUIRED)
      .key_store = "certs/client.crt",   // Client certificate (optional)
      .private_key = "certs/client.key", // Client private key (optional)
      .private_key_password = "",        // Password for encrypted key (optional)
      .enabled_cipher_suites = "",       // Custom cipher suites (optional)
      .enable_server_cert_auth = true    // Verify server certificate (default: true)
  };

  sparkplug::Subscriber::Config config{
      .broker_url = "ssl://localhost:8883", // Use ssl:// prefix for TLS
      .client_id = "sparkplug_tls_subscriber",
      .group_id = "Energy",
      .qos = 1,
      .clean_session = true,
      .validate_sequence = true,
      .tls = tls // Enable TLS
  };

  std::cout << "Configuration:\n";
  std::cout << "  Broker URL: " << config.broker_url << "\n";
  std::cout << "  Client ID: " << config.client_id << "\n";
  std::cout << "  Group ID: " << config.group_id << "\n";
  std::cout << "  TLS Enabled: Yes\n";
  std::cout << "  CA Certificate: " << tls.trust_store << "\n";
  if (!tls.key_store.empty()) {
    std::cout << "  Client Certificate: " << tls.key_store << " (mutual TLS)\n";
  }
  std::cout << "\n";

  // Save group_id before moving config
  std::string group_id = config.group_id;
  sparkplug::Subscriber subscriber(std::move(config), callback);

  std::cout << "Connecting to TLS-enabled broker...\n";
  auto connect_result = subscriber.connect();
  if (!connect_result) {
    std::cerr << "Failed to connect: " << connect_result.error() << "\n";
    std::cerr << "\nTroubleshooting:\n";
    std::cerr << "  1. Verify MQTT broker is running with TLS enabled\n";
    std::cerr << "  2. Check CA certificate path is correct\n";
    std::cerr << "  3. Ensure server certificate is valid and trusted\n";
    std::cerr << "  4. For Mosquitto, check mosquitto.conf for TLS settings\n";
    return 1;
  }

  std::cout << "Connected to broker securely via TLS\n\n";

  auto subscribe_result = subscriber.subscribe_all();
  if (!subscribe_result) {
    std::cerr << "Failed to subscribe: " << subscribe_result.error() << "\n";
    return 1;
  }

  std::cout << "Subscribed to: spBv1.0/" << group_id << "/#\n";
  std::cout << "Waiting for secure messages (Ctrl+C to stop)...\n";

  while (running) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  std::cout << "\nShutting down...\n";

  auto disconnect_result = subscriber.disconnect();
  if (!disconnect_result) {
    std::cerr << "Failed to disconnect: " << disconnect_result.error() << "\n";
  } else {
    std::cout << "Disconnected securely\n";
  }

  return 0;
}
