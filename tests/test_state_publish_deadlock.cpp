// tests/test_state_publish_deadlock.cpp
// Verify publish_state_birth/death don't hold mutex_ during blocking MQTT send.
//
// The old code held mutex_ while waiting on a future inside publish_raw_message.
// If the Paho callback thread needed mutex_ (e.g., to deliver on_message_arrived)
// before firing the send-success callback, that was a deadlock.
//
// Test strategy: a publisher floods messages at the host while the host
// publishes STATE birth/death.  With the old code this would deadlock
// (or timeout); with the fix it completes promptly.

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

#include <sparkplug/edge_node.hpp>
#include <sparkplug/host_application.hpp>

static constexpr auto SETTLE = std::chrono::milliseconds(500);
static constexpr auto DEADLINE = std::chrono::seconds(10);

void test_state_publish_under_message_load() {
  std::atomic<int> messages_received{0};

  auto callback = [&](const sparkplug::Topic&, const auto&) {
    messages_received.fetch_add(1, std::memory_order_relaxed);
  };

  sparkplug::HostApplication::Config host_config{.broker_url = "tcp://localhost:1883",
                                                 .client_id = "test_deadlock_host",
                                                 .host_id = "DeadlockHost",
                                                 .message_callback = callback};
  sparkplug::HostApplication host(std::move(host_config));

  if (!host.connect() || !host.subscribe_all_groups()) {
    std::cout << "[SKIP] STATE publish deadlock (no broker)\n";
    return;
  }
  std::this_thread::sleep_for(SETTLE);

  sparkplug::EdgeNode::Config pub_config{.broker_url = "tcp://localhost:1883",
                                         .client_id = "test_deadlock_pub",
                                         .group_id = "DLGroup",
                                         .edge_node_id = "DLNode01"};
  sparkplug::EdgeNode pub(std::move(pub_config));

  if (!pub.connect()) {
    std::cout << "[SKIP] STATE publish deadlock (pub connect failed)\n";
    (void)host.disconnect();
    return;
  }

  sparkplug::PayloadBuilder birth;
  birth.add_metric("val", 0);
  assert(pub.publish_birth(birth).has_value());
  std::this_thread::sleep_for(SETTLE);

  std::atomic<bool> stop{false};
  std::thread flooder([&] {
    int i = 0;
    while (!stop.load(std::memory_order_relaxed)) {
      sparkplug::PayloadBuilder data;
      data.add_metric("val", i++);
      (void)pub.publish_data(data);
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  auto ts = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count());

  auto start = std::chrono::steady_clock::now();

  auto r1 = host.publish_state_birth(ts);
  auto r2 = host.publish_state_death(ts + 1);
  auto r3 = host.publish_state_birth(ts + 2);

  auto elapsed = std::chrono::steady_clock::now() - start;

  stop.store(true, std::memory_order_relaxed);
  flooder.join();

  assert(r1.has_value());
  assert(r2.has_value());
  assert(r3.has_value());
  assert(elapsed < DEADLINE);
  assert(messages_received.load() > 0);

  (void)pub.disconnect();
  std::this_thread::sleep_for(SETTLE);
  (void)host.disconnect();

  std::cout << "[OK] STATE publish completes under message load ("
            << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
            << "ms, " << messages_received.load() << " msgs received)\n";
}

int main() {
  std::cout << "=== STATE Publish Deadlock Tests ===\n\n";

  test_state_publish_under_message_load();

  std::cout << "\n=== All STATE publish deadlock tests passed! ===\n";
  return 0;
}
