// tests/test_device_apis.cpp
// Tests for device-level Sparkplug B APIs (DBIRTH/DDATA/DDEATH)
#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

#include <sparkplug/publisher.hpp>
#include <sparkplug/subscriber.hpp>

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
  sparkplug::Publisher::Config config{.broker_url = "tcp://localhost:1883",
                                      .client_id = "test_device_nbirth",
                                      .group_id = "TestGroup",
                                      .edge_node_id = "TestNode"};

  sparkplug::Publisher pub(std::move(config));

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

  sparkplug::Subscriber::Config sub_config{.broker_url = "tcp://localhost:1883",
                                           .client_id = "test_dbirth_seq_sub",
                                           .group_id = "TestGroup"};

  sparkplug::Subscriber sub(std::move(sub_config), callback);

  if (!sub.connect()) {
    report_test("DBIRTH sequence zero", false, "Subscriber failed to connect");
    return;
  }

  if (!sub.subscribe_all()) {
    report_test("DBIRTH sequence zero", false, "Subscribe failed");
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  sparkplug::Publisher::Config pub_config{.broker_url = "tcp://localhost:1883",
                                          .client_id = "test_dbirth_seq_pub",
                                          .group_id = "TestGroup",
                                          .edge_node_id = "TestNode"};

  sparkplug::Publisher pub(std::move(pub_config));

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

  bool passed = found_dbirth && (dbirth_seq == 0);
  report_test("DBIRTH sequence zero", passed,
              passed ? ""
                     : std::format("Found: {}, Seq: {}", found_dbirth.load(), dbirth_seq.load()));

  (void)pub.disconnect();
  (void)sub.disconnect();
}

// Test 3: DDATA requires DBIRTH first
void test_ddata_requires_dbirth() {
  sparkplug::Publisher::Config config{.broker_url = "tcp://localhost:1883",
                                      .client_id = "test_ddata_birth",
                                      .group_id = "TestGroup",
                                      .edge_node_id = "TestNode"};

  sparkplug::Publisher pub(std::move(config));

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

// Test 4: Device sequence increments independently from node sequence
void test_device_sequence_independent() {
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

  sparkplug::Subscriber::Config sub_config{.broker_url = "tcp://localhost:1883",
                                           .client_id = "test_seq_indep_sub",
                                           .group_id = "TestGroup"};

  sparkplug::Subscriber sub(std::move(sub_config), callback);

  if (!sub.connect()) {
    report_test("Device sequence independent", false, "Subscriber failed to connect");
    return;
  }

  if (!sub.subscribe_all()) {
    report_test("Device sequence independent", false, "Subscribe failed");
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  sparkplug::Publisher::Config pub_config{.broker_url = "tcp://localhost:1883",
                                          .client_id = "test_seq_indep_pub",
                                          .group_id = "TestGroup",
                                          .edge_node_id = "TestNode"};

  sparkplug::Publisher pub(std::move(pub_config));

  if (!pub.connect()) {
    report_test("Device sequence independent", false, "Publisher failed to connect");
    (void)sub.disconnect();
    return;
  }

  // Publish NBIRTH
  sparkplug::PayloadBuilder node_birth;
  node_birth.add_metric("node_value", 0);
  if (!pub.publish_birth(node_birth)) {
    report_test("Device sequence independent", false, "NBIRTH failed");
    (void)pub.disconnect();
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Publish DBIRTH
  sparkplug::PayloadBuilder device_birth;
  device_birth.add_metric("device_value", 0);
  if (!pub.publish_device_birth("Device01", device_birth)) {
    report_test("Device sequence independent", false, "DBIRTH failed");
    (void)pub.disconnect();
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Publish several NDATA and DDATA messages
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

  // Both sequences should have advanced independently
  // NDATA: 1,2,3,4,5 (started after NBIRTH with seq=0)
  // DDATA: 1,2,3,4,5 (started after DBIRTH with seq=0)
  bool passed =
      (ndata_count == 5) && (ddata_count == 5) && (last_ndata_seq == 5) && (last_ddata_seq == 5);

  report_test("Device sequence independent", passed,
              passed
                  ? ""
                  : std::format("NDATA: {} (seq={}), DDATA: {} (seq={})", ndata_count.load(),
                                last_ndata_seq.load(), ddata_count.load(), last_ddata_seq.load()));

  (void)pub.disconnect();
  (void)sub.disconnect();
}

// Test 5: DDEATH marks device offline
void test_ddeath() {
  std::atomic<bool> got_ddeath{false};

  auto callback = [&](const sparkplug::Topic& topic,
                      const org::eclipse::tahu::protobuf::Payload& payload) {
    (void)payload;
    if (topic.message_type == sparkplug::MessageType::DDEATH && topic.device_id == "Device01") {
      got_ddeath = true;
    }
  };

  sparkplug::Subscriber::Config sub_config{.broker_url = "tcp://localhost:1883",
                                           .client_id = "test_ddeath_sub",
                                           .group_id = "TestGroup"};

  sparkplug::Subscriber sub(std::move(sub_config), callback);

  if (!sub.connect()) {
    report_test("DDEATH marks device offline", false, "Subscriber failed to connect");
    return;
  }

  if (!sub.subscribe_all()) {
    report_test("DDEATH marks device offline", false, "Subscribe failed");
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  sparkplug::Publisher::Config pub_config{.broker_url = "tcp://localhost:1883",
                                          .client_id = "test_ddeath_pub",
                                          .group_id = "TestGroup",
                                          .edge_node_id = "TestNode"};

  sparkplug::Publisher pub(std::move(pub_config));

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
  test_device_sequence_independent();
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
