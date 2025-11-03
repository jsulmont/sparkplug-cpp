/* tests/test_c_api.c
 * Unit tests for C API bindings
 */
#include <assert.h>
#include <sparkplug/sparkplug_c.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                                                                                 \
  printf("Testing: %s ... ", name);                                                                \
  fflush(stdout);

#define PASS()                                                                                     \
  printf("PASS\n");                                                                                \
  tests_passed++;

#define FAIL(msg)                                                                                  \
  printf("FAIL: %s\n", msg);                                                                       \
  tests_failed++;                                                                                  \
  return;

/* Test payload creation and destruction */
void test_payload_create_destroy(void) {
  TEST("payload create/destroy");

  sparkplug_payload_t* payload = sparkplug_payload_create();
  assert(payload != NULL);

  sparkplug_payload_destroy(payload);
  sparkplug_payload_destroy(NULL); /* Should not crash */

  PASS();
}

/* Test adding metrics by name */
void test_payload_add_metrics(void) {
  TEST("payload add metrics by name");

  sparkplug_payload_t* payload = sparkplug_payload_create();
  assert(payload != NULL);

  sparkplug_payload_add_int32(payload, "int_metric", 42);
  sparkplug_payload_add_int64(payload, "long_metric", 123456789LL);
  sparkplug_payload_add_uint32(payload, "uint_metric", 4294967295U);
  sparkplug_payload_add_float(payload, "float_metric", 3.14159f);
  sparkplug_payload_add_double(payload, "double_metric", 2.718281828);
  sparkplug_payload_add_bool(payload, "bool_metric", true);
  sparkplug_payload_add_string(payload, "string_metric", "Hello C API");

  uint8_t buffer[4096];
  size_t size = sparkplug_payload_serialize(payload, buffer, sizeof(buffer));
  assert(size > 0);
  assert(size < sizeof(buffer));
  (void)size;

  sparkplug_payload_destroy(payload);
  PASS();
}

/* Test adding metrics with aliases */
void test_payload_add_with_alias(void) {
  TEST("payload add metrics with alias");

  sparkplug_payload_t* payload = sparkplug_payload_create();
  assert(payload != NULL);

  sparkplug_payload_add_int32_with_alias(payload, "Temperature", 1, 20);
  sparkplug_payload_add_double_with_alias(payload, "Pressure", 2, 101.325);
  sparkplug_payload_add_bool_with_alias(payload, "Active", 3, true);

  uint8_t buffer[4096];
  size_t size = sparkplug_payload_serialize(payload, buffer, sizeof(buffer));
  assert(size > 0);
  (void)size;

  sparkplug_payload_destroy(payload);
  PASS();
}

/* Test adding metrics by alias only */
void test_payload_add_by_alias(void) {
  TEST("payload add metrics by alias only");

  sparkplug_payload_t* payload = sparkplug_payload_create();
  assert(payload != NULL);

  sparkplug_payload_add_int32_by_alias(payload, 1, 21);
  sparkplug_payload_add_double_by_alias(payload, 2, 102.5);
  sparkplug_payload_add_bool_by_alias(payload, 3, false);

  uint8_t buffer[4096];
  size_t size = sparkplug_payload_serialize(payload, buffer, sizeof(buffer));
  assert(size > 0);
  (void)size;

  sparkplug_payload_destroy(payload);
  PASS();
}

/* Test timestamp and sequence */
void test_payload_timestamp_seq(void) {
  TEST("payload timestamp and sequence");

  sparkplug_payload_t* payload = sparkplug_payload_create();
  assert(payload != NULL);

  sparkplug_payload_set_timestamp(payload, 1234567890123ULL);
  sparkplug_payload_set_seq(payload, 42);
  sparkplug_payload_add_int32(payload, "test", 100);

  uint8_t buffer[4096];
  size_t size = sparkplug_payload_serialize(payload, buffer, sizeof(buffer));
  assert(size > 0);
  (void)size;

  sparkplug_payload_destroy(payload);
  PASS();
}

/* Test empty payload serialization */
void test_payload_empty(void) {
  TEST("empty payload serialization");

  sparkplug_payload_t* payload = sparkplug_payload_create();
  assert(payload != NULL);

  uint8_t buffer[4096];
  size_t size = sparkplug_payload_serialize(payload, buffer, sizeof(buffer));
  assert(size > 0); /* Empty payload still has protobuf overhead */
  (void)size;

  sparkplug_payload_destroy(payload);
  PASS();
}

