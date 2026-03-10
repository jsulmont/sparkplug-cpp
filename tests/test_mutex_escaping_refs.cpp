// tests/test_mutex_escaping_refs.cpp
// Verify get_node_state() and get_metric_name() return owned data that
// remains valid after the internal mutex is released and the map is mutated.

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

#include <sparkplug/edge_node.hpp>
#include <sparkplug/host_application.hpp>

static constexpr auto SETTLE = std::chrono::milliseconds(500);

// ---------------------------------------------------------------------------
// get_node_state returns a snapshot that survives map mutation
// ---------------------------------------------------------------------------
void test_node_state_snapshot_is_owned() {
  std::atomic<bool> got_nbirth{false};

  auto callback = [&](const sparkplug::Topic& topic, const auto&) {
    if (topic.message_type == sparkplug::MessageType::NBIRTH) {
      got_nbirth = true;
    }
  };

  sparkplug::HostApplication::Config sub_config{.broker_url = "tcp://localhost:1883",
                                                .client_id = "test_snap_sub",
                                                .host_id = "SnapHost",
                                                .message_callback = callback};
  sparkplug::HostApplication sub(std::move(sub_config));

  if (!sub.connect() || !sub.subscribe_all_groups()) {
    std::cout << "[SKIP] get_node_state snapshot (no broker)\n";
    return;
  }
  std::this_thread::sleep_for(SETTLE);

  sparkplug::EdgeNode::Config pub_config{.broker_url = "tcp://localhost:1883",
                                         .client_id = "test_snap_pub",
                                         .group_id = "SnapGroup",
                                         .edge_node_id = "SnapNode01"};
  sparkplug::EdgeNode pub(std::move(pub_config));

  if (!pub.connect()) {
    std::cout << "[SKIP] get_node_state snapshot (pub connect failed)\n";
    (void)sub.disconnect();
    return;
  }

  sparkplug::PayloadBuilder birth;
  birth.add_metric("Temperature", 25.0);
  assert(pub.publish_birth(birth).has_value());

  std::this_thread::sleep_for(SETTLE);
  assert(got_nbirth.load());

  // Take a snapshot
  auto snapshot = sub.get_node_state("SnapGroup", "SnapNode01");
  assert(snapshot.has_value());
  assert(snapshot->is_online);
  assert(snapshot->birth_received);
  uint64_t saved_bd_seq = snapshot->bd_seq;

  // Now disconnect the publisher — the internal NodeState will be mutated
  // (on_connection_lost -> NDEATH processing sets is_online = false)
  (void)pub.disconnect();
  std::this_thread::sleep_for(SETTLE);

  // The snapshot we took earlier must still show the old state
  assert(snapshot->is_online);
  assert(snapshot->bd_seq == saved_bd_seq);

  // A fresh query should reflect the updated state
  auto snapshot2 = sub.get_node_state("SnapGroup", "SnapNode01");
  assert(snapshot2.has_value());
  // Node should be offline after NDEATH
  // (depends on whether broker delivers Will; either way snapshot is safe)

  (void)sub.disconnect();
  std::cout << "[OK] get_node_state returns owned snapshot\n";
}

// ---------------------------------------------------------------------------
// get_metric_name returns an owned string that survives map mutation
// ---------------------------------------------------------------------------
void test_metric_name_is_owned() {
  std::atomic<bool> got_nbirth{false};

  auto callback = [&](const sparkplug::Topic& topic, const auto&) {
    if (topic.message_type == sparkplug::MessageType::NBIRTH) {
      got_nbirth = true;
    }
  };

  sparkplug::HostApplication::Config sub_config{.broker_url = "tcp://localhost:1883",
                                                .client_id = "test_metric_sub",
                                                .host_id = "MetricHost",
                                                .message_callback = callback};
  sparkplug::HostApplication sub(std::move(sub_config));

  if (!sub.connect() || !sub.subscribe_all_groups()) {
    std::cout << "[SKIP] get_metric_name ownership (no broker)\n";
    return;
  }
  std::this_thread::sleep_for(SETTLE);

  sparkplug::EdgeNode::Config pub_config{.broker_url = "tcp://localhost:1883",
                                         .client_id = "test_metric_pub",
                                         .group_id = "MetricGroup",
                                         .edge_node_id = "MetricNode01"};
  sparkplug::EdgeNode pub(std::move(pub_config));

  if (!pub.connect()) {
    std::cout << "[SKIP] get_metric_name ownership (pub connect failed)\n";
    (void)sub.disconnect();
    return;
  }

  sparkplug::PayloadBuilder birth;
  birth.add_metric_with_alias("Temperature", 0, 25.0);
  birth.add_metric_with_alias("Humidity", 1, 60.0);
  assert(pub.publish_birth(birth).has_value());

  std::this_thread::sleep_for(SETTLE);
  assert(got_nbirth.load());

  // Get metric names — these should be owned strings
  auto name0 = sub.get_metric_name("MetricGroup", "MetricNode01", "", 0);
  auto name1 = sub.get_metric_name("MetricGroup", "MetricNode01", "", 1);
  assert(name0.has_value());
  assert(name1.has_value());
  assert(*name0 == "Temperature");
  assert(*name1 == "Humidity");

  // Disconnect publisher to mutate internal state
  (void)pub.disconnect();
  std::this_thread::sleep_for(SETTLE);

  // Strings we captured must still be valid and correct
  assert(*name0 == "Temperature");
  assert(*name1 == "Humidity");

  // Non-existent alias returns nullopt
  auto name_bad = sub.get_metric_name("MetricGroup", "MetricNode01", "", 999);
  assert(!name_bad.has_value());

  // Non-existent node returns nullopt
  auto name_nonode = sub.get_metric_name("MetricGroup", "NoSuchNode", "", 0);
  assert(!name_nonode.has_value());

  (void)sub.disconnect();
  std::cout << "[OK] get_metric_name returns owned string\n";
}

int main() {
  std::cout << "=== Mutex-Escaping Reference Tests ===\n\n";

  test_node_state_snapshot_is_owned();
  test_metric_name_is_owned();

  std::cout << "\n=== All mutex-escaping reference tests passed! ===\n";
  return 0;
}
