// examples/test_auth_combined.cpp - Test combined username/password + mTLS authentication
#include <atomic>
#include <csignal>
#include <iostream>
#include <thread>

#include <sparkplug/payload_builder.hpp>
#include <sparkplug/publisher.hpp>

std::atomic<bool> running{true};

void signal_handler(int signal) {
  (void)signal;
  running = false;
}

int main() {
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  std::cout << "Sparkplug B Combined Authentication Test\n";
  std::cout << "=========================================\n";
  std::cout << "Testing: Username/Password + mTLS (Production Configuration)\n\n";

  sparkplug::Publisher::TlsOptions tls{.trust_store = "certs/ca.crt",
                                       .key_store = "certs/client.crt",
                                       .private_key = "certs/client.key",
                                       .private_key_password = "",
                                       .enabled_cipher_suites = "",
                                       .enable_server_cert_auth = true};

  sparkplug::Publisher::Config config{.broker_url = "ssl://localhost:8883",
                                      .client_id = "test_combined_auth_client",
                                      .group_id = "TestGroup",
                                      .edge_node_id = "TestNodeCombined",
                                      .data_qos = 0,
                                      .death_qos = 1,
                                      .clean_session = true,
                                      .keep_alive_interval = 60,
                                      .tls = tls,
                                      .username = "admin",
                                      .password = "admin"};

  std::cout << "Configuration:\n";
  std::cout << "  Broker URL: " << config.broker_url << "\n";
  std::cout << "  Client ID: " << config.client_id << "\n";
  std::cout << "  Authentication Layers:\n";
  std::cout << "    1. Transport: TLS 1.2+ (encrypted connection)\n";
  std::cout << "    2. Client Auth: mTLS (client certificates)\n";
  std::cout << "    3. User Auth: Username/Password (" << config.username.value() << "/***)\n";
  std::cout << "  CA Certificate: " << tls.trust_store << "\n";
  std::cout << "  Client Certificate: " << tls.key_store << "\n\n";

  std::cout << "NOTE: Make sure test broker is running with password auth enabled:\n";
  std::cout << "  cd certs && ./start_mosquitto_test.sh\n\n";

  sparkplug::Publisher publisher(std::move(config));

  std::cout << "Connecting with combined authentication (mTLS + username/password)...\n";
  auto connect_result = publisher.connect();
  if (!connect_result) {
    std::cerr << "FAILED to connect: " << connect_result.error() << "\n";
    std::cerr << "\nTroubleshooting:\n";
    std::cerr << "  1. Start test broker: cd certs && ./start_mosquitto_test.sh\n";
    std::cerr << "  2. Verify certificates exist in certs/ directory\n";
    std::cerr << "  3. Check passwordfile configured in mosquitto_test.conf\n";
    std::cerr << "  4. Ensure broker requires both client certs and passwords\n";
    return 1;
  }

  std::cout << "SUCCESS: Connected with combined authentication\n";
  std::cout << "  Security: Triple-layer (TLS + mTLS + Username/Password)\n";
  std::cout << "  Initial bdSeq: " << publisher.get_bd_seq() << "\n\n";

  sparkplug::PayloadBuilder birth;
  birth.add_metric("bdSeq", static_cast<uint64_t>(publisher.get_bd_seq()));
  birth.add_node_control_rebirth(false);
  birth.add_metric("Test/AuthMethod", "Combined (mTLS + Username/Password)");
  birth.add_metric("Test/Security", "Production-grade: TLS 1.2+ + Client Certs + Credentials");
  birth.add_metric_with_alias("Temperature", 1, 25.5);

  auto birth_result = publisher.publish_birth(birth);
  if (!birth_result) {
    std::cerr << "FAILED to publish NBIRTH: " << birth_result.error() << "\n";
    return 1;
  }

  std::cout << "SUCCESS: Published NBIRTH message over secure connection\n";
  std::cout << "  Sequence: " << publisher.get_seq() << "\n\n";

  std::cout << "Publishing test data messages...\n";
  for (int i = 0; i < 3 && running; i++) {
    sparkplug::PayloadBuilder data;
    data.add_metric_by_alias(1, 25.5 + i * 0.5);

    auto data_result = publisher.publish_data(data);
    if (!data_result) {
      std::cerr << "FAILED to publish NDATA: " << data_result.error() << "\n";
    } else {
      std::cout << "  Published NDATA #" << (i + 1) << " (seq: " << publisher.get_seq() << ")\n";
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  std::cout << "\nDisconnecting...\n";
  auto disconnect_result = publisher.disconnect();
  if (!disconnect_result) {
    std::cerr << "FAILED to disconnect: " << disconnect_result.error() << "\n";
    return 1;
  }

  std::cout << "SUCCESS: Disconnected securely (NDEATH sent)\n";
  std::cout << "\n===========================================\n";
  std::cout << "Combined Authentication: PASS\n";
  std::cout << "Production Configuration: VERIFIED\n";
  std::cout << "===========================================\n";

  return 0;
}