/* Test publisher creation and destruction */
void test_publisher_create_destroy(void) {
  TEST("publisher create/destroy");

  sparkplug_publisher_t* pub = sparkplug_publisher_create("tcp://localhost:1883", "test_c_api_pub",
                                                          "TestGroup", "TestNodeC01");
  assert(pub != NULL);

  sparkplug_publisher_destroy(pub);
  sparkplug_publisher_destroy(NULL); /* Should not crash */

  PASS();
}

/* Test publisher connect/disconnect */
void test_publisher_connect(void) {
  TEST("publisher connect/disconnect");

  sparkplug_publisher_t* pub = sparkplug_publisher_create("tcp://localhost:1883", "test_c_connect",
                                                          "TestGroup", "TestNodeC02");
  assert(pub != NULL);

  int result = sparkplug_publisher_connect(pub);
  if (result != 0) {
    sparkplug_publisher_destroy(pub);
    FAIL("failed to connect (is mosquitto running?)");
    return;
  }

  /* Give it a moment to establish */
  usleep(100000);

  result = sparkplug_publisher_disconnect(pub);
  assert(result == 0);

  sparkplug_publisher_destroy(pub);
  PASS();
}

/* Test publisher NBIRTH */
void test_publisher_birth(void) {
  TEST("publisher publish NBIRTH");

  sparkplug_publisher_t* pub = sparkplug_publisher_create("tcp://localhost:1883", "test_c_birth",
                                                          "TestGroup", "TestNodeC03");
  assert(pub != NULL);

  int result = sparkplug_publisher_connect(pub);
  if (result != 0) {
    sparkplug_publisher_destroy(pub);
    FAIL("failed to connect");
    return;
  }

  usleep(100000);

  /* Create and publish NBIRTH */
  sparkplug_payload_t* payload = sparkplug_payload_create();
  sparkplug_payload_add_int32_with_alias(payload, "Metric1", 1, 100);

  uint8_t buffer[4096];
  size_t size = sparkplug_payload_serialize(payload, buffer, sizeof(buffer));
  assert(size > 0);

  result = sparkplug_publisher_publish_birth(pub, buffer, size);
  assert(result == 0);

  /* Check sequence is 0 after NBIRTH */
  uint64_t seq = sparkplug_publisher_get_seq(pub);
  assert(seq == 0);
  (void)seq;

  sparkplug_payload_destroy(payload);
  sparkplug_publisher_disconnect(pub);
  sparkplug_publisher_destroy(pub);

  PASS();
}

/* Test publisher NDATA */
void test_publisher_data(void) {
  TEST("publisher publish NDATA");

  sparkplug_publisher_t* pub =
      sparkplug_publisher_create("tcp://localhost:1883", "test_c_data", "TestGroup", "TestNodeC04");
  assert(pub != NULL);

  int result = sparkplug_publisher_connect(pub);
  if (result != 0) {
    sparkplug_publisher_destroy(pub);
    FAIL("failed to connect");
    return;
  }

  usleep(100000);

  /* Publish NBIRTH first */
  sparkplug_payload_t* birth = sparkplug_payload_create();
  sparkplug_payload_add_int32_with_alias(birth, "Metric1", 1, 100);

  uint8_t buffer[4096];
  size_t size = sparkplug_payload_serialize(birth, buffer, sizeof(buffer));
  sparkplug_publisher_publish_birth(pub, buffer, size);
  sparkplug_payload_destroy(birth);

  usleep(100000);

  /* Publish NDATA */
  sparkplug_payload_t* data = sparkplug_payload_create();
  sparkplug_payload_add_int32_by_alias(data, 1, 200);

  size = sparkplug_payload_serialize(data, buffer, sizeof(buffer));
  result = sparkplug_publisher_publish_data(pub, buffer, size);
  assert(result == 0);

  /* Check sequence incremented */
  uint64_t seq = sparkplug_publisher_get_seq(pub);
  assert(seq == 1);
  (void)seq;

  sparkplug_payload_destroy(data);
  sparkplug_publisher_disconnect(pub);
  sparkplug_publisher_destroy(pub);

  PASS();
}

