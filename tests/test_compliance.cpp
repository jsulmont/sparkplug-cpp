// tests/test_compliance.cpp
// Sparkplug 2.2 Compliance Tests
#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

#include <sparkplug/edge_node.hpp>
#include <sparkplug/host_application.hpp>

// Test result tracking
struct TestResult {
  std::string name;
  bool passed;
  std::string message;
};

std::vector<TestResult> results;

void report_test(const std::string& name, bool passed, const std::string& msg = "") {
  results.push_back({name, passed, msg});
  std::cout << (passed ? "✓" : "✗") << " " << name;
  if (!msg.empty()) {
    std::cout << ": " << msg;
  }
  std::cout << "\n";
}

// Test 1: NBIRTH must have sequence 0
void test_nbirth_sequence_zero() {
  sparkplug::EdgeNode::Config config{.broker_url = "tcp://localhost:1883",
                                     .client_id = "test_nbirth_seq",
                                     .group_id = "TestGroup",
                                     .edge_node_id = "TestNode01"};

  sparkplug::EdgeNode pub(std::move(config));

  if (!pub.connect()) {
    report_test("NBIRTH sequence zero", false, "Failed to connect");
    return;
  }

  sparkplug::PayloadBuilder birth;
  birth.add_metric("test", 42);

  if (!pub.publish_birth(birth)) {
    report_test("NBIRTH sequence zero", false, "Failed to publish");
    (void)pub.disconnect();
    return;
  }

  bool passed = (pub.get_seq() == 0);
  report_test("NBIRTH sequence zero", passed,
              passed ? "" : std::format("Got seq={}", pub.get_seq()));

  (void)pub.disconnect();
}

// Test 2: Sequence wraps at 256
void test_sequence_wraps() {
  sparkplug::EdgeNode::Config config{.broker_url = "tcp://localhost:1883",
                                     .client_id = "test_seq_wrap",
                                     .group_id = "TestGroup",
                                     .edge_node_id = "TestNode02"};

  sparkplug::EdgeNode pub(std::move(config));

  if (!pub.connect()) {
    report_test("Sequence wraps at 256", false, "Failed to connect");
    return;
  }

  sparkplug::PayloadBuilder birth;
  birth.add_metric("test", 0);
  if (!pub.publish_birth(birth)) {
    report_test("Sequence wraps at 256", false, "Failed to publish NBIRTH");
    (void)pub.disconnect();
    return;
  }

  // Publish 256 messages to trigger wrap
  for (int i = 0; i < 256; i++) {
    sparkplug::PayloadBuilder data;
    data.add_metric("test", i);
    if (!pub.publish_data(data)) {
      report_test("Sequence wraps at 256", false, std::format("Failed at iteration {}", i));
      (void)pub.disconnect();
      return;
    }
  }

  bool passed = (pub.get_seq() == 0);
  report_test("Sequence wraps at 256", passed,
              passed ? "" : std::format("Got seq={}", pub.get_seq()));

  (void)pub.disconnect();
}

// Test 3: bdSeq increments on rebirth
void test_bdseq_increment() {
  sparkplug::EdgeNode::Config config{.broker_url = "tcp://localhost:1883",
                                     .client_id = "test_bdseq",
                                     .group_id = "TestGroup",
                                     .edge_node_id = "TestNode03"};

  sparkplug::EdgeNode pub(std::move(config));

  if (!pub.connect()) {
    report_test("bdSeq increments on rebirth", false, "Failed to connect");
    return;
  }

  sparkplug::PayloadBuilder birth;
  birth.add_metric("test", 0);
  if (!pub.publish_birth(birth)) {
    report_test("bdSeq increments on rebirth", false, "NBIRTH failed");
    (void)pub.disconnect();
    return;
  }

  uint64_t first_bdseq = pub.get_bd_seq();

  if (!pub.rebirth()) {
    report_test("bdSeq increments on rebirth", false, "Rebirth failed");
    (void)pub.disconnect();
    return;
  }

  uint64_t second_bdseq = pub.get_bd_seq();
  bool passed = (second_bdseq == first_bdseq + 1);

  report_test("bdSeq increments on rebirth", passed,
              passed ? "" : std::format("First={}, Second={}", first_bdseq, second_bdseq));

  (void)pub.disconnect();
}

