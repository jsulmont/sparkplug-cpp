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
  std::cout << "✓ Topic to string\n";
}

void test_topic_with_device() {
  sparkplug::Topic topic{.group_id = "Energy",
                         .message_type = sparkplug::MessageType::DBIRTH,
                         .edge_node_id = "Gateway01",
                         .device_id = "Sensor01"};

  auto topic_str = topic.to_string();
  assert(topic_str == "spBv1.0/Energy/DBIRTH/Gateway01/Sensor01");
  std::cout << "✓ Topic with device\n";
}

void test_state_topic() {
  sparkplug::Topic topic{.group_id = "",
                         .message_type = sparkplug::MessageType::STATE,
                         .edge_node_id = "scada_host",
                         .device_id = ""};

  auto topic_str = topic.to_string();
  assert(topic_str == "spBv1.0/STATE/scada_host");
  std::cout << "✓ STATE topic\n";
}

void test_parse_topic() {
  auto result = sparkplug::Topic::parse("spBv1.0/Energy/NDATA/Gateway01");
  assert(result.has_value());

  [[maybe_unused]] auto& topic = *result;
  assert(topic.group_id == "Energy");
  assert(topic.message_type == sparkplug::MessageType::NDATA);
  assert(topic.edge_node_id == "Gateway01");
  assert(topic.device_id.empty());
  std::cout << "✓ Parse topic\n";
}

void test_parse_device_topic() {
  auto result = sparkplug::Topic::parse("spBv1.0/Energy/DDATA/Gateway01/Sensor01");
  assert(result.has_value());

  [[maybe_unused]] auto& topic = *result;
  assert(topic.group_id == "Energy");
  assert(topic.message_type == sparkplug::MessageType::DDATA);
  assert(topic.edge_node_id == "Gateway01");
  assert(topic.device_id == "Sensor01");
  std::cout << "✓ Parse device topic\n";
}

void test_parse_state_topic() {
  auto result = sparkplug::Topic::parse("spBv1.0/STATE/scada_host");
  assert(result.has_value());

  [[maybe_unused]] auto& topic = *result;
  assert(topic.message_type == sparkplug::MessageType::STATE);
  assert(topic.edge_node_id == "scada_host");
  std::cout << "✓ Parse STATE topic\n";
}

int main() {
  test_topic_to_string();
  test_topic_with_device();
  test_state_topic();
  test_parse_topic();
  test_parse_device_topic();
  test_parse_state_topic();

  std::cout << "\nAll tests passed!\n";
  return 0;
}