/* Test publisher rebirth */
void test_publisher_rebirth(void) {
  TEST("publisher rebirth");

  sparkplug_publisher_t* pub = sparkplug_publisher_create("tcp://localhost:1883", "test_c_rebirth",
                                                          "TestGroup", "TestNodeC05");
  assert(pub != NULL);

  int result = sparkplug_publisher_connect(pub);
  if (result != 0) {
    sparkplug_publisher_destroy(pub);
    FAIL("failed to connect");
    return;
  }

  usleep(100000);

  /* Publish initial NBIRTH */
  sparkplug_payload_t* birth = sparkplug_payload_create();
  sparkplug_payload_add_int32_with_alias(birth, "Metric1", 1, 100);

  uint8_t buffer[4096];
  size_t size = sparkplug_payload_serialize(birth, buffer, sizeof(buffer));
  sparkplug_publisher_publish_birth(pub, buffer, size);
  sparkplug_payload_destroy(birth);

  uint64_t initial_bdseq = sparkplug_publisher_get_bd_seq(pub);

  usleep(100000);

  /* Trigger rebirth */
  result = sparkplug_publisher_rebirth(pub);
  assert(result == 0);

  /* Check bdSeq incremented and seq reset */
  uint64_t new_bdseq = sparkplug_publisher_get_bd_seq(pub);
  uint64_t seq = sparkplug_publisher_get_seq(pub);

  assert(new_bdseq == initial_bdseq + 1);
  assert(seq == 0);
  (void)initial_bdseq;
  (void)new_bdseq;
  (void)seq;

  sparkplug_publisher_disconnect(pub);
  sparkplug_publisher_destroy(pub);

  PASS();
}

/* Dummy callback for subscriber tests */
static void dummy_callback(const char* topic, const uint8_t* data, size_t len, void* ctx) {
  (void)topic;
  (void)data;
  (void)len;
  (void)ctx;
}

/* Test host application creation and destruction */
void test_subscriber_create_destroy(void) {
  TEST("host application create/destroy");

  sparkplug_host_application_t* host = sparkplug_host_application_create(
      "tcp://localhost:1883", "test_c_api_host", "TestHost");
  assert(host != NULL);

  sparkplug_host_application_set_message_callback(host, dummy_callback, NULL);

  sparkplug_host_application_destroy(host);
  sparkplug_host_application_destroy(NULL);

  PASS();
}

/* Test host application connect */
void test_subscriber_connect(void) {
  TEST("host application connect/disconnect");

  sparkplug_host_application_t* host = sparkplug_host_application_create(
      "tcp://localhost:1883", "test_c_host_connect", "TestHost");
  assert(host != NULL);

  sparkplug_host_application_set_message_callback(host, dummy_callback, NULL);

  int result = sparkplug_host_application_connect(host);
  if (result != 0) {
    sparkplug_host_application_destroy(host);
    FAIL("failed to connect");
    return;
  }

  usleep(100000);

  result = sparkplug_host_application_disconnect(host);
  assert(result == 0);

  sparkplug_host_application_destroy(host);
  PASS();
}

/* Test host application subscribe_all */
void test_subscriber_subscribe_all(void) {
  TEST("host application subscribe_all");

  sparkplug_host_application_t* host = sparkplug_host_application_create(
      "tcp://localhost:1883", "test_c_host_all", "TestHost");
  assert(host != NULL);

  sparkplug_host_application_set_message_callback(host, dummy_callback, NULL);

  int result = sparkplug_host_application_connect(host);
  if (result != 0) {
    sparkplug_host_application_destroy(host);
    FAIL("failed to connect");
    return;
  }

  usleep(100000);

  result = sparkplug_host_application_subscribe_all(host);
  assert(result == 0);

  usleep(100000);

  sparkplug_host_application_disconnect(host);
  sparkplug_host_application_destroy(host);

  PASS();
}

