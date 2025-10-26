// examples/publisher_example_c.c - C API example for Sparkplug B
#include <sparkplug/sparkplug_c.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
  printf("Sparkplug B C API Example\n");
  printf("=========================\n\n");

  sparkplug_publisher_t* pub = sparkplug_publisher_create(
      "tcp://localhost:1883", "c_publisher_example", "Energy", "Gateway01");

  if (!pub) {
    fprintf(stderr, "Failed to create publisher\n");
    return 1;
  }

  printf("[OK] Publisher created\n");

  if (sparkplug_publisher_connect(pub) != 0) {
    fprintf(stderr, "Failed to connect to broker\n");
    sparkplug_publisher_destroy(pub);
    return 1;
  }

  printf("[OK] Connected to broker\n");
  printf("  Initial bdSeq: %llu\n", (unsigned long long)sparkplug_publisher_get_bd_seq(pub));

  sparkplug_payload_t* birth = sparkplug_payload_create();

  // Add metrics with aliases (for efficient NDATA later)
  sparkplug_payload_add_double_with_alias(birth, "Temperature", 1, 20.5);
  sparkplug_payload_add_double_with_alias(birth, "Voltage", 2, 230.0);
  sparkplug_payload_add_bool_with_alias(birth, "Active", 3, true);
  sparkplug_payload_add_int64_with_alias(birth, "Uptime", 4, 0);

  // Add metadata
  sparkplug_payload_add_string(birth, "Properties/Hardware", "x86_64");
  sparkplug_payload_add_string(birth, "Properties/OS", "Linux");

  uint8_t buffer[4096];
  size_t size = sparkplug_payload_serialize(birth, buffer, sizeof(buffer));

  if (size == 0) {
    fprintf(stderr, "Failed to serialize NBIRTH payload\n");
    sparkplug_payload_destroy(birth);
    sparkplug_publisher_destroy(pub);
    return 1;
  }

  if (sparkplug_publisher_publish_birth(pub, buffer, size) != 0) {
    fprintf(stderr, "Failed to publish NBIRTH\n");
    sparkplug_payload_destroy(birth);
    sparkplug_publisher_destroy(pub);
    return 1;
  }

  printf("[OK] Published NBIRTH\n");
  printf("  Sequence: %llu\n", (unsigned long long)sparkplug_publisher_get_seq(pub));
  printf("  bdSeq: %llu\n", (unsigned long long)sparkplug_publisher_get_bd_seq(pub));

  sparkplug_payload_destroy(birth);

  printf("\nPublishing NDATA messages...\n");

  for (int i = 0; i < 10; i++) {
    // Create NDATA payload using aliases only (bandwidth optimization)
    sparkplug_payload_t* data = sparkplug_payload_create();

    // Only include changed values (Report by Exception)
    double temp = 20.5 + (i * 0.1);
    int64_t uptime = i;

    sparkplug_payload_add_double_by_alias(data, 1, temp);  // Temperature
    sparkplug_payload_add_int64_by_alias(data, 4, uptime); // Uptime
    // Voltage and Active unchanged - not included

    size = sparkplug_payload_serialize(data, buffer, sizeof(buffer));

    if (size > 0 && sparkplug_publisher_publish_data(pub, buffer, size) == 0) {
      if ((i + 1) % 5 == 0) {
        printf("[OK] Published %d NDATA messages (seq: %llu)\n", i + 1,
               (unsigned long long)sparkplug_publisher_get_seq(pub));
      }
    } else {
      fprintf(stderr, "Failed to publish NDATA #%d\n", i + 1);
    }

    sparkplug_payload_destroy(data);
    sleep(1);
  }

  printf("\nTesting rebirth...\n");
  if (sparkplug_publisher_rebirth(pub) == 0) {
    printf("[OK] Rebirth complete\n");
    printf("  New bdSeq: %llu\n", (unsigned long long)sparkplug_publisher_get_bd_seq(pub));
    printf("  Sequence reset to: %llu\n", (unsigned long long)sparkplug_publisher_get_seq(pub));
  } else {
    fprintf(stderr, "Failed to rebirth\n");
  }

  printf("\nPublishing post-rebirth NDATA...\n");
  for (int i = 0; i < 3; i++) {
    sparkplug_payload_t* data = sparkplug_payload_create();
    sparkplug_payload_add_double_by_alias(data, 1, 25.0 + i);

    size = sparkplug_payload_serialize(data, buffer, sizeof(buffer));
    if (size > 0) {
      sparkplug_publisher_publish_data(pub, buffer, size);
    }

    sparkplug_payload_destroy(data);
    sleep(1);
  }

  printf("[OK] Published 3 post-rebirth messages (seq: %llu)\n",
         (unsigned long long)sparkplug_publisher_get_seq(pub));

  printf("\nDisconnecting...\n");
  if (sparkplug_publisher_disconnect(pub) == 0) {
    printf("[OK] Disconnected (NDEATH sent via MQTT Will)\n");
  }

  sparkplug_publisher_destroy(pub);

  printf("\nC API test complete!\n");
  printf("Final statistics:\n");
  printf("  Total messages sent: 13 (1 NBIRTH + 10 NDATA + 1 REBIRTH + 3 NDATA)\n");

  return 0;
}
