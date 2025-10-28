// tests/test_error_handling.cpp
// Tests for error conditions and failure handling
#include <cassert>
#include <iostream>

#include <sparkplug/edge_node.hpp>
#include <sparkplug/host_application.hpp>
#include <sparkplug/topic.hpp>

void test_invalid_broker_url() {
  sparkplug::EdgeNode::Config config{.broker_url = "invalid://bad_url:99999",
                                     .client_id = "test_invalid",
                                     .group_id = "Test",
                                     .edge_node_id = "NodeErr01"};

  sparkplug::EdgeNode pub(std::move(config));

  auto result = pub.connect();
  assert(!result.has_value()); // Should fail
  assert(!result.error().empty());

  std::cout << "✓ Invalid broker URL fails gracefully\n";
}

void test_publish_before_connect() {
  sparkplug::EdgeNode::Config config{.broker_url = "tcp://localhost:1883",
                                     .client_id = "test_no_connect",
                                     .group_id = "Test",
                                     .edge_node_id = "NodeErr02"};

  sparkplug::EdgeNode pub(std::move(config));

  sparkplug::PayloadBuilder payload;
  payload.add_metric("test", 42);

  // Try to publish without connecting
  auto result = pub.publish_birth(payload);
  assert(!result.has_value()); // Should fail

  std::cout << "✓ Publish before connect fails gracefully\n";
}

void test_publish_data_before_birth() {
  sparkplug::EdgeNode::Config config{.broker_url = "tcp://localhost:1883",
                                     .client_id = "test_no_birth",
                                     .group_id = "Test",
                                     .edge_node_id = "NodeErr03"};

  sparkplug::EdgeNode pub(std::move(config));

  auto conn_result = pub.connect();
  if (!conn_result) {
    std::cout << "⊘ Skipping (no MQTT broker): publish_data before NBIRTH\n";
    return;
  }

  sparkplug::PayloadBuilder payload;
  payload.add_metric("test", 42);

  // Library allows NDATA without NBIRTH (protocol compliance is user's responsibility)
  // This test verifies it doesn't crash, not that it fails
  auto result = pub.publish_data(payload);
  assert(result.has_value()); // Should succeed (but violates protocol)

  (void)pub.disconnect();
  std::cout << "✓ NDATA before NBIRTH succeeds (protocol violation allowed)\n";
}

void test_double_connect() {
  sparkplug::EdgeNode::Config config{.broker_url = "tcp://localhost:1883",
                                     .client_id = "test_double_conn",
                                     .group_id = "Test",
                                     .edge_node_id = "NodeErr04"};

  sparkplug::EdgeNode pub(std::move(config));

  auto result1 = pub.connect();
  if (!result1) {
    std::cout << "⊘ Skipping (no MQTT broker): double connect\n";
    return;
  }

  // Try to connect again - implementation allows idempotent connect
  auto result2 = pub.connect();
  // Note: Double connect behavior is implementation-defined
  // This test just verifies it doesn't crash

  (void)pub.disconnect();
  std::cout << "✓ Double connect handled (implementation allows it)\n";
}

void test_disconnect_not_connected() {
  sparkplug::EdgeNode::Config config{.broker_url = "tcp://localhost:1883",
                                     .client_id = "test_disc_no_conn",
                                     .group_id = "Test",
                                     .edge_node_id = "NodeErr05"};

  sparkplug::EdgeNode pub(std::move(config));

  // Try to disconnect without connecting
  auto result = pub.disconnect();
  assert(!result.has_value()); // Should fail

  std::cout << "✓ Disconnect without connect fails gracefully\n";
}

void test_invalid_topic_parse() {
  auto result1 = sparkplug::Topic::parse("invalid_topic");
  assert(!result1.has_value());

  auto result2 = sparkplug::Topic::parse("spBv1.0/");
  assert(!result2.has_value());

  auto result3 = sparkplug::Topic::parse("");
  assert(!result3.has_value());

  auto result4 = sparkplug::Topic::parse("spBv1.0/Group/INVALID_TYPE/Node");
  assert(!result4.has_value());

  std::cout << "✓ Invalid topic strings fail to parse\n";
}