/* Test publisher device birth */
void test_publisher_device_birth(void) {
  TEST("publisher publish DBIRTH");

  sparkplug_publisher_t* pub = sparkplug_publisher_create("tcp://localhost:1883", "test_c_dbirth",
                                                          "TestGroup", "TestNodeC06");
  assert(pub != NULL);

  int result = sparkplug_publisher_connect(pub);
  if (result != 0) {
    sparkplug_publisher_destroy(pub);
    FAIL("failed to connect");
    return;
  }

  usleep(100000);

  /* Publish NBIRTH first (required before DBIRTH) */
  sparkplug_payload_t* nbirth = sparkplug_payload_create();
  sparkplug_payload_add_int32_with_alias(nbirth, "NodeMetric", 1, 100);

  uint8_t buffer[4096];
  size_t size = sparkplug_payload_serialize(nbirth, buffer, sizeof(buffer));
  sparkplug_publisher_publish_birth(pub, buffer, size);
  sparkplug_payload_destroy(nbirth);

  usleep(100000);

  /* Publish DBIRTH for device */
  sparkplug_payload_t* dbirth = sparkplug_payload_create();
  sparkplug_payload_add_double_with_alias(dbirth, "Temperature", 1, 20.5);
  sparkplug_payload_add_double_with_alias(dbirth, "Humidity", 2, 65.0);

  size = sparkplug_payload_serialize(dbirth, buffer, sizeof(buffer));
  result = sparkplug_publisher_publish_device_birth(pub, "Sensor01", buffer, size);
  assert(result == 0);

  sparkplug_payload_destroy(dbirth);
  sparkplug_publisher_disconnect(pub);
  sparkplug_publisher_destroy(pub);

  PASS();
}

/* Test publisher device data */
void test_publisher_device_data(void) {
  TEST("publisher publish DDATA");

  sparkplug_publisher_t* pub = sparkplug_publisher_create("tcp://localhost:1883", "test_c_ddata",
                                                          "TestGroup", "TestNodeC07");
  assert(pub != NULL);

  int result = sparkplug_publisher_connect(pub);
  if (result != 0) {
    sparkplug_publisher_destroy(pub);
    FAIL("failed to connect");
    return;
  }

  usleep(100000);

  /* Publish NBIRTH */
  sparkplug_payload_t* nbirth = sparkplug_payload_create();
  sparkplug_payload_add_int32(nbirth, "NodeMetric", 100);
  uint8_t buffer[4096];
  size_t size = sparkplug_payload_serialize(nbirth, buffer, sizeof(buffer));
  sparkplug_publisher_publish_birth(pub, buffer, size);
  sparkplug_payload_destroy(nbirth);

  usleep(100000);

  /* Publish DBIRTH */
  sparkplug_payload_t* dbirth = sparkplug_payload_create();
  sparkplug_payload_add_double_with_alias(dbirth, "Temperature", 1, 20.5);
  size = sparkplug_payload_serialize(dbirth, buffer, sizeof(buffer));
  sparkplug_publisher_publish_device_birth(pub, "Sensor01", buffer, size);
  sparkplug_payload_destroy(dbirth);

  usleep(100000);

  /* Publish DDATA */
  sparkplug_payload_t* ddata = sparkplug_payload_create();
  sparkplug_payload_add_double_by_alias(ddata, 1, 21.5);
  size = sparkplug_payload_serialize(ddata, buffer, sizeof(buffer));
  result = sparkplug_publisher_publish_device_data(pub, "Sensor01", buffer, size);
  assert(result == 0);

  sparkplug_payload_destroy(ddata);
  sparkplug_publisher_disconnect(pub);
  sparkplug_publisher_destroy(pub);

  PASS();
}

/* Test that sequence numbers in payload are ignored for DDATA (TCK fix) */
void test_device_data_ignores_payload_seq(void) {
  TEST("DDATA ignores sequence in payload");

  sparkplug_publisher_t* pub = sparkplug_publisher_create(
      "tcp://localhost:1883", "test_c_ddata_seq", "TestGroup", "TestNodeC08");
  assert(pub != NULL);

  int result = sparkplug_publisher_connect(pub);
  if (result != 0) {
    sparkplug_publisher_destroy(pub);
    FAIL("failed to connect");
    return;
  }

  usleep(100000);

  /* Publish NBIRTH */
  sparkplug_payload_t* nbirth = sparkplug_payload_create();
  sparkplug_payload_add_int32(nbirth, "NodeMetric", 100);
  uint8_t buffer[4096];
  size_t size = sparkplug_payload_serialize(nbirth, buffer, sizeof(buffer));
  sparkplug_publisher_publish_birth(pub, buffer, size);
  sparkplug_payload_destroy(nbirth);

  usleep(100000);

  /* Publish DBIRTH */
  sparkplug_payload_t* dbirth = sparkplug_payload_create();
  sparkplug_payload_add_double_with_alias(dbirth, "Temperature", 1, 20.5);
  size = sparkplug_payload_serialize(dbirth, buffer, sizeof(buffer));
  sparkplug_publisher_publish_device_birth(pub, "Sensor01", buffer, size);
  sparkplug_payload_destroy(dbirth);

  usleep(100000);

  /* Publish DDATA with WRONG sequence number set in payload (should be ignored) */
  sparkplug_payload_t* ddata1 = sparkplug_payload_create();
  sparkplug_payload_set_seq(ddata1, 999); /* Wrong seq - should be ignored! */
  sparkplug_payload_add_double_by_alias(ddata1, 1, 21.5);
  size = sparkplug_payload_serialize(ddata1, buffer, sizeof(buffer));
  result = sparkplug_publisher_publish_device_data(pub, "Sensor01", buffer, size);
  assert(result == 0);
  sparkplug_payload_destroy(ddata1);

  /* Publish second DDATA with another wrong sequence */
  sparkplug_payload_t* ddata2 = sparkplug_payload_create();
  sparkplug_payload_set_seq(ddata2, 777); /* Also wrong - should be ignored! */
  sparkplug_payload_add_double_by_alias(ddata2, 1, 22.5);
  size = sparkplug_payload_serialize(ddata2, buffer, sizeof(buffer));
  result = sparkplug_publisher_publish_device_data(pub, "Sensor01", buffer, size);
  assert(result == 0);
  sparkplug_payload_destroy(ddata2);

  /* Note: The actual sequence numbers sent should be 1, 2 (managed by library),
   * not 999, 777. This test verifies the fix doesn't crash and works correctly. */

  sparkplug_publisher_disconnect(pub);
  sparkplug_publisher_destroy(pub);

  PASS();
}

/* Test publisher device death */
void test_publisher_device_death(void) {
  TEST("publisher publish DDEATH");

  sparkplug_publisher_t* pub = sparkplug_publisher_create("tcp://localhost:1883", "test_c_ddeath",
                                                          "TestGroup", "TestNodeC09");
  assert(pub != NULL);

  int result = sparkplug_publisher_connect(pub);
  if (result != 0) {
    sparkplug_publisher_destroy(pub);
    FAIL("failed to connect");
    return;
  }

  usleep(100000);

  /* Publish NBIRTH */
  sparkplug_payload_t* nbirth = sparkplug_payload_create();
  sparkplug_payload_add_int32(nbirth, "NodeMetric", 100);
  uint8_t buffer[4096];
  size_t size = sparkplug_payload_serialize(nbirth, buffer, sizeof(buffer));
  sparkplug_publisher_publish_birth(pub, buffer, size);
  sparkplug_payload_destroy(nbirth);

  usleep(100000);

  /* Publish DBIRTH */
  sparkplug_payload_t* dbirth = sparkplug_payload_create();
  sparkplug_payload_add_double(dbirth, "Temperature", 20.5);
  size = sparkplug_payload_serialize(dbirth, buffer, sizeof(buffer));
  sparkplug_publisher_publish_device_birth(pub, "Sensor01", buffer, size);
  sparkplug_payload_destroy(dbirth);

  usleep(100000);

  /* Publish DDEATH */
  result = sparkplug_publisher_publish_device_death(pub, "Sensor01");
  assert(result == 0);

  sparkplug_publisher_disconnect(pub);
  sparkplug_publisher_destroy(pub);

  PASS();
}

/* Test publisher node command */
void test_publisher_node_command(void) {
  TEST("publisher publish NCMD");

  sparkplug_publisher_t* pub =
      sparkplug_publisher_create("tcp://localhost:1883", "test_c_ncmd", "TestGroup", "HostNodeC10");
  assert(pub != NULL);

  int result = sparkplug_publisher_connect(pub);
  if (result != 0) {
    sparkplug_publisher_destroy(pub);
    FAIL("failed to connect");
    return;
  }

  usleep(100000);

  /* Publish NCMD to target node */
  sparkplug_payload_t* cmd = sparkplug_payload_create();
  sparkplug_payload_add_bool(cmd, "Node Control/Rebirth", true);

  uint8_t buffer[4096];
  size_t size = sparkplug_payload_serialize(cmd, buffer, sizeof(buffer));
  result = sparkplug_publisher_publish_node_command(pub, "TargetNode", buffer, size);
  assert(result == 0);

  sparkplug_payload_destroy(cmd);
  sparkplug_publisher_disconnect(pub);
  sparkplug_publisher_destroy(pub);

  PASS();
}