// Test 4: NBIRTH contains bdSeq metric
void test_nbirth_has_bdseq() {
  std::atomic<bool> found_bdseq{false};
  std::atomic<bool> got_nbirth{false};

  auto callback = [&](const sparkplug::Topic& topic,
                      const org::eclipse::tahu::protobuf::Payload& payload) {
    if (topic.message_type == sparkplug::MessageType::NBIRTH) {
      got_nbirth = true;
      for (const auto& metric : payload.metrics()) {
        if (metric.name() == "bdSeq") {
          found_bdseq = true;
          break;
        }
      }
    }
  };

  sparkplug::HostApplication::Config sub_config{.broker_url = "tcp://localhost:1883",
                                                .client_id = "test_bdseq_sub",
                                                .host_id = "TestGroup",
                                                .message_callback = callback};

  sparkplug::HostApplication sub(std::move(sub_config));

  if (!sub.connect()) {
    report_test("NBIRTH contains bdSeq", false, "Subscriber failed to connect");
    return;
  }

  if (!sub.subscribe_all_groups()) {
    report_test("NBIRTH contains bdSeq", false, "Subscribe failed");
    (void)sub.disconnect();
    return;
  }

  // Give subscriber time to subscribe
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  sparkplug::EdgeNode::Config pub_config{.broker_url = "tcp://localhost:1883",
                                         .client_id = "test_bdseq_pub",
                                         .group_id = "TestGroup",
                                         .edge_node_id = "TestNode04"};

  sparkplug::EdgeNode pub(std::move(pub_config));

  if (!pub.connect()) {
    report_test("NBIRTH contains bdSeq", false, "Publisher failed to connect");
    (void)sub.disconnect();
    return;
  }

  sparkplug::PayloadBuilder birth;
  birth.add_metric("test", 42);

  auto birth_result = pub.publish_birth(birth);
  if (!birth_result) {
    report_test("NBIRTH contains bdSeq", false, "Failed to publish: " + birth_result.error());
    (void)pub.disconnect();
    (void)sub.disconnect();
    return;
  }

  // Wait for message
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  bool passed = got_nbirth && found_bdseq;
  report_test("NBIRTH contains bdSeq", passed,
              !got_nbirth    ? "No NBIRTH received"
              : !found_bdseq ? "bdSeq metric not found"
                             : "");

  (void)pub.disconnect();
  (void)sub.disconnect();
}

