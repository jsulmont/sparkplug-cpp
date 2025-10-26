// examples/subscriber_example_c.c - C API subscriber example
#include <signal.h>
#include <sparkplug/sparkplug_c.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static volatile int running = 1;

void signal_handler(int signum) {
  (void)signum;
  running = 0;
}

// Callback function that gets called for each received message
void on_message(const char* topic, const uint8_t* payload_data, size_t payload_len,
                void* user_data) {
  (void)user_data;

  printf("\n=== Message Received ===\n");
  printf("Topic: %s\n", topic);
  printf("Payload size: %zu bytes\n", payload_len);

  sparkplug_payload_t* payload = sparkplug_payload_parse(payload_data, payload_len);
  if (!payload) {
    fprintf(stderr, "Failed to parse payload\n");
    return;
  }

  uint64_t timestamp;
  if (sparkplug_payload_get_timestamp(payload, &timestamp)) {
    printf("Timestamp: %llu\n", (unsigned long long)timestamp);
  }

  uint64_t seq;
  if (sparkplug_payload_get_seq(payload, &seq)) {
    printf("Sequence: %llu\n", (unsigned long long)seq);
  }

  const char* uuid = sparkplug_payload_get_uuid(payload);
  if (uuid) {
    printf("UUID: %s\n", uuid);
  }

  size_t metric_count = sparkplug_payload_get_metric_count(payload);
  printf("Metrics (%zu):\n", metric_count);

  for (size_t i = 0; i < metric_count; i++) {
    sparkplug_metric_t metric;
    if (!sparkplug_payload_get_metric_at(payload, i, &metric)) {
      fprintf(stderr, "Failed to get metric at index %zu\n", i);
      continue;
    }

    printf("  [%zu] ", i);
    if (metric.has_name) {
      printf("%s", metric.name);
    } else if (metric.has_alias) {
      printf("<alias %llu>", (unsigned long long)metric.alias);
    } else {
      printf("<unnamed>");
    }

    if (metric.is_null) {
      printf(" = NULL\n");
    } else {
      printf(" = ");
      switch (metric.datatype) {
      case SPARKPLUG_DATA_TYPE_INT8:
      case SPARKPLUG_DATA_TYPE_INT16:
      case SPARKPLUG_DATA_TYPE_INT32:
        printf("%d (int32)\n", metric.value.int32_value);
        break;
      case SPARKPLUG_DATA_TYPE_INT64:
        printf("%lld (int64)\n", (long long)metric.value.int64_value);
        break;
      case SPARKPLUG_DATA_TYPE_UINT8:
      case SPARKPLUG_DATA_TYPE_UINT16:
      case SPARKPLUG_DATA_TYPE_UINT32:
        printf("%u (uint32)\n", metric.value.uint32_value);
        break;
      case SPARKPLUG_DATA_TYPE_UINT64:
        printf("%llu (uint64)\n", (unsigned long long)metric.value.uint64_value);
        break;
      case SPARKPLUG_DATA_TYPE_FLOAT:
        printf("%f (float)\n", metric.value.float_value);
        break;
      case SPARKPLUG_DATA_TYPE_DOUBLE:
        printf("%f (double)\n", metric.value.double_value);
        break;
      case SPARKPLUG_DATA_TYPE_BOOLEAN:
        printf("%s (bool)\n", metric.value.boolean_value ? "true" : "false");
        break;
      case SPARKPLUG_DATA_TYPE_STRING:
      case SPARKPLUG_DATA_TYPE_TEXT:
        printf("\"%s\" (string)\n", metric.value.string_value);
        break;
      default:
        printf("<unsupported type %d>\n", metric.datatype);
        break;
      }
    }
  }

  printf("========================\n");

  sparkplug_payload_destroy(payload);
}

int main(void) {
  printf("Sparkplug B C Subscriber Example\n");
  printf("=================================\n\n");

  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  sparkplug_subscriber_t* sub = sparkplug_subscriber_create(
      "tcp://localhost:1883", "c_subscriber_example", "Energy", on_message,
      NULL // user_data (optional)
  );

  if (!sub) {
    fprintf(stderr, "Failed to create subscriber\n");
    return 1;
  }

  printf("[OK] Subscriber created\n");

  if (sparkplug_subscriber_connect(sub) != 0) {
    fprintf(stderr, "Failed to connect to broker\n");
    sparkplug_subscriber_destroy(sub);
    return 1;
  }

  printf("[OK] Connected to broker\n");

  if (sparkplug_subscriber_subscribe_all(sub) != 0) {
    fprintf(stderr, "Failed to subscribe\n");
    sparkplug_subscriber_disconnect(sub);
    sparkplug_subscriber_destroy(sub);
    return 1;
  }

  printf("[OK] Subscribed to spBv1.0/Energy/#\n");
  printf("\nListening for messages (Ctrl+C to stop)...\n");

  while (running) {
    sleep(1);
  }

  printf("\n\nShutting down...\n");

  if (sparkplug_subscriber_disconnect(sub) == 0) {
    printf("[OK] Disconnected from broker\n");
  }

  sparkplug_subscriber_destroy(sub);

  printf("[OK] Subscriber destroyed\n");
  printf("\nC subscriber example complete!\n");

  return 0;
}