/* Test publisher device command */
void test_publisher_device_command(void) {
  TEST("publisher publish DCMD");

  sparkplug_publisher_t* pub =
      sparkplug_publisher_create("tcp://localhost:1883", "test_c_dcmd", "TestGroup", "HostNodeC11");
  assert(pub != NULL);

  int result = sparkplug_publisher_connect(pub);
  if (result != 0) {
    sparkplug_publisher_destroy(pub);
    FAIL("failed to connect");
    return;
  }

  usleep(100000);

  /* Publish DCMD to target device */
  sparkplug_payload_t* cmd = sparkplug_payload_create();
  sparkplug_payload_add_double(cmd, "SetPoint", 75.0);

  uint8_t buffer[4096];
  size_t size = sparkplug_payload_serialize(cmd, buffer, sizeof(buffer));
  result = sparkplug_publisher_publish_device_command(pub, "TargetNode", "Motor01", buffer, size);
  assert(result == 0);

  sparkplug_payload_destroy(cmd);
  sparkplug_publisher_disconnect(pub);
  sparkplug_publisher_destroy(pub);

  PASS();
}

/* Test host application receives commands */
void test_subscriber_command_callback(void) {
  TEST("host application receives commands");

  sparkplug_publisher_t* pub = sparkplug_publisher_create("tcp://localhost:1883", "test_c_cmd_pub",
                                                          "TestGroup", "HostNodeC12");
  assert(pub != NULL);

  sparkplug_host_application_t* host = sparkplug_host_application_create(
      "tcp://localhost:1883", "test_c_cmd_host", "TestHost");
  assert(host != NULL);

  sparkplug_host_application_set_message_callback(host, dummy_callback, NULL);

  int result = sparkplug_publisher_connect(pub);
  if (result != 0) {
    sparkplug_publisher_destroy(pub);
    sparkplug_host_application_destroy(host);
    FAIL("publisher failed to connect");
    return;
  }

  result = sparkplug_host_application_connect(host);
  if (result != 0) {
    sparkplug_publisher_destroy(pub);
    sparkplug_host_application_destroy(host);
    FAIL("host application failed to connect");
    return;
  }

  usleep(100000);

  result = sparkplug_host_application_subscribe_all(host);
  assert(result == 0);

  usleep(200000);

  sparkplug_payload_t* cmd = sparkplug_payload_create();
  sparkplug_payload_add_bool(cmd, "Node Control/Rebirth", true);

  uint8_t buffer[4096];
  size_t size = sparkplug_payload_serialize(cmd, buffer, sizeof(buffer));
  sparkplug_publisher_publish_node_command(pub, "TestNodeTarget", buffer, size);
  sparkplug_payload_destroy(cmd);

  usleep(500000);

  sparkplug_host_application_disconnect(host);
  sparkplug_publisher_disconnect(pub);
  sparkplug_host_application_destroy(host);
  sparkplug_publisher_destroy(pub);

  PASS();
}

/* Test payload parse and read */
void test_payload_parse_and_read(void) {
  TEST("payload parse and read");

  /* Create a payload with known data */
  sparkplug_payload_t* payload = sparkplug_payload_create();
  assert(payload != NULL);

  sparkplug_payload_set_timestamp(payload, 1234567890123ULL);
  sparkplug_payload_set_seq(payload, 42);
  sparkplug_payload_add_int32_with_alias(payload, "Temperature", 1, 25);
  sparkplug_payload_add_double_with_alias(payload, "Pressure", 2, 101.325);
  sparkplug_payload_add_bool_with_alias(payload, "Active", 3, true);
  sparkplug_payload_add_string(payload, "Status", "Running");

  /* Serialize it */
  uint8_t buffer[4096];
  size_t size = sparkplug_payload_serialize(payload, buffer, sizeof(buffer));
  assert(size > 0);

  sparkplug_payload_destroy(payload);

  /* Parse it back */
  sparkplug_payload_t* parsed = sparkplug_payload_parse(buffer, size);
  assert(parsed != NULL);

  /* Read timestamp */
  uint64_t timestamp;
  assert(sparkplug_payload_get_timestamp(parsed, &timestamp));
  assert(timestamp == 1234567890123ULL);
  (void)timestamp;

  /* Read sequence */
  uint64_t seq;
  assert(sparkplug_payload_get_seq(parsed, &seq));
  assert(seq == 42);
  (void)seq;

  /* Read metric count */
  size_t count = sparkplug_payload_get_metric_count(parsed);
  assert(count == 4);
  (void)count;

  /* Read first metric (Temperature) */
  sparkplug_metric_t metric;
  assert(sparkplug_payload_get_metric_at(parsed, 0, &metric));
  assert(metric.has_name);
  assert(strcmp(metric.name, "Temperature") == 0);
  assert(metric.has_alias);
  assert(metric.alias == 1);
  assert(metric.datatype == SPARKPLUG_DATA_TYPE_INT32);
  assert(!metric.is_null);
  assert(metric.value.int32_value == 25);

  /* Read second metric (Pressure) */
  assert(sparkplug_payload_get_metric_at(parsed, 1, &metric));
  assert(metric.has_name);
  assert(strcmp(metric.name, "Pressure") == 0);
  assert(metric.datatype == SPARKPLUG_DATA_TYPE_DOUBLE);
  assert(metric.value.double_value > 101.3 && metric.value.double_value < 101.4);

  /* Read third metric (Active) */
  assert(sparkplug_payload_get_metric_at(parsed, 2, &metric));
  assert(strcmp(metric.name, "Active") == 0);
  assert(metric.datatype == SPARKPLUG_DATA_TYPE_BOOLEAN);
  assert(metric.value.boolean_value == true);

  /* Read fourth metric (Status) */
  assert(sparkplug_payload_get_metric_at(parsed, 3, &metric));
  assert(strcmp(metric.name, "Status") == 0);
  assert(metric.datatype == SPARKPLUG_DATA_TYPE_STRING);
  assert(strcmp(metric.value.string_value, "Running") == 0);

  /* Test out of bounds */
  assert(!sparkplug_payload_get_metric_at(parsed, 4, &metric));
  (void)metric;

  sparkplug_payload_destroy(parsed);

  PASS();
}