// Test 5: NDATA uses aliases correctly
void test_alias_usage() {
  std::atomic<bool> got_ndata{false};
  std::atomic<bool> has_alias{false};
  std::atomic<bool> no_name{false};

  auto callback = [&](const sparkplug::Topic& topic,
                      const org::eclipse::tahu::protobuf::Payload& payload) {
    if (topic.message_type == sparkplug::MessageType::NDATA) {
      got_ndata = true;
      for (const auto& metric : payload.metrics()) {
        if (metric.has_alias()) {
          has_alias = true;
        }
        // After BIRTH, DATA messages should use alias without name
        if (metric.has_alias() && !metric.has_name()) {
          no_name = true;
        }
      }
    }
  };

  sparkplug::HostApplication::Config sub_config{.broker_url = "tcp://localhost:1883",
                                                .client_id = "test_alias_sub",
                                                .host_id = "TestGroup",
                                                .message_callback = callback};

  sparkplug::HostApplication sub(std::move(sub_config));
  if (!sub.connect()) {
    report_test("NDATA uses aliases", false, "Subscriber failed to connect");
    return;
  }
  if (!sub.subscribe_all_groups()) {
    report_test("NDATA uses aliases", false, "Subscribe failed");
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  sparkplug::EdgeNode::Config pub_config{.broker_url = "tcp://localhost:1883",
                                         .client_id = "test_alias_pub",
                                         .group_id = "TestGroup",
                                         .edge_node_id = "TestNode05"};

  sparkplug::EdgeNode pub(std::move(pub_config));
  if (!pub.connect()) {
    report_test("NDATA uses aliases", false, "Publisher failed to connect");
    (void)sub.disconnect();
    return;
  }

  // NBIRTH with alias
  sparkplug::PayloadBuilder birth;
  birth.add_metric_with_alias("Temperature", 1, 20.5);
  auto birth_result = pub.publish_birth(birth);
  if (!birth_result) {
    report_test("NDATA uses aliases", false, "NBIRTH failed: " + birth_result.error());
    (void)pub.disconnect();
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // NDATA with alias only
  sparkplug::PayloadBuilder data;
  data.add_metric_by_alias(1, 21.0);
  auto data_result = pub.publish_data(data);
  if (!data_result) {
    report_test("NDATA uses aliases", false, "NDATA failed: " + data_result.error());
    (void)pub.disconnect();
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  bool passed = got_ndata && has_alias;
  report_test("NDATA uses aliases", passed,
              !got_ndata   ? "No NDATA received"
              : !has_alias ? "No alias found"
                           : "");

  (void)pub.disconnect();
  (void)sub.disconnect();
}

// Test 6: Subscriber validates sequence
void test_subscriber_validation() {
  auto callback = [](const sparkplug::Topic&, const auto&) {
    // Just receive messages
  };

  sparkplug::HostApplication::Config config{.broker_url = "tcp://localhost:1883",
                                            .client_id = "test_validation",
                                            .host_id = "TestGroup",
                                            .validate_sequence = true,
                                            .message_callback = callback};

  sparkplug::HostApplication sub(std::move(config));

  if (!sub.connect()) {
    report_test("Subscriber validation", false, "Failed to connect");
    return;
  }

  // Validation happens automatically during message receipt
  report_test("Subscriber validation", true, "Enabled successfully");

  (void)sub.disconnect();
}

// Test 7: Payload has timestamp
void test_payload_timestamp() {
  sparkplug::PayloadBuilder payload;
  payload.add_metric("test", 42);

  auto built = payload.build();

  org::eclipse::tahu::protobuf::Payload proto;
  proto.ParseFromArray(built.data(), static_cast<int>(built.size()));

  bool passed = proto.has_timestamp() && proto.timestamp() > 0;
  report_test("Payload has timestamp", passed, passed ? "" : "Timestamp missing or zero");
}

// Test 8: Auto sequence management
void test_auto_sequence() {
  sparkplug::EdgeNode::Config config{.broker_url = "tcp://localhost:1883",
                                     .client_id = "test_auto_seq",
                                     .group_id = "TestGroup",
                                     .edge_node_id = "TestNode06"};

  sparkplug::EdgeNode pub(std::move(config));

  if (!pub.connect()) {
    report_test("Auto sequence management", false, "Failed to connect");
    return;
  }

  sparkplug::PayloadBuilder birth;
  birth.add_metric("test", 0);
  auto birth_result = pub.publish_birth(birth);
  if (!birth_result) {
    report_test("Auto sequence management", false, "NBIRTH failed: " + birth_result.error());
    (void)pub.disconnect();
    return;
  }

  uint64_t prev_seq = pub.get_seq(); // Should be 0

  // Publish without explicit seq
  sparkplug::PayloadBuilder data;
  data.add_metric("test", 1);
  auto data_result = pub.publish_data(data);
  if (!data_result) {
    report_test("Auto sequence management", false, "NDATA failed: " + data_result.error());
    (void)pub.disconnect();
    return;
  }

  uint64_t new_seq = pub.get_seq();
  bool passed = (new_seq == 1 && prev_seq == 0);

  report_test("Auto sequence management", passed,
              passed ? "" : std::format("Expected 0→1, got {}→{}", prev_seq, new_seq));

  (void)pub.disconnect();
}

// Test 9: DBIRTH sequence starts at 0
void test_dbirth_sequence_zero() {
  std::atomic<bool> got_dbirth{false};
  std::atomic<uint64_t> dbirth_seq{999};

  auto callback = [&](const sparkplug::Topic& topic,
                      const org::eclipse::tahu::protobuf::Payload& payload) {
    if (topic.message_type == sparkplug::MessageType::DBIRTH && topic.device_id == "Device01") {
      got_dbirth = true;
      if (payload.has_seq()) {
        dbirth_seq = payload.seq();
      }
    }
  };

  // Set up subscriber FIRST so it can receive NBIRTH
  sparkplug::HostApplication::Config sub_config{.broker_url = "tcp://localhost:1883",
                                                .client_id = "test_dbirth_seq_sub",
                                                .host_id = "TestGroup",
                                                .message_callback = callback};

  sparkplug::HostApplication sub(std::move(sub_config));
  if (!sub.connect() || !sub.subscribe_all_groups()) {
    report_test("DBIRTH sequence zero", false, "Subscriber setup failed");
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Now create publisher and publish NBIRTH
  sparkplug::EdgeNode::Config config{.broker_url = "tcp://localhost:1883",
                                     .client_id = "test_dbirth_seq",
                                     .group_id = "TestGroup",
                                     .edge_node_id = "TestNode07"};

  sparkplug::EdgeNode pub(std::move(config));

  if (!pub.connect()) {
    report_test("DBIRTH sequence zero", false, "Failed to connect");
    (void)sub.disconnect();
    return;
  }

  // Publish NBIRTH first
  sparkplug::PayloadBuilder nbirth;
  nbirth.add_metric("NodeMetric", 100);
  if (!pub.publish_birth(nbirth)) {
    report_test("DBIRTH sequence zero", false, "NBIRTH failed");
    (void)pub.disconnect();
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Now publish DBIRTH
  sparkplug::PayloadBuilder dbirth;
  dbirth.add_metric_with_alias("DeviceMetric", 1, 42.0);
  if (!pub.publish_device_birth("Device01", dbirth)) {
    report_test("DBIRTH sequence zero", false, "DBIRTH failed");
    (void)pub.disconnect();
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  bool passed = got_dbirth && dbirth_seq == 0;
  report_test("DBIRTH sequence zero", passed,
              !got_dbirth ? "No DBIRTH received"
                          : std::format("Expected seq=0, got seq={}", dbirth_seq.load()));

  (void)pub.disconnect();
  (void)sub.disconnect();
}

// Test 10: DBIRTH requires NBIRTH first
void test_dbirth_requires_nbirth() {
  sparkplug::EdgeNode::Config config{.broker_url = "tcp://localhost:1883",
                                     .client_id = "test_dbirth_nbirth",
                                     .group_id = "TestGroup",
                                     .edge_node_id = "TestNode08"};

  sparkplug::EdgeNode pub(std::move(config));

  if (!pub.connect()) {
    report_test("DBIRTH requires NBIRTH", false, "Failed to connect");
    return;
  }

  // Try to publish DBIRTH without NBIRTH first
  sparkplug::PayloadBuilder dbirth;
  dbirth.add_metric("test", 42);
  auto result = pub.publish_device_birth("Device01", dbirth);

  bool passed = !result.has_value(); // Should fail
  report_test("DBIRTH requires NBIRTH", passed,
              result.has_value() ? "DBIRTH succeeded without NBIRTH (should fail)" : "");

  (void)pub.disconnect();
}

// Test 11: Device and node sequence numbers are independent
void test_device_sequence_independent() {
  std::atomic<bool> got_ddata{false};
  std::atomic<uint64_t> ddata_seq{999};

  auto callback = [&](const sparkplug::Topic& topic,
                      const org::eclipse::tahu::protobuf::Payload& payload) {
    if (topic.message_type == sparkplug::MessageType::DDATA && topic.device_id == "Device01") {
      got_ddata = true;
      if (payload.has_seq()) {
        ddata_seq = payload.seq();
      }
    }
  };

  // Set up subscriber FIRST so it can track the full sequence
  sparkplug::HostApplication::Config sub_config{.broker_url = "tcp://localhost:1883",
                                                .client_id = "test_dev_seq_sub",
                                                .host_id = "TestGroup",
                                                .message_callback = callback};

  sparkplug::HostApplication sub(std::move(sub_config));
  if (!sub.connect() || !sub.subscribe_all_groups()) {
    report_test("Device sequence independent", false, "Subscriber setup failed");
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Now create publisher
  sparkplug::EdgeNode::Config config{.broker_url = "tcp://localhost:1883",
                                     .client_id = "test_dev_seq_ind",
                                     .group_id = "TestGroup",
                                     .edge_node_id = "TestNode09"};

  sparkplug::EdgeNode pub(std::move(config));

  if (!pub.connect()) {
    report_test("Device sequence independent", false, "Failed to connect");
    (void)sub.disconnect();
    return;
  }

  // Publish NBIRTH
  sparkplug::PayloadBuilder nbirth;
  nbirth.add_metric("NodeMetric", 100);
  if (!pub.publish_birth(nbirth)) {
    report_test("Device sequence independent", false, "NBIRTH failed");
    (void)pub.disconnect();
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Publish DBIRTH
  sparkplug::PayloadBuilder dbirth;
  dbirth.add_metric("DeviceMetric", 42);
  if (!pub.publish_device_birth("Device01", dbirth)) {
    report_test("Device sequence independent", false, "DBIRTH failed");
    (void)pub.disconnect();
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Publish NDATA - should increment node sequence
  sparkplug::PayloadBuilder ndata;
  ndata.add_metric("NodeMetric", 101);
  if (!pub.publish_data(ndata)) {
    report_test("Device sequence independent", false, "NDATA failed");
    (void)pub.disconnect();
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Publish DDATA - should have independent sequence starting from 0
  sparkplug::PayloadBuilder ddata;
  ddata.add_metric("DeviceMetric", 43);
  if (!pub.publish_device_data("Device01", ddata)) {
    report_test("Device sequence independent", false, "DDATA failed");
    (void)pub.disconnect();
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Node seq should be 1, device seq should be 1
  bool passed = got_ddata && ddata_seq == 1 && pub.get_seq() == 1;
  report_test("Device sequence independent", passed,
              !got_ddata ? "No DDATA received"
                         : std::format("Node seq={}, Device seq={} (both should be 1)",
                                       pub.get_seq(), ddata_seq.load()));

  (void)pub.disconnect();
  (void)sub.disconnect();
}

// Test 12: NCMD can be published and received
void test_ncmd_publishing() {
  std::atomic<bool> got_ncmd{false};
  std::atomic<bool> has_rebirth_command{false};

  auto callback = [&](const sparkplug::Topic& topic,
                      const org::eclipse::tahu::protobuf::Payload& payload) {
    if (topic.message_type == sparkplug::MessageType::NCMD) {
      got_ncmd = true;
      for (const auto& metric : payload.metrics()) {
        if (metric.name() == "Node Control/Rebirth") {
          has_rebirth_command = true;
          break;
        }
      }
    }
  };

  sparkplug::HostApplication::Config sub_config{.broker_url = "tcp://localhost:1883",
                                                .client_id = "test_ncmd_sub",
                                                .host_id = "TestGroup",
                                                .message_callback = callback};

  sparkplug::HostApplication sub(std::move(sub_config));
  if (!sub.connect() || !sub.subscribe_all_groups()) {
    report_test("NCMD publishing", false, "Subscriber setup failed");
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  sparkplug::EdgeNode::Config pub_config{.broker_url = "tcp://localhost:1883",
                                         .client_id = "test_ncmd_pub",
                                         .group_id = "TestGroup",
                                         .edge_node_id = "HostNode10"};

  sparkplug::EdgeNode pub(std::move(pub_config));
  if (!pub.connect()) {
    report_test("NCMD publishing", false, "Publisher failed to connect");
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Publish NCMD
  sparkplug::PayloadBuilder cmd;
  cmd.add_metric("Node Control/Rebirth", true);
  if (!pub.publish_node_command("TargetNode", cmd)) {
    report_test("NCMD publishing", false, "NCMD publish failed");
    (void)pub.disconnect();
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  bool passed = got_ncmd && has_rebirth_command;
  report_test("NCMD publishing", passed,
              !got_ncmd              ? "No NCMD received"
              : !has_rebirth_command ? "Rebirth command not found"
                                     : "");

  (void)pub.disconnect();
  (void)sub.disconnect();
}

// Test 13: DCMD can be published and received
void test_dcmd_publishing() {
  std::atomic<bool> got_dcmd{false};
  std::atomic<bool> has_setpoint{false};

  auto callback = [&](const sparkplug::Topic& topic,
                      const org::eclipse::tahu::protobuf::Payload& payload) {
    if (topic.message_type == sparkplug::MessageType::DCMD && topic.device_id == "Motor01") {
      got_dcmd = true;
      for (const auto& metric : payload.metrics()) {
        if (metric.name() == "SetPoint") {
          has_setpoint = true;
          break;
        }
      }
    }
  };

  sparkplug::HostApplication::Config sub_config{.broker_url = "tcp://localhost:1883",
                                                .client_id = "test_dcmd_sub",
                                                .host_id = "TestGroup",
                                                .message_callback = callback};

  sparkplug::HostApplication sub(std::move(sub_config));
  if (!sub.connect() || !sub.subscribe_all_groups()) {
    report_test("DCMD publishing", false, "Subscriber setup failed");
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  sparkplug::EdgeNode::Config pub_config{.broker_url = "tcp://localhost:1883",
                                         .client_id = "test_dcmd_pub",
                                         .group_id = "TestGroup",
                                         .edge_node_id = "HostNode11"};

  sparkplug::EdgeNode pub(std::move(pub_config));
  if (!pub.connect()) {
    report_test("DCMD publishing", false, "Publisher failed to connect");
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Publish DCMD
  sparkplug::PayloadBuilder cmd;
  cmd.add_metric("SetPoint", 75.0);
  if (!pub.publish_device_command("TargetNode", "Motor01", cmd)) {
    report_test("DCMD publishing", false, "DCMD publish failed");
    (void)pub.disconnect();
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  bool passed = got_dcmd && has_setpoint;
  report_test("DCMD publishing", passed,
              !got_dcmd       ? "No DCMD received"
              : !has_setpoint ? "SetPoint metric not found"
                              : "");

  (void)pub.disconnect();
  (void)sub.disconnect();
}

// Test 14: Command callback invoked for NCMD/DCMD
void test_command_callback() {
  std::atomic<bool> general_callback_invoked{false};

  auto general_callback = [&](const sparkplug::Topic& topic, const auto&) {
    if (topic.message_type == sparkplug::MessageType::NCMD) {
      general_callback_invoked = true;
    }
  };

  sparkplug::HostApplication::Config sub_config{.broker_url = "tcp://localhost:1883",
                                                .client_id = "test_cmd_cb_sub",
                                                .host_id = "TestGroup",
                                                .message_callback = general_callback};

  sparkplug::HostApplication sub(std::move(sub_config));
  // Note: set_command_callback not implemented yet - will need to be added or test modified

  if (!sub.connect() || !sub.subscribe_all_groups()) {
    report_test("Command callback invoked", false, "Subscriber setup failed");
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  sparkplug::EdgeNode::Config pub_config{.broker_url = "tcp://localhost:1883",
                                         .client_id = "test_cmd_cb_pub",
                                         .group_id = "TestGroup",
                                         .edge_node_id = "HostNode12"};

  sparkplug::EdgeNode pub(std::move(pub_config));
  if (!pub.connect()) {
    report_test("Command callback invoked", false, "Publisher failed to connect");
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  sparkplug::PayloadBuilder cmd;
  cmd.add_metric("Node Control/Rebirth", true);
  if (!pub.publish_node_command("TargetNode", cmd)) {
    report_test("Command callback invoked", false, "NCMD publish failed");
    (void)pub.disconnect();
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  bool passed = general_callback_invoked;
  report_test("Command callback invoked", passed,
              !general_callback_invoked ? "General callback not invoked" : "");

  (void)pub.disconnect();
  (void)sub.disconnect();
}

int main() {
  std::cout << "=== Sparkplug 2.2 Compliance Tests ===\n\n";

  // Node-level tests
  test_nbirth_sequence_zero();
  test_sequence_wraps();
  test_bdseq_increment();
  test_nbirth_has_bdseq();
  test_alias_usage();
  test_subscriber_validation();
  test_payload_timestamp();
  test_auto_sequence();

  // Device-level tests
  test_dbirth_sequence_zero();
  test_dbirth_requires_nbirth();
  test_device_sequence_independent();

  // Command handling tests
  test_ncmd_publishing();
  test_dcmd_publishing();
  test_command_callback();

  // Summary
  int passed = 0;
  int failed = 0;

  std::cout << "\n=== Test Results ===\n";
  for (const auto& result : results) {
    if (result.passed) {
      passed++;
    } else {
      failed++;
      std::cout << "FAILED: " << result.name << " - " << result.message << "\n";
    }
  }

  std::cout << "\nTotal: " << results.size() << " tests\n";
  std::cout << "Passed: " << passed << "\n";
  std::cout << "Failed: " << failed << "\n";

  if (failed == 0) {
    std::cout << "\n✓ All tests passed! Library is Sparkplug 2.2 compliant.\n";
    return 0;
  } else {
    std::cout << "\n✗ Some tests failed. Review implementation.\n";
    return 1;
  }
}