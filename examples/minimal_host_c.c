// Minimal host application - C API version
// Quick and dirty KISS example to reproduce disconnect bug

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

void on_message(const char* topic, const uint8_t* payload_data, size_t payload_len,
                void* user_data) {
  (void)user_data;
  (void)payload_data;

  printf("Received: %s (%zu bytes)\n", topic, payload_len);
}

void on_log(int level, const char* message, size_t message_len, void* user_data) {
  (void)level;
  (void)message_len;
  (void)user_data;
  printf("[LOG] %s\n", message);
}

int main(void) {
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  printf("Creating HostApplication...\n");
  sparkplug_host_application_t* host = sparkplug_host_application_create(
      "tcp://localhost:1883", "minimal_host_test_c", "MinimalHostC");
  if (!host) {
    fprintf(stderr, "Failed to create host application\n");
    return 1;
  }

  sparkplug_host_application_set_message_callback(host, on_message, NULL);
  sparkplug_host_application_set_log_callback(host, on_log, NULL);

  printf("Connecting to broker...\n");
  if (sparkplug_host_application_connect(host) != 0) {
    fprintf(stderr, "Connect failed\n");
    sparkplug_host_application_destroy(host);
    return 1;
  }
  printf("Connected!\n");

  printf("Subscribing to all groups...\n");
  if (sparkplug_host_application_subscribe_all(host) != 0) {
    fprintf(stderr, "Subscribe failed\n");
    sparkplug_host_application_disconnect(host);
    sparkplug_host_application_destroy(host);
    return 1;
  }
  printf("Subscribed to spBv1.0/#\n");

  printf("Listening... (Ctrl+C to exit)\n\n");

  while (running) {
    sleep(1);
  }

  printf("\nDisconnecting...\n");
  if (sparkplug_host_application_disconnect(host) != 0) {
    fprintf(stderr, "Disconnect failed\n");
  }

  sparkplug_host_application_destroy(host);
  printf("Done.\n");

  return 0;
}