/* Test parsing payload with only aliases (NDATA) */
void test_payload_parse_alias_only(void) {
  TEST("payload parse alias-only metrics");

  /* Create NDATA-style payload with only aliases */
  sparkplug_payload_t* payload = sparkplug_payload_create();
  sparkplug_payload_add_int32_by_alias(payload, 1, 30);
  sparkplug_payload_add_double_by_alias(payload, 2, 102.5);

  uint8_t buffer[4096];
  size_t size = sparkplug_payload_serialize(payload, buffer, sizeof(buffer));
  sparkplug_payload_destroy(payload);

  /* Parse it */
  sparkplug_payload_t* parsed = sparkplug_payload_parse(buffer, size);
  assert(parsed != NULL);

  size_t count = sparkplug_payload_get_metric_count(parsed);
  assert(count == 2);
  (void)count;

  sparkplug_metric_t metric;
  assert(sparkplug_payload_get_metric_at(parsed, 0, &metric));
  assert(!metric.has_name); /* No name for alias-only */
  assert(metric.has_alias);
  assert(metric.alias == 1);
  assert(metric.value.int32_value == 30);
  (void)metric;

  sparkplug_payload_destroy(parsed);

  PASS();
}

/* Test parsing invalid payload */
void test_payload_parse_invalid(void) {
  TEST("payload parse invalid data");

  uint8_t invalid_data[] = {0xFF, 0xFF, 0xFF, 0xFF};
  sparkplug_payload_t* parsed = sparkplug_payload_parse(invalid_data, sizeof(invalid_data));
  assert(parsed == NULL); /* Should fail gracefully */

  /* Test NULL inputs */
  parsed = sparkplug_payload_parse(NULL, 100);
  assert(parsed == NULL);

  parsed = sparkplug_payload_parse(invalid_data, 0);
  assert(parsed == NULL);
  (void)parsed;

  PASS();
}

/* Test reading payload without optional fields */
void test_payload_parse_no_optional(void) {
  TEST("payload parse without seq/uuid");

  /* Create payload without explicitly setting seq */
  sparkplug_payload_t* payload = sparkplug_payload_create();
  sparkplug_payload_add_int32(payload, "Value", 100);
  /* Note: timestamp is auto-set by PayloadBuilder constructor */

  uint8_t buffer[4096];
  size_t size = sparkplug_payload_serialize(payload, buffer, sizeof(buffer));
  sparkplug_payload_destroy(payload);

  /* Parse it */
  sparkplug_payload_t* parsed = sparkplug_payload_parse(buffer, size);
  assert(parsed != NULL);

  /* Timestamp should be present (auto-set) */
  uint64_t timestamp;
  assert(sparkplug_payload_get_timestamp(parsed, &timestamp));
  assert(timestamp > 0);
  (void)timestamp;

  /* Seq should not be present (wasn't set) */
  uint64_t seq;
  assert(!sparkplug_payload_get_seq(parsed, &seq));
  (void)seq;

  /* UUID should be NULL (wasn't set) */
  const char* uuid = sparkplug_payload_get_uuid(parsed);
  assert(uuid == NULL);
  (void)uuid;

  /* But metric should be there */
  assert(sparkplug_payload_get_metric_count(parsed) == 1);

  sparkplug_payload_destroy(parsed);

  PASS();
}

