// tests/test_c_bindings_timestamps.cpp
// Verify that C bindings preserve per-metric timestamps through parse/serialize
// round-trip.
#include <cassert>
#include <cstring>
#include <iostream>

#include <sparkplug/sparkplug_c.h>

void test_metric_timestamps_preserved_through_round_trip() {
  // Create a payload with explicit timestamps
  sparkplug_payload_t* original = sparkplug_payload_create();
  assert(original);

  uint64_t ts1 = 1000000000001ULL;
  uint64_t ts2 = 1000000000002ULL;
  uint64_t ts3 = 1000000000003ULL;

  sparkplug_payload_add_double_with_alias_timestamp(original, "Temperature", 1, 20.5,
                                                    ts1);
  sparkplug_payload_add_int32_with_alias_timestamp(original, "Pressure", 2, 101325, ts2);
  sparkplug_payload_add_bool_with_alias_timestamp(original, "Active", 3, true, ts3);

  // Serialize
  uint8_t buffer[4096];
  size_t size = sparkplug_payload_serialize(original, buffer, sizeof(buffer));
  assert(size > 0);

  // Parse back
  sparkplug_payload_t* parsed = sparkplug_payload_parse(buffer, size);
  assert(parsed);

  // Verify metric count
  assert(sparkplug_payload_get_metric_count(parsed) == 3);

  // Verify timestamps are preserved
  sparkplug_metric_t metric;

  assert(sparkplug_payload_get_metric_at(parsed, 0, &metric));
  assert(metric.has_timestamp);
  assert(metric.timestamp == ts1);
  assert(metric.datatype == SPARKPLUG_DATA_TYPE_DOUBLE);

  assert(sparkplug_payload_get_metric_at(parsed, 1, &metric));
  assert(metric.has_timestamp);
  assert(metric.timestamp == ts2);
  assert(metric.datatype == SPARKPLUG_DATA_TYPE_INT32);

  assert(sparkplug_payload_get_metric_at(parsed, 2, &metric));
  assert(metric.has_timestamp);
  assert(metric.timestamp == ts3);
  assert(metric.datatype == SPARKPLUG_DATA_TYPE_BOOLEAN);

  sparkplug_payload_destroy(parsed);
  sparkplug_payload_destroy(original);

  std::cout << "[OK] Metric timestamps preserved through parse/serialize round-trip\n";
}

void test_metric_timestamps_without_explicit_timestamp() {
  // Metrics added without timestamps should get auto-generated ones,
  // and those should survive a round-trip too.
  sparkplug_payload_t* original = sparkplug_payload_create();
  assert(original);

  sparkplug_payload_add_double_with_alias(original, "Temperature", 1, 20.5);

  uint8_t buffer[4096];
  size_t size = sparkplug_payload_serialize(original, buffer, sizeof(buffer));
  assert(size > 0);

  sparkplug_payload_t* parsed = sparkplug_payload_parse(buffer, size);
  assert(parsed);

  sparkplug_metric_t metric;
  assert(sparkplug_payload_get_metric_at(parsed, 0, &metric));
  assert(metric.has_timestamp);
  // Auto-generated timestamp should be a reasonable epoch-ms value (> year 2020)
  assert(metric.timestamp > 1577836800000ULL);

  sparkplug_payload_destroy(parsed);
  sparkplug_payload_destroy(original);

  std::cout << "[OK] Auto-generated metric timestamps survive round-trip\n";
}

int main() {
  std::cout << "=== C Bindings Timestamp Tests ===\n\n";

  test_metric_timestamps_preserved_through_round_trip();
  test_metric_timestamps_without_explicit_timestamp();

  std::cout << "\n=== All C bindings timestamp tests passed! ===\n";
  return 0;
}