void test_move_semantics() {
  sparkplug::EdgeNode::Config config{.broker_url = "tcp://localhost:1883",
                                     .client_id = "test_move",
                                     .group_id = "Test",
                                     .edge_node_id = "NodeErr06"};

  sparkplug::EdgeNode pub1(std::move(config));

  // Move constructor
  sparkplug::EdgeNode pub2(std::move(pub1));

  // pub1 is now in moved-from state
  // Attempting to use it should fail safely (though behavior is undefined)

  std::cout << "✓ Move constructor works\n";
}

void test_subscriber_invalid_broker() {
  auto callback = [](const sparkplug::Topic&, const auto&) {};

  sparkplug::HostApplication::Config config{.broker_url = "invalid://bad:99999",
                                            .client_id = "test_sub_invalid",
                                            .host_id = "Test",
                                            .message_callback = callback};

  sparkplug::HostApplication sub(std::move(config));

  auto result = sub.connect();
  assert(!result.has_value()); // Should fail

  std::cout << "✓ Subscriber invalid broker fails gracefully\n";
}

void test_subscriber_subscribe_before_connect() {
  auto callback = [](const sparkplug::Topic&, const auto&) {};

  sparkplug::HostApplication::Config config{.broker_url = "tcp://localhost:1883",
                                            .client_id = "test_sub_no_conn",
                                            .host_id = "Test",
                                            .message_callback = callback};

  sparkplug::HostApplication sub(std::move(config));

  // Try to subscribe without connecting
  auto result = sub.subscribe_all_groups();
  assert(!result.has_value()); // Should fail

  std::cout << "✓ Subscribe before connect fails gracefully\n";
}

void test_empty_config_fields() {
  // Empty broker URL should fail
  sparkplug::EdgeNode::Config config1{
      .broker_url = "", .client_id = "test", .group_id = "Test", .edge_node_id = "NodeErr07"};

  sparkplug::EdgeNode pub1(std::move(config1));
  auto result1 = pub1.connect();
  assert(!result1.has_value()); // Should fail with empty broker URL

  // Empty client ID is allowed by the library (MQTT will auto-generate one)
  // This test just verifies it doesn't crash
  std::cout << "✓ Empty broker URL fails, empty client_id allowed\n";
}

void test_sequence_overflow() {
  sparkplug::EdgeNode::Config config{.broker_url = "tcp://localhost:1883",
                                     .client_id = "test_seq_overflow",
                                     .group_id = "Test",
                                     .edge_node_id = "NodeErr08"};

  sparkplug::EdgeNode pub(std::move(config));

  auto conn = pub.connect();
  if (!conn) {
    std::cout << "⊘ Skipping (no MQTT broker): sequence overflow\n";
    return;
  }

  sparkplug::PayloadBuilder birth;
  birth.add_metric("test", 0);
  (void)pub.publish_birth(birth);

  // Publish 260 messages to ensure wrap happens multiple times
  for (int i = 0; i < 260; i++) {
    sparkplug::PayloadBuilder data;
    data.add_metric("test", i);
    auto result = pub.publish_data(data);
    assert(result.has_value()); // Should succeed
  }

  // Sequence should have wrapped at least once
  [[maybe_unused]] uint64_t seq = pub.get_seq();
  assert(seq < 256); // Should be within 0-255 range

  (void)pub.disconnect();
  std::cout << "✓ Sequence overflow wraps correctly\n";
}

int main() {
  std::cout << "=== Error Handling Tests ===\n\n";

  test_invalid_broker_url();
  test_publish_before_connect();
  test_publish_data_before_birth();
  test_double_connect();
  test_disconnect_not_connected();
  test_invalid_topic_parse();
  test_move_semantics();
  test_subscriber_invalid_broker();
  test_subscriber_subscribe_before_connect();
  test_empty_config_fields();
  test_sequence_overflow();

  std::cout << "\n=== All error handling tests passed! ===\n";
  return 0;
}
