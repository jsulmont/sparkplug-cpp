// tests/test_payload_builder.cpp
// Unit tests for PayloadBuilder type safety and functionality
#include <cassert>
#include <cmath>
#include <iostream>

#include <sparkplug/payload_builder.hpp>

void test_int_types() {
  sparkplug::PayloadBuilder payload;

  payload.add_metric("int8", static_cast<int8_t>(-128));
  payload.add_metric("int16", static_cast<int16_t>(-32768));
  payload.add_metric("int32", static_cast<int32_t>(-2147483648));
  payload.add_metric("int64", static_cast<int64_t>(-9223372036854775807LL));

  payload.add_metric("uint8", static_cast<uint8_t>(255));
  payload.add_metric("uint16", static_cast<uint16_t>(65535));
  payload.add_metric("uint32", static_cast<uint32_t>(4294967295));
  payload.add_metric("uint64", static_cast<uint64_t>(18446744073709551615ULL));

  auto pb = payload.payload();
  assert(pb.metrics_size() == 8);

  std::cout << "✓ Integer types (int8/16/32/64, uint8/16/32/64)\n";
}

void test_float_types() {
  sparkplug::PayloadBuilder payload;

  payload.add_metric("float", 3.14159f);
  payload.add_metric("double", 2.718281828459045);
  payload.add_metric("float_neg", -123.456f);
  payload.add_metric("double_neg", -987.654321);

  auto pb = payload.payload();
  assert(pb.metrics_size() == 4);

  // Check float precision
  assert(std::abs(pb.metrics(0).float_value() - 3.14159f) < 0.0001f);
  assert(std::abs(pb.metrics(1).double_value() - 2.718281828459045) < 0.000001);

  std::cout << "✓ Floating-point types (float, double)\n";
}

void test_bool_type() {
  sparkplug::PayloadBuilder payload;

  payload.add_metric("bool_true", true);
  payload.add_metric("bool_false", false);

  auto pb = payload.payload();
  assert(pb.metrics_size() == 2);
  assert(pb.metrics(0).boolean_value() == true);
  assert(pb.metrics(1).boolean_value() == false);

  std::cout << "✓ Boolean type\n";
}

void test_string_type() {
  sparkplug::PayloadBuilder payload;

  payload.add_metric("string_literal", "Hello, World!");
  payload.add_metric("string_obj", std::string("C++ String"));
  payload.add_metric("empty_string", "");

  auto pb = payload.payload();
  assert(pb.metrics_size() == 3);
  assert(pb.metrics(0).string_value() == "Hello, World!");
  assert(pb.metrics(1).string_value() == "C++ String");
  assert(pb.metrics(2).string_value().empty());

  std::cout << "✓ String types (literal, std::string, empty)\n";
}

void test_metric_with_alias() {
  sparkplug::PayloadBuilder payload;

  payload.add_metric_with_alias("Temperature", 1, 20.5);
  payload.add_metric_with_alias("Pressure", 2, 101325);
  payload.add_metric_with_alias("Humidity", 3, 65.0f);

  auto pb = payload.payload();
  assert(pb.metrics_size() == 3);

  // Check aliases are set
  assert(pb.metrics(0).has_alias());
  assert(pb.metrics(0).alias() == 1);
  assert(pb.metrics(0).name() == "Temperature");

  assert(pb.metrics(1).has_alias());
  assert(pb.metrics(1).alias() == 2);
  assert(pb.metrics(1).name() == "Pressure");

  assert(pb.metrics(2).has_alias());
  assert(pb.metrics(2).alias() == 3);
  assert(pb.metrics(2).name() == "Humidity");

  std::cout << "✓ Metrics with aliases (name + alias)\n";
}

void test_metric_by_alias_only() {
  sparkplug::PayloadBuilder payload;

  payload.add_metric_by_alias(1, 21.0);
  payload.add_metric_by_alias(2, 101326);
  payload.add_metric_by_alias(3, 66.0f);

  auto pb = payload.payload();
  assert(pb.metrics_size() == 3);

  // Check no names, only aliases
  assert(pb.metrics(0).name().empty());
  assert(pb.metrics(0).has_alias());
  assert(pb.metrics(0).alias() == 1);

  assert(pb.metrics(1).name().empty());
  assert(pb.metrics(1).has_alias());
  assert(pb.metrics(1).alias() == 2);

  assert(pb.metrics(2).name().empty());
  assert(pb.metrics(2).has_alias());
  assert(pb.metrics(2).alias() == 3);

  std::cout << "✓ Metrics by alias only (NDATA pattern)\n";
}

