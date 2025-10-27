// examples/test_auth_combined_c.c - C API Production Authentication Example
// Demonstrates combined username/password + mTLS authentication
#include <signal.h>
#include <sparkplug/sparkplug_c.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile int running = 1;

void signal_handler(int sig) {
  (void)sig;
  running = 0;
}

int main(void) {
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  printf("Sparkplug B C API - Production Authentication Example\n");
  printf("=====================================================\n");
  printf("Testing: Username/Password + mTLS (Production Configuration)\n\n");

  sparkplug_publisher_t* pub = sparkplug_publisher_create(
      "ssl://localhost:8883", "c_test_combined_auth", "TestGroup", "TestNodeC");

  if (!pub) {
    fprintf(stderr, "ERROR: Failed to create publisher\n");
    return 1;
  }

  printf("Configuration:\n");
  printf("  Broker URL: ssl://localhost:8883\n");
  printf("  Client ID: c_test_combined_auth\n");
  printf("  Authentication Layers:\n");
  printf("    1. Transport: TLS 1.2+ (encrypted connection)\n");
  printf("    2. Client Auth: mTLS (client certificates)\n");
  printf("    3. User Auth: Username/Password (admin/***)\n");
  printf("  CA Certificate: certs/ca.crt\n");
  printf("  Client Certificate: certs/client.crt\n\n");

  if (sparkplug_publisher_set_tls(pub, "certs/ca.crt", "certs/client.crt", "certs/client.key", NULL,
                                  1) != 0) {
    fprintf(stderr, "ERROR: Failed to configure TLS\n");
    sparkplug_publisher_destroy(pub);
    return 1;
  }

  printf("[OK] TLS configured with client certificates\n");

  if (sparkplug_publisher_set_credentials(pub, "admin", "admin") != 0) {
    fprintf(stderr, "ERROR: Failed to set credentials\n");
    sparkplug_publisher_destroy(pub);
    return 1;
  }

  printf("[OK] Credentials configured\n\n");

  printf("Connecting with combined authentication (mTLS + username/password)...\n");
  if (sparkplug_publisher_connect(pub) != 0) {
    fprintf(stderr, "ERROR: Failed to connect to broker\n");
    fprintf(stderr, "\nTroubleshooting:\n");
    fprintf(stderr, "  1. Start test broker: mosquitto -c certs/mosquitto_test.conf -d\n");
    fprintf(stderr, "  2. Verify certificates exist in certs/ directory\n");
    fprintf(stderr, "  3. Check passwordfile configured in mosquitto_test.conf\n");
    sparkplug_publisher_destroy(pub);
    return 1;
  }

  printf("SUCCESS: Connected with combined authentication\n");
  printf("  Security: Triple-layer (TLS + mTLS + Username/Password)\n");
  printf("  Initial bdSeq: %llu\n\n", (unsigned long long)sparkplug_publisher_get_bd_seq(pub));

  sparkplug_payload_t* birth = sparkplug_payload_create();

  sparkplug_payload_add_uint64(birth, "bdSeq", sparkplug_publisher_get_bd_seq(pub));
  sparkplug_payload_add_bool(birth, "Node Control/Rebirth", false);
  sparkplug_payload_add_string(birth, "Test/AuthMethod", "Combined (mTLS + Username/Password)");
  sparkplug_payload_add_string(birth, "Test/Security",
                               "Production-grade: TLS 1.2+ + Client Certs + Credentials");
  sparkplug_payload_add_double_with_alias(birth, "Temperature", 1, 25.5);

  uint8_t buffer[4096];
  size_t size = sparkplug_payload_serialize(birth, buffer, sizeof(buffer));

  if (size == 0) {
    fprintf(stderr, "ERROR: Failed to serialize NBIRTH payload\n");
    sparkplug_payload_destroy(birth);
    sparkplug_publisher_destroy(pub);
    return 1;
  }

  if (sparkplug_publisher_publish_birth(pub, buffer, size) != 0) {
    fprintf(stderr, "ERROR: Failed to publish NBIRTH\n");
    sparkplug_payload_destroy(birth);
    sparkplug_publisher_destroy(pub);
    return 1;
  }

  printf("SUCCESS: Published NBIRTH message over secure connection\n");
  printf("  Sequence: %llu\n\n", (unsigned long long)sparkplug_publisher_get_seq(pub));

  sparkplug_payload_destroy(birth);

  printf("Publishing test data messages...\n");
  for (int i = 0; i < 3 && running; i++) {
    sparkplug_payload_t* data = sparkplug_payload_create();

    double temp = 25.5 + (i * 0.5);
    sparkplug_payload_add_double_by_alias(data, 1, temp);

    size = sparkplug_payload_serialize(data, buffer, sizeof(buffer));
    sparkplug_payload_destroy(data);

    if (size == 0) {
      fprintf(stderr, "ERROR: Failed to serialize NDATA payload\n");
      continue;
    }

    if (sparkplug_publisher_publish_data(pub, buffer, size) != 0) {
      fprintf(stderr, "ERROR: Failed to publish NDATA\n");
    } else {
      printf("  Published NDATA #%d (seq: %llu)\n", i + 1,
             (unsigned long long)sparkplug_publisher_get_seq(pub));
    }

    sleep(1);
  }

  printf("\nDisconnecting...\n");
  if (sparkplug_publisher_disconnect(pub) != 0) {
    fprintf(stderr, "ERROR: Failed to disconnect\n");
    sparkplug_publisher_destroy(pub);
    return 1;
  }

  printf("SUCCESS: Disconnected securely (NDEATH sent)\n\n");

  sparkplug_publisher_destroy(pub);

  printf("===========================================\n");
  printf("Combined Authentication: PASS\n");
  printf("Production Configuration: VERIFIED\n");
  printf("===========================================\n");

  return 0;
}
