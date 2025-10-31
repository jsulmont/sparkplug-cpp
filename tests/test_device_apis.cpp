// tests/test_device_apis.cpp
// Tests for device-level Sparkplug B APIs (DBIRTH/DDATA/DDEATH)
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
  std::cout << (passed ? "[PASS]" : "[FAIL]") << " " << name;
  if (!msg.empty()) {
    std::cout << ": " << msg;
  }
  std::cout << "\n";
}

// Test 1: DBIRTH requires NBIRTH first
void test_dbirth_requires_nbirth() {
  sparkplug::EdgeNode::Config config{.broker_url = "tcp://localhost:1883",
                                     .client_id = "test_device_nbirth",
                                     .group_id = "TestGroup",
                                     .edge_node_id = "TestNodeDev01"};

  sparkplug::EdgeNode pub(std::move(config));

  if (!pub.connect()) {
    report_test("DBIRTH requires NBIRTH first", false, "Failed to connect");
    return;
  }

  // Try to publish DBIRTH without NBIRTH
  sparkplug::PayloadBuilder device_birth;
  device_birth.add_metric("test", 42);

  auto result = pub.publish_device_birth("Device01", device_birth);

  bool passed = !result.has_value(); // Should fail
  report_test("DBIRTH requires NBIRTH first", passed,
              passed ? "" : "DBIRTH succeeded without NBIRTH");

  (void)pub.disconnect();
}

