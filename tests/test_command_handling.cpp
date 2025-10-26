// tests/test_command_handling.cpp
// Tests for command handling (NCMD/DCMD)
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

// Test 1: NCMD callback is invoked
void test_ncmd_callback_invoked() {
  std::atomic<bool> command_received{false};
  std::atomic<bool> rebirth_command{false};

  auto command_callback = [&](const sparkplug::Topic& topic,
                              const org::eclipse::tahu::protobuf::Payload& payload) {
    if (topic.message_type == sparkplug::MessageType::NCMD) {
      command_received = true;
      for (const auto& metric : payload.metrics()) {
        if (metric.name() == "Node Control/Rebirth") {
          rebirth_command = metric.boolean_value();
        }
      }
    }
  };

  sparkplug::Subscriber::Config sub_config{
      .broker_url = "tcp://localhost:1883", .client_id = "test_ncmd_sub", .group_id = "TestGroup"};

  sparkplug::Subscriber sub(
      std::move(sub_config),
      [](const sparkplug::Topic&, const org::eclipse::tahu::protobuf::Payload&) {});

  sub.set_command_callback(command_callback);

  if (!sub.connect()) {
    report_test("NCMD callback invoked", false, "Subscriber failed to connect");
    return;
  }

  if (!sub.subscribe_node("Gateway01")) {
    report_test("NCMD callback invoked", false, "Subscribe failed");
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Send command
  sparkplug::Publisher::Config pub_config{.broker_url = "tcp://localhost:1883",
                                          .client_id = "test_ncmd_pub",
                                          .group_id = "TestGroup",
                                          .edge_node_id = "ScadaHost"};

  sparkplug::Publisher pub(std::move(pub_config));

  if (!pub.connect()) {
    report_test("NCMD callback invoked", false, "Publisher failed to connect");
    (void)sub.disconnect();
    return;
  }

  sparkplug::PayloadBuilder birth;
  birth.add_metric("test", 0);
  if (!pub.publish_birth(birth)) {
    report_test("NCMD callback invoked", false, "NBIRTH failed");
    (void)pub.disconnect();
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  sparkplug::PayloadBuilder cmd;
  cmd.add_metric("Node Control/Rebirth", true);

  auto cmd_result = pub.publish_node_command("Gateway01", cmd);
  if (!cmd_result) {
    report_test("NCMD callback invoked", false, "Failed to send command: " + cmd_result.error());
    (void)pub.disconnect();
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  bool passed = command_received && rebirth_command;
  report_test("NCMD callback invoked", passed,
              passed ? ""
                     : std::format("Received: {}, Rebirth: {}", command_received.load(),
                                   rebirth_command.load()));

  (void)pub.disconnect();
  (void)sub.disconnect();
}

// Test 2: DCMD callback is invoked
void test_dcmd_callback_invoked() {
  std::atomic<bool> command_received{false};
  std::string received_device;
  std::string received_metric;

  auto command_callback = [&](const sparkplug::Topic& topic,
                              const org::eclipse::tahu::protobuf::Payload& payload) {
    if (topic.message_type == sparkplug::MessageType::DCMD) {
      command_received = true;
      received_device = topic.device_id;
      if (!payload.metrics().empty()) {
        received_metric = payload.metrics()[0].name();
      }
    }
  };

  sparkplug::Subscriber::Config sub_config{
      .broker_url = "tcp://localhost:1883", .client_id = "test_dcmd_sub", .group_id = "TestGroup"};

  sparkplug::Subscriber sub(
      std::move(sub_config),
      [](const sparkplug::Topic&, const org::eclipse::tahu::protobuf::Payload&) {});

  sub.set_command_callback(command_callback);

  if (!sub.connect()) {
    report_test("DCMD callback invoked", false, "Subscriber failed to connect");
    return;
  }

  if (!sub.subscribe_node("Gateway01")) {
    report_test("DCMD callback invoked", false, "Subscribe failed");
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Send command
  sparkplug::Publisher::Config pub_config{.broker_url = "tcp://localhost:1883",
                                          .client_id = "test_dcmd_pub",
                                          .group_id = "TestGroup",
                                          .edge_node_id = "ScadaHost"};

  sparkplug::Publisher pub(std::move(pub_config));

  if (!pub.connect()) {
    report_test("DCMD callback invoked", false, "Publisher failed to connect");
    (void)sub.disconnect();
    return;
  }

  sparkplug::PayloadBuilder birth;
  birth.add_metric("test", 0);
  if (!pub.publish_birth(birth)) {
    report_test("DCMD callback invoked", false, "NBIRTH failed");
    (void)pub.disconnect();
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  sparkplug::PayloadBuilder cmd;
  cmd.add_metric("SetPoint", 75.0);

  auto cmd_result = pub.publish_device_command("Gateway01", "Motor01", cmd);
  if (!cmd_result) {
    report_test("DCMD callback invoked", false, "Failed to send command: " + cmd_result.error());
    (void)pub.disconnect();
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  bool passed =
      command_received && (received_device == "Motor01") && (received_metric == "SetPoint");
  report_test("DCMD callback invoked", passed,
              passed ? ""
                     : std::format("Received: {}, Device: {}, Metric: {}", command_received.load(),
                                   received_device, received_metric));

  (void)pub.disconnect();
  (void)sub.disconnect();
}

// Test 3: Multiple commands are handled
void test_multiple_commands() {
  std::atomic<int> command_count{0};
  std::atomic<int64_t> scan_rate{0};
  std::atomic<bool> rebirth_cmd{false};

  auto command_callback = [&](const sparkplug::Topic& topic,
                              const org::eclipse::tahu::protobuf::Payload& payload) {
    if (topic.message_type == sparkplug::MessageType::NCMD) {
      for (const auto& metric : payload.metrics()) {
        command_count++;
        if (metric.name() == "Node Control/Rebirth") {
          rebirth_cmd = metric.boolean_value();
        } else if (metric.name() == "Node Control/Scan Rate") {
          scan_rate = static_cast<int64_t>(metric.long_value());
        }
      }
    }
  };

  sparkplug::Subscriber::Config sub_config{.broker_url = "tcp://localhost:1883",
                                           .client_id = "test_multi_cmd_sub",
                                           .group_id = "TestGroup"};

  sparkplug::Subscriber sub(
      std::move(sub_config),
      [](const sparkplug::Topic&, const org::eclipse::tahu::protobuf::Payload&) {});

  sub.set_command_callback(command_callback);

  if (!sub.connect()) {
    report_test("Multiple commands handled", false, "Subscriber failed to connect");
    return;
  }

  if (!sub.subscribe_node("Gateway01")) {
    report_test("Multiple commands handled", false, "Subscribe failed");
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  sparkplug::Publisher::Config pub_config{.broker_url = "tcp://localhost:1883",
                                          .client_id = "test_multi_cmd_pub",
                                          .group_id = "TestGroup",
                                          .edge_node_id = "ScadaHost"};

  sparkplug::Publisher pub(std::move(pub_config));

  if (!pub.connect()) {
    report_test("Multiple commands handled", false, "Publisher failed to connect");
    (void)sub.disconnect();
    return;
  }

  sparkplug::PayloadBuilder birth;
  birth.add_metric("test", 0);
  if (!pub.publish_birth(birth)) {
    report_test("Multiple commands handled", false, "NBIRTH failed");
    (void)pub.disconnect();
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Send rebirth command
  sparkplug::PayloadBuilder cmd1;
  cmd1.add_metric("Node Control/Rebirth", true);
  (void)pub.publish_node_command("Gateway01", cmd1);

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Send scan rate command
  sparkplug::PayloadBuilder cmd2;
  cmd2.add_metric("Node Control/Scan Rate", static_cast<int64_t>(500));
  (void)pub.publish_node_command("Gateway01", cmd2);

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  bool passed = (command_count == 2) && rebirth_cmd && (scan_rate == 500);
  report_test("Multiple commands handled", passed,
              passed ? ""
                     : std::format("Count: {}, Rebirth: {}, Scan: {}", command_count.load(),
                                   rebirth_cmd.load(), scan_rate.load()));

  (void)pub.disconnect();
  (void)sub.disconnect();
}

// Test 4: Command callback and regular callback both invoked
void test_both_callbacks_invoked() {
  std::atomic<bool> command_callback_invoked{false};
  std::atomic<bool> regular_callback_invoked{false};

  auto command_callback = [&](const sparkplug::Topic& topic,
                              const org::eclipse::tahu::protobuf::Payload&) {
    if (topic.message_type == sparkplug::MessageType::NCMD) {
      command_callback_invoked = true;
    }
  };

  auto regular_callback = [&](const sparkplug::Topic& topic,
                              const org::eclipse::tahu::protobuf::Payload&) {
    if (topic.message_type == sparkplug::MessageType::NCMD) {
      regular_callback_invoked = true;
    }
  };

  sparkplug::Subscriber::Config sub_config{.broker_url = "tcp://localhost:1883",
                                           .client_id = "test_both_callbacks_sub",
                                           .group_id = "TestGroup"};

  sparkplug::Subscriber sub(std::move(sub_config), regular_callback);
  sub.set_command_callback(command_callback);

  if (!sub.connect()) {
    report_test("Both callbacks invoked", false, "Subscriber failed to connect");
    return;
  }

  if (!sub.subscribe_node("Gateway01")) {
    report_test("Both callbacks invoked", false, "Subscribe failed");
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  sparkplug::Publisher::Config pub_config{.broker_url = "tcp://localhost:1883",
                                          .client_id = "test_both_callbacks_pub",
                                          .group_id = "TestGroup",
                                          .edge_node_id = "ScadaHost"};

  sparkplug::Publisher pub(std::move(pub_config));

  if (!pub.connect()) {
    report_test("Both callbacks invoked", false, "Publisher failed to connect");
    (void)sub.disconnect();
    return;
  }

  sparkplug::PayloadBuilder birth;
  birth.add_metric("test", 0);
  if (!pub.publish_birth(birth)) {
    report_test("Both callbacks invoked", false, "NBIRTH failed");
    (void)pub.disconnect();
    (void)sub.disconnect();
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  sparkplug::PayloadBuilder cmd;
  cmd.add_metric("Node Control/Rebirth", true);
  (void)pub.publish_node_command("Gateway01", cmd);

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  bool passed = command_callback_invoked && regular_callback_invoked;
  report_test("Both callbacks invoked", passed,
              passed
                  ? ""
                  : std::format("Command CB: {}, Regular CB: {}", command_callback_invoked.load(),
                                regular_callback_invoked.load()));

  (void)pub.disconnect();
  (void)sub.disconnect();
}

int main() {
  std::cout << "Running Command Handling Tests...\n\n";

  test_ncmd_callback_invoked();
  test_dcmd_callback_invoked();
  test_multiple_commands();
  test_both_callbacks_invoked();

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
