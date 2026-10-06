// tests/test_topic.cpp
#include <cassert>
#include <iostream>

#include <sparkplug/topic.hpp>

void test_topic_to_string() {
  sparkplug::Topic topic{.group_id = "Energy",
                         .message_type = sparkplug::MessageType::NBIRTH,
                         .edge_node_id = "Gateway01",
                         .device_id = ""};

  auto topic_str = topic.to_string();
  assert(topic_str == "spBv1.0/Energy/NBIRTH/Gateway01");
  std::cout << "[OK] Topic to string\n";
}

void test_topic_with_device() {
  sparkplug::Topic topic{.group_id = "Energy",
                         .message_type = sparkplug::MessageType::DBIRTH,
                         .edge_node_id = "Gateway01",
                         .device_id = "Sensor01"};

  auto topic_str = topic.to_string();
  assert(topic_str == "spBv1.0/Energy/DBIRTH/Gateway01/Sensor01");
  std::cout << "[OK] Topic with device\n";
}

void test_state_topic() {
  sparkplug::Topic topic{.group_id = "",
                         .message_type = sparkplug::MessageType::STATE,
                         .edge_node_id = "scada_host",
                         .device_id = ""};

  auto topic_str = topic.to_string();
  assert(topic_str == "spBv1.0/STATE/scada_host");
  std::cout << "[OK] STATE topic\n";
}

void test_parse_topic() {
  auto result = sparkplug::Topic::parse("spBv1.0/Energy/NDATA/Gateway01");
  assert(result.has_value());

  [[maybe_unused]] auto& topic = *result;
  assert(topic.group_id == "Energy");
  assert(topic.message_type == sparkplug::MessageType::NDATA);
  assert(topic.edge_node_id == "Gateway01");
  assert(topic.device_id.empty());
  std::cout << "[OK] Parse topic\n";
}

void test_parse_device_topic() {
  auto result = sparkplug::Topic::parse("spBv1.0/Energy/DDATA/Gateway01/Sensor01");
  assert(result.has_value());

  [[maybe_unused]] auto& topic = *result;
  assert(topic.group_id == "Energy");
  assert(topic.message_type == sparkplug::MessageType::DDATA);
  assert(topic.edge_node_id == "Gateway01");
  assert(topic.device_id == "Sensor01");
  std::cout << "[OK] Parse device topic\n";
}

void test_parse_state_topic() {
  auto result = sparkplug::Topic::parse("spBv1.0/STATE/scada_host");
  assert(result.has_value());

  [[maybe_unused]] auto& topic = *result;
  assert(topic.message_type == sparkplug::MessageType::STATE);
  assert(topic.edge_node_id == "scada_host");
  std::cout << "[OK] Parse STATE topic\n";
}

void test_parse_rejects_empty_group_id() {
  auto result = sparkplug::Topic::parse("spBv1.0//NBIRTH/Gateway01");
  assert(!result.has_value());
  std::cout << "[OK] Parse rejects empty group_id\n";
}

void test_parse_rejects_empty_edge_node_id() {
  auto result = sparkplug::Topic::parse("spBv1.0/Energy/NDATA/");
  assert(!result.has_value());
  std::cout << "[OK] Parse rejects empty edge_node_id\n";
}

void test_parse_rejects_trailing_component_on_node_message() {
  auto result = sparkplug::Topic::parse("spBv1.0/Energy/NDATA/Gateway01/extra");
  assert(!result.has_value());
  std::cout << "[OK] Parse rejects trailing component on node message\n";
}

void test_parse_rejects_trailing_component_on_device_message() {
  auto result = sparkplug::Topic::parse("spBv1.0/Energy/DDATA/Gateway01/Sensor01/extra");
  assert(!result.has_value());
  std::cout << "[OK] Parse rejects trailing component on device message\n";
}

void test_parse_rejects_empty_device_id_on_device_message() {
  auto result = sparkplug::Topic::parse("spBv1.0/Energy/DBIRTH/Gateway01/");
  assert(!result.has_value());
  std::cout << "[OK] Parse rejects empty device_id on device message\n";
}

void test_parse_rejects_oversized_component() {
  std::string long_id(sparkplug::kMaxComponentLength + 1, 'a');
  auto result = sparkplug::Topic::parse("spBv1.0/" + long_id + "/NDATA/Gateway01");
  assert(!result.has_value());
  result = sparkplug::Topic::parse("spBv1.0/Energy/NDATA/" + long_id);
  assert(!result.has_value());
  result = sparkplug::Topic::parse("spBv1.0/Energy/DBIRTH/Gateway01/" + long_id);
  assert(!result.has_value());
  std::cout << "[OK] Parse rejects oversized components\n";
}

void test_parse_rejects_invalid_state_topic() {
  auto result = sparkplug::Topic::parse("spBv1.0/STATE/host/extra");
  assert(!result.has_value());
  result = sparkplug::Topic::parse("spBv1.0/STATE/");
  assert(!result.has_value());
  std::cout << "[OK] Parse rejects invalid STATE topics\n";
}

int main() {
  test_topic_to_string();
  test_topic_with_device();
  test_state_topic();
  test_parse_topic();
  test_parse_device_topic();
  test_parse_state_topic();
  test_parse_rejects_empty_group_id();
  test_parse_rejects_empty_edge_node_id();
  test_parse_rejects_trailing_component_on_node_message();
  test_parse_rejects_trailing_component_on_device_message();
  test_parse_rejects_empty_device_id_on_device_message();
  test_parse_rejects_oversized_component();
  test_parse_rejects_invalid_state_topic();

  std::cout << "\nAll tests passed!\n";
  return 0;
}