// Test 2: DBIRTH sequence starts at 0
void test_dbirth_sequence_zero() {
  std::atomic<bool> found_dbirth{false};
  std::atomic<uint64_t> dbirth_seq{999};

  auto callback = [&](const sparkplug::Topic& topic,
                      const org::eclipse::tahu::protobuf::Payload& payload) {
    if (topic.message_type == sparkplug::MessageType::DBIRTH && topic.device_id == "Device01") {
      found_dbirth = true;
      if (payload.has_seq()) {
        dbirth_seq = payload.seq();
      }
    }
  };

  sparkplug::HostApplication::Config sub_config{.broker_url = "tcp://localhost:1883",
                                                .client_id = "test_dbirth_seq_sub",
                                                .host_id = "TestGroup"};

  sub_config.message_callback = callback;
  sparkplug::HostApplication sub(std::move(sub_config));

  if (!sub.connect()) {
    report_test("DBIRTH sequence zero", false, "Subscriber failed to connect");
    return;
  }

  if (!sub.subscribe_all_groups()) {
    report_test("DBIRTH sequence zero", false, "Subscribe failed");
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  sparkplug::EdgeNode::Config pub_config{.broker_url = "tcp://localhost:1883",
                                         .client_id = "test_dbirth_seq_pub",
                                         .group_id = "TestGroup",
                                         .edge_node_id = "TestNodeDev02"};

  sparkplug::EdgeNode pub(std::move(pub_config));

  if (!pub.connect()) {
    report_test("DBIRTH sequence zero", false, "Publisher failed to connect");
    (void)sub.disconnect();
    return;
  }

  // Publish NBIRTH first
  sparkplug::PayloadBuilder node_birth;
  node_birth.add_metric("test", 0);
  if (!pub.publish_birth(node_birth)) {
    report_test("DBIRTH sequence zero", false, "NBIRTH failed");
    (void)pub.disconnect();
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Publish DBIRTH
  sparkplug::PayloadBuilder device_birth;
  device_birth.add_metric("value", 42);

  auto result = pub.publish_device_birth("Device01", device_birth);
  if (!result) {
    report_test("DBIRTH sequence zero", false, "DBIRTH failed: " + result.error());
    (void)pub.disconnect();
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // After fix: NBIRTH has seq=0, DBIRTH increments to seq=1 (shared counter)
  bool passed = found_dbirth && (dbirth_seq == 1);
  report_test("DBIRTH sequence zero", passed,
              passed ? ""
                     : std::format("Found: {}, Seq: {} (expected 1 after NBIRTH seq=0)",
                                   found_dbirth.load(), dbirth_seq.load()));

  (void)pub.disconnect();
  (void)sub.disconnect();
}

// Test 3: DDATA requires DBIRTH first
void test_ddata_requires_dbirth() {
  sparkplug::EdgeNode::Config config{.broker_url = "tcp://localhost:1883",
                                     .client_id = "test_ddata_birth",
                                     .group_id = "TestGroup",
                                     .edge_node_id = "TestNodeDev03"};

  sparkplug::EdgeNode pub(std::move(config));

  if (!pub.connect()) {
    report_test("DDATA requires DBIRTH first", false, "Failed to connect");
    return;
  }

  // Publish NBIRTH
  sparkplug::PayloadBuilder node_birth;
  node_birth.add_metric("test", 0);
  if (!pub.publish_birth(node_birth)) {
    report_test("DDATA requires DBIRTH first", false, "NBIRTH failed");
    (void)pub.disconnect();
    return;
  }

  // Try to publish DDATA without DBIRTH
  sparkplug::PayloadBuilder device_data;
  device_data.add_metric("test", 42);

  auto result = pub.publish_device_data("Device01", device_data);

  bool passed = !result.has_value(); // Should fail
  report_test("DDATA requires DBIRTH first", passed,
              passed ? "" : "DDATA succeeded without DBIRTH");

  (void)pub.disconnect();
}

// Test 4: Device and node messages share sequence counter
void test_device_sequence_shared() {
  std::atomic<int> ndata_count{0};
  std::atomic<int> ddata_count{0};
  std::atomic<uint64_t> last_ndata_seq{0};
  std::atomic<uint64_t> last_ddata_seq{0};

  auto callback = [&](const sparkplug::Topic& topic,
                      const org::eclipse::tahu::protobuf::Payload& payload) {
    if (topic.message_type == sparkplug::MessageType::NDATA) {
      ndata_count++;
      if (payload.has_seq()) {
        last_ndata_seq = payload.seq();
      }
    } else if (topic.message_type == sparkplug::MessageType::DDATA) {
      ddata_count++;
      if (payload.has_seq()) {
        last_ddata_seq = payload.seq();
      }
    }
  };

  sparkplug::HostApplication::Config sub_config{.broker_url = "tcp://localhost:1883",
                                                .client_id = "test_seq_shared_sub",
                                                .host_id = "TestGroup"};

  sub_config.message_callback = callback;
  sparkplug::HostApplication sub(std::move(sub_config));

  if (!sub.connect()) {
    report_test("Device and node share sequence", false, "Subscriber failed to connect");
    return;
  }

  if (!sub.subscribe_all_groups()) {
    report_test("Device and node share sequence", false, "Subscribe failed");
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  sparkplug::EdgeNode::Config pub_config{.broker_url = "tcp://localhost:1883",
                                         .client_id = "test_seq_shared_pub",
                                         .group_id = "TestGroup",
                                         .edge_node_id = "TestNodeDev04"};

  sparkplug::EdgeNode pub(std::move(pub_config));

  if (!pub.connect()) {
    report_test("Device and node share sequence", false, "Publisher failed to connect");
    (void)sub.disconnect();
    return;
  }

  // Publish NBIRTH (seq=0)
  sparkplug::PayloadBuilder node_birth;
  node_birth.add_metric("node_value", 0);
  if (!pub.publish_birth(node_birth)) {
    report_test("Device and node share sequence", false, "NBIRTH failed");
    (void)pub.disconnect();
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Publish DBIRTH (seq=1, shared counter increments)
  sparkplug::PayloadBuilder device_birth;
  device_birth.add_metric("device_value", 0);
  if (!pub.publish_device_birth("Device01", device_birth)) {
    report_test("Device and node share sequence", false, "DBIRTH failed");
    (void)pub.disconnect();
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Publish several NDATA and DDATA messages (alternating)
  // Expected sequence: NBIRTH(0), DBIRTH(1), NDATA(2), DDATA(3), NDATA(4), DDATA(5), ...
  for (int i = 0; i < 5; i++) {
    sparkplug::PayloadBuilder ndata;
    ndata.add_metric("node_value", i);
    (void)pub.publish_data(ndata);

    sparkplug::PayloadBuilder ddata;
    ddata.add_metric("device_value", i);
    (void)pub.publish_device_data("Device01", ddata);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // After NBIRTH(0), DBIRTH(1), we publish 5 NDATA and 5 DDATA alternating
  // Last NDATA should be seq=10 (2,4,6,8,10), last DDATA should be seq=11 (3,5,7,9,11)
  bool passed = (ndata_count == 5) && (ddata_count == 5) && (last_ndata_seq == 10) &&
                (last_ddata_seq == 11);

  report_test("Device and node share sequence", passed,
              passed ? ""
                     : std::format("NDATA count={} seq={} (expected 5/10), DDATA count={} seq={} "
                                   "(expected 5/11)",
                                   ndata_count.load(), last_ndata_seq.load(), ddata_count.load(),
                                   last_ddata_seq.load()));

  (void)pub.disconnect();
  (void)sub.disconnect();
}

// Test 5: DDATA sequence increments correctly (TCK requirement)
void test_ddata_sequence_increments() {
  std::vector<uint64_t> ddata_sequences;
  std::atomic<uint64_t> dbirth_seq{999};

  auto callback = [&](const sparkplug::Topic& topic,
                      const org::eclipse::tahu::protobuf::Payload& payload) {
    if (topic.message_type == sparkplug::MessageType::DBIRTH && topic.device_id == "Device01") {
      if (payload.has_seq()) {
        dbirth_seq = payload.seq();
      }
    } else if (topic.message_type == sparkplug::MessageType::DDATA &&
               topic.device_id == "Device01") {
      if (payload.has_seq()) {
        ddata_sequences.push_back(payload.seq());
      }
    }
  };

  sparkplug::HostApplication::Config sub_config{.broker_url = "tcp://localhost:1883",
                                                .client_id = "test_ddata_seq_sub",
                                                .host_id = "TestGroup"};

  sub_config.message_callback = callback;
  sparkplug::HostApplication sub(std::move(sub_config));

  if (!sub.connect()) {
    report_test("DDATA sequence increments (TCK)", false, "Subscriber failed to connect");
    return;
  }

  if (!sub.subscribe_all_groups()) {
    report_test("DDATA sequence increments (TCK)", false, "Subscribe failed");
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  sparkplug::EdgeNode::Config pub_config{.broker_url = "tcp://localhost:1883",
                                         .client_id = "test_ddata_seq_pub",
                                         .group_id = "TestGroup",
                                         .edge_node_id = "TestNodeDev05"};

  sparkplug::EdgeNode pub(std::move(pub_config));

  if (!pub.connect()) {
    report_test("DDATA sequence increments (TCK)", false, "Publisher failed to connect");
    (void)sub.disconnect();
    return;
  }

  // Publish NBIRTH
  sparkplug::PayloadBuilder node_birth;
  node_birth.add_metric("test", 0);
  if (!pub.publish_birth(node_birth)) {
    report_test("DDATA sequence increments (TCK)", false, "NBIRTH failed");
    (void)pub.disconnect();
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Publish DBIRTH
  sparkplug::PayloadBuilder device_birth;
  device_birth.add_metric_with_alias("Temperature", 1, 20.5);
  if (!pub.publish_device_birth("Device01", device_birth)) {
    report_test("DDATA sequence increments (TCK)", false, "DBIRTH failed");
    (void)pub.disconnect();
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Publish 10 DDATA messages
  for (int i = 0; i < 10; i++) {
    sparkplug::PayloadBuilder ddata;
    ddata.add_metric_by_alias(1, 20.5 + i);
    if (!pub.publish_device_data("Device01", ddata)) {
      report_test("DDATA sequence increments (TCK)", false, std::format("DDATA #{} failed", i + 1));
      (void)pub.disconnect();
      (void)sub.disconnect();
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Verify TCK requirements with shared sequence:
  // NBIRTH(0) -> DBIRTH(1) -> DDATA(2,3,4,5,6,7,8,9,10,11)
  // 1. DBIRTH must have seq=1 (after NBIRTH seq=0)
  // 2. Every DDATA must increment by 1 from previous message
  // 3. First DDATA must have seq=2 (one greater than DBIRTH seq=1)
  bool passed = true;
  std::string error_msg;

  if (dbirth_seq != 1) {
    passed = false;
    error_msg = std::format("DBIRTH seq={}, expected 1 (after NBIRTH seq=0)", dbirth_seq.load());
  } else if (ddata_sequences.size() != 10) {
    passed = false;
    error_msg = std::format("Received {} DDATA messages, expected 10", ddata_sequences.size());
  } else {
    // Check each DDATA sequence increments correctly
    for (size_t i = 0; i < ddata_sequences.size(); i++) {
      uint64_t expected_seq = i + 2; // First DDATA should be 2 (after DBIRTH=1)
      if (ddata_sequences[i] != expected_seq) {
        passed = false;
        error_msg = std::format("DDATA #{} has seq={}, expected {}", i + 1, ddata_sequences[i],
                                expected_seq);
        break;
      }
    }
  }

  report_test("DDATA sequence increments (TCK)", passed, error_msg);

  (void)pub.disconnect();
  (void)sub.disconnect();
}

// Test 6: DDEATH marks device offline
void test_ddeath() {
  std::atomic<bool> got_ddeath{false};

  auto callback = [&](const sparkplug::Topic& topic,
                      const org::eclipse::tahu::protobuf::Payload& payload) {
    (void)payload;
    if (topic.message_type == sparkplug::MessageType::DDEATH && topic.device_id == "Device01") {
      got_ddeath = true;
    }
  };

  sparkplug::HostApplication::Config sub_config{
      .broker_url = "tcp://localhost:1883", .client_id = "test_ddeath_sub", .host_id = "TestGroup"};

  sub_config.message_callback = callback;
  sparkplug::HostApplication sub(std::move(sub_config));

  if (!sub.connect()) {
    report_test("DDEATH marks device offline", false, "Subscriber failed to connect");
    return;
  }

  if (!sub.subscribe_all_groups()) {
    report_test("DDEATH marks device offline", false, "Subscribe failed");
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  sparkplug::EdgeNode::Config pub_config{.broker_url = "tcp://localhost:1883",
                                         .client_id = "test_ddeath_pub",
                                         .group_id = "TestGroup",
                                         .edge_node_id = "TestNodeDev06"};

  sparkplug::EdgeNode pub(std::move(pub_config));

  if (!pub.connect()) {
    report_test("DDEATH marks device offline", false, "Publisher failed to connect");
    (void)sub.disconnect();
    return;
  }

  // Publish NBIRTH
  sparkplug::PayloadBuilder node_birth;
  node_birth.add_metric("test", 0);
  if (!pub.publish_birth(node_birth)) {
    report_test("DDEATH marks device offline", false, "NBIRTH failed");
    (void)pub.disconnect();
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Publish DBIRTH
  sparkplug::PayloadBuilder device_birth;
  device_birth.add_metric("value", 42);
  if (!pub.publish_device_birth("Device01", device_birth)) {
    report_test("DDEATH marks device offline", false, "DBIRTH failed");
    (void)pub.disconnect();
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Publish DDEATH
  auto result = pub.publish_device_death("Device01");
  if (!result) {
    report_test("DDEATH marks device offline", false, "DDEATH failed: " + result.error());
    (void)pub.disconnect();
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  bool passed = got_ddeath;
  report_test("DDEATH marks device offline", passed, passed ? "" : "DDEATH not received");

  (void)pub.disconnect();
  (void)sub.disconnect();
}

int main() {
  std::cout << "Running Device-Level API Tests...\n\n";

  test_dbirth_requires_nbirth();
  test_dbirth_sequence_zero();
  test_ddata_requires_dbirth();
  test_device_sequence_shared();
  test_ddata_sequence_increments();
  test_ddeath();

  // Summary
  std::cout << "\n========== Test Summary ==========\n";
  int passed = 0;
  int failed = 0;
  for (const auto& result : results) {
    if (result.passed) {
      passed++;
    } else {
      failed++;
      std::cout << "[FAIL] " << result.name;
      if (!result.message.empty()) {
        std::cout << ": " << result.message;
      }
      std::cout << "\n";
    }
  }

  std::cout << "\nTotal: " << results.size() << " tests\n";
  std::cout << "Passed: " << passed << "\n";
  std::cout << "Failed: " << failed << "\n";

  return failed > 0 ? 1 : 0;
}