void test_custom_timestamp() {
  sparkplug::PayloadBuilder payload;

  uint64_t custom_ts = 1234567890123ULL;
  payload.add_metric("test", 42, custom_ts);

  auto pb = payload.payload();
  assert(pb.metrics_size() == 1);
  assert(pb.metrics(0).timestamp() == custom_ts);

  std::cout << "✓ Custom metric timestamp\n";
}

void test_auto_timestamp() {
  sparkplug::PayloadBuilder payload;

  [[maybe_unused]] auto before = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count();

  payload.add_metric("test", 42);

  [[maybe_unused]] auto after = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();

  auto pb = payload.payload();
  assert(pb.metrics_size() == 1);

  [[maybe_unused]] uint64_t ts = pb.metrics(0).timestamp();
  assert(ts >= static_cast<uint64_t>(before));
  assert(ts <= static_cast<uint64_t>(after));

  std::cout << "✓ Auto-generated timestamp\n";
}

void test_payload_timestamp() {
  sparkplug::PayloadBuilder payload;

  uint64_t custom_ts = 9876543210987ULL;
  payload.set_timestamp(custom_ts);
  payload.add_metric("test", 42);

  auto pb = payload.payload();
  assert(pb.has_timestamp());
  assert(pb.timestamp() == custom_ts);

  std::cout << "✓ Payload-level timestamp\n";
}

void test_payload_sequence() {
  sparkplug::PayloadBuilder payload;

  payload.set_seq(123);
  payload.add_metric("test", 42);

  auto pb = payload.payload();
  assert(pb.has_seq());
  assert(pb.seq() == 123);

  std::cout << "✓ Payload sequence number\n";
}

void test_empty_payload() {
  sparkplug::PayloadBuilder payload;

  [[maybe_unused]] const auto& pb = payload.payload();
  assert(pb.metrics_size() == 0);

  std::cout << "✓ Empty payload\n";
}

void test_multiple_metrics() {
  sparkplug::PayloadBuilder payload;

  payload.add_metric("metric1", 100);
  payload.add_metric("metric2", 200.5);
  payload.add_metric("metric3", true);
  payload.add_metric("metric4", "test");

  auto pb = payload.payload();
  assert(pb.metrics_size() == 4);

  std::cout << "✓ Multiple metrics of different types\n";
}

void test_method_chaining() {
  sparkplug::PayloadBuilder payload;

  payload.add_metric("m1", 1).add_metric("m2", 2).add_metric("m3", 3).set_timestamp(123456).set_seq(
      42);

  auto pb = payload.payload();
  assert(pb.metrics_size() == 3);
  assert(pb.timestamp() == 123456);
  assert(pb.seq() == 42);

  std::cout << "✓ Method chaining (fluent API)\n";
}

void test_node_control_metrics() {
  sparkplug::PayloadBuilder payload;

  payload.add_node_control_rebirth(false);
  payload.add_node_control_next_server(false);
  payload.add_node_control_scan_rate(1000);

  auto pb = payload.payload();
  assert(pb.metrics_size() == 3);

  // Check metric names follow Node Control pattern
  [[maybe_unused]] bool has_rebirth = false;
  [[maybe_unused]] bool has_next_server = false;
  [[maybe_unused]] bool has_scan_rate = false;

  for (int i = 0; i < pb.metrics_size(); i++) {
    if (pb.metrics(i).name() == "Node Control/Rebirth")
      has_rebirth = true;
    if (pb.metrics(i).name() == "Node Control/Next Server")
      has_next_server = true;
    if (pb.metrics(i).name() == "Node Control/Scan Rate")
      has_scan_rate = true;
  }

  assert(has_rebirth);
  assert(has_next_server);
  assert(has_scan_rate);

  std::cout << "✓ Node Control metrics\n";
}

void test_serialize() {
  sparkplug::PayloadBuilder payload;

  payload.add_metric("test", 42);

  auto data = payload.build();
  assert(!data.empty());

  std::cout << "✓ Payload serialization\n";
}

int main() {
  std::cout << "=== PayloadBuilder Unit Tests ===\n\n";

  test_int_types();
  test_float_types();
  test_bool_type();
  test_string_type();
  test_metric_with_alias();
  test_metric_by_alias_only();
  test_custom_timestamp();
  test_auto_timestamp();
  test_payload_timestamp();
  test_payload_sequence();
  test_empty_payload();
  test_multiple_metrics();
  test_method_chaining();
  test_node_control_metrics();
  test_serialize();

  std::cout << "\n=== All PayloadBuilder tests passed! ===\n";
  return 0;
}
