// tests/test_state_json_parsing.cpp
// Verify EdgeNode parses STATE JSON correctly with whitespace variations.
// The old code used exact string matching ("online":true) which broke
// with any whitespace around the colon or value.

#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <thread>

#include <MQTTAsync.h>
#include <sparkplug/edge_node.hpp>

static constexpr auto SETTLE = std::chrono::milliseconds(500);

static void publish_raw(const std::string& broker,
                        const std::string& topic,
                        const std::string& payload) {
  MQTTAsync client = nullptr;
  MQTTAsync_create(&client, broker.c_str(), "test_json_raw_pub",
                   MQTTCLIENT_PERSISTENCE_NONE, nullptr);

  MQTTAsync_connectOptions conn = MQTTAsync_connectOptions_initializer;
  conn.cleansession = 1;

  std::promise<void> p;
  auto f = p.get_future();
  conn.context = &p;
  conn.onSuccess = [](void* ctx, MQTTAsync_successData*) {
    static_cast<std::promise<void>*>(ctx)->set_value();
  };
  conn.onFailure = [](void* ctx, MQTTAsync_failureData*) {
    static_cast<std::promise<void>*>(ctx)->set_exception(
        std::make_exception_ptr(std::runtime_error("connect failed")));
  };

  MQTTAsync_connect(client, &conn);
  f.get();

  MQTTAsync_message msg = MQTTAsync_message_initializer;
  msg.payload = const_cast<char*>(payload.data());
  msg.payloadlen = static_cast<int>(payload.size());
  msg.qos = 1;
  msg.retained = 1;

  std::promise<void> sp;
  auto sf = sp.get_future();
  MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
  opts.context = &sp;
  opts.onSuccess = [](void* ctx, MQTTAsync_successData*) {
    static_cast<std::promise<void>*>(ctx)->set_value();
  };
  opts.onFailure = [](void* ctx, MQTTAsync_failureData*) {
    static_cast<std::promise<void>*>(ctx)->set_exception(
        std::make_exception_ptr(std::runtime_error("publish failed")));
  };

  MQTTAsync_sendMessage(client, topic.c_str(), &msg, &opts);
  sf.get();

  MQTTAsync_disconnect(client, nullptr);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  MQTTAsync_destroy(&client);
}

void test_whitespace_variations() {
  const std::string broker = "tcp://localhost:1883";
  const std::string host_id = "JsonTestHost";
  const std::string state_topic = "spBv1.0/STATE/" + host_id;

  sparkplug::EdgeNode::Config config{.broker_url = broker,
                                     .client_id = "test_json_edge",
                                     .group_id = "JsonGroup",
                                     .edge_node_id = "JsonNode01",
                                     .primary_host_id = host_id};
  sparkplug::EdgeNode node(std::move(config));

  if (!node.connect()) {
    std::cout << "[SKIP] STATE JSON parsing (no broker)\n";
    return;
  }
  std::this_thread::sleep_for(SETTLE);

  assert(!node.is_primary_host_online());

  struct TestCase {
    std::string json;
    bool expected;
  };

  std::vector<TestCase> cases = {
      // compact (old code handled this)
      {R"({"online":true})", true},
      {R"({"online":false})", false},
      // space after colon
      {R"({"online": true})", true},
      {R"({"online": false})", false},
      // spaces around colon
      {R"({"online" : true})", true},
      {R"({"online" : false})", false},
      // tabs and newlines
      {"{\"online\"\t:\ttrue}", true},
      {"{\"online\"\n:\nfalse}", false},
      // with timestamp field
      {R"({"online" : true, "timestamp": 123456})", true},
      {R"({"online" : false, "timestamp": 123456})", false},
  };

  for (size_t i = 0; i < cases.size(); ++i) {
    const auto& tc = cases[i];

    // Set to opposite so we can detect a change
    if (tc.expected) {
      publish_raw(broker, state_topic, R"({"online":false})");
    } else {
      publish_raw(broker, state_topic, R"({"online":true})");
    }
    std::this_thread::sleep_for(SETTLE);
    assert(node.is_primary_host_online() != tc.expected);

    publish_raw(broker, state_topic, tc.json);
    std::this_thread::sleep_for(SETTLE);

    if (node.is_primary_host_online() != tc.expected) {
      std::cerr << "[FAIL] case " << i << ": json=" << tc.json
                << " expected=" << tc.expected << " got=" << node.is_primary_host_online()
                << "\n";
      assert(false);
    }
  }

  // Clean up retained message
  publish_raw(broker, state_topic, "");

  (void)node.disconnect();
  std::cout << "[OK] STATE JSON parsing handles " << cases.size()
            << " whitespace variations\n";
}

int main() {
  std::cout << "=== STATE JSON Parsing Tests ===\n\n";

  test_whitespace_variations();

  std::cout << "\n=== All STATE JSON parsing tests passed! ===\n";
  return 0;
}