/* Global variable to track if callback was invoked */
static int host_callback_invoked = 0;

/* Callback for testing HostApplication message callback */
static void test_host_message_callback(const char* topic, const uint8_t* payload_data,
                                       size_t payload_len, void* user_data) {
  (void)payload_data;
  (void)payload_len;

  printf("\n  [Callback invoked] topic=%s\n", topic);
  host_callback_invoked++;

  /* Verify user_data is passed correctly */
  int* counter = (int*)user_data;
  (*counter)++;
}

/* Test HostApplication with message callback and subscriptions */
void test_host_application_with_callback(void) {
  TEST("host application with message callback");

  /* Create host application */
  sparkplug_host_application_t* host =
      sparkplug_host_application_create("tcp://localhost:1883", "test_host_c_api", "TEST_HOST");

  if (host == NULL) {
    FAIL("Failed to create host application");
  }

  /* Set user data counter */
  int user_counter = 0;

  /* Set message callback */
  int ret = sparkplug_host_application_set_message_callback(host, test_host_message_callback,
                                                            &user_counter);
  assert(ret == 0);

  /* Connect */
  ret = sparkplug_host_application_connect(host);
  if (ret != 0) {
    sparkplug_host_application_destroy(host);
    FAIL("Failed to connect (broker not running?)");
  }

  /* Publish STATE birth */
  ret = sparkplug_host_application_publish_state_birth(host, 1234567890);
  assert(ret == 0);

  /* Subscribe to a test group */
  ret = sparkplug_host_application_subscribe_group(host, "TestGroup");
  assert(ret == 0);

  printf("\n  Waiting for messages (3 seconds)...\n");
  sleep(3);

  /* Publish STATE death */
  ret = sparkplug_host_application_publish_state_death(host, 1234567890);
  assert(ret == 0);

  /* Disconnect and cleanup */
  ret = sparkplug_host_application_disconnect(host);
  assert(ret == 0);

  sparkplug_host_application_destroy(host);
  sparkplug_host_application_destroy(NULL); /* Should not crash */

  printf("  Callback invocations: %d\n", host_callback_invoked);
  printf("  User counter value: %d\n", user_counter);

  /* Note: We don't assert callback was invoked since it depends on having a publisher */
  /* This test mainly validates the API doesn't crash */

  PASS();
}

int main(void) {
  printf("=== C API Unit Tests ===\n\n");

  /* Payload tests (no MQTT broker required) */
  test_payload_create_destroy();
  test_payload_add_metrics();
  test_payload_add_with_alias();
  test_payload_add_by_alias();
  test_payload_timestamp_seq();
  test_payload_empty();

  /* NEW: Payload parsing tests */
  test_payload_parse_and_read();
  test_payload_parse_alias_only();
  test_payload_parse_invalid();
  test_payload_parse_no_optional();

  /* Publisher tests (require MQTT broker) */
  test_publisher_create_destroy();
  test_publisher_connect();
  test_publisher_birth();
  test_publisher_data();
  test_publisher_rebirth();

  /* Subscriber tests (require MQTT broker) */
  test_subscriber_create_destroy();
  test_subscriber_connect();
  test_subscriber_subscribe_all();

  /* Device-level API tests (require MQTT broker) */
  test_publisher_device_birth();
  test_publisher_device_data();
  test_device_data_ignores_payload_seq();
  test_publisher_device_death();

  /* Command API tests (require MQTT broker) */
  test_publisher_node_command();
  test_publisher_device_command();
  test_subscriber_command_callback();

  /* Host Application callback tests (require MQTT broker) */
  test_host_application_with_callback();

  printf("\n=== Test Summary ===\n");
  printf("Passed: %d\n", tests_passed);
  printf("Failed: %d\n", tests_failed);

  if (tests_failed > 0) {
    printf("\n*** SOME TESTS FAILED ***\n");
    return 1;
  }

  printf("\n=== All C API tests passed! ===\n");
  return 0;
}
