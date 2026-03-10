// tests/test_move_constructor_race.cpp
// Verify move constructors acquire the source mutex before reading members.
//
// The functional tests verify state transfer.  The contention tests have a
// background thread repeatedly calling a lock-acquiring method on the source
// while the main thread moves.  Under TSan, the old (lock-after-read) code
// would flag a data race; the fixed code is clean.

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

#include <sparkplug/edge_node.hpp>
#include <sparkplug/host_application.hpp>

// ---------------------------------------------------------------------------
// EdgeNode: verify state transfers correctly through move
// ---------------------------------------------------------------------------
void test_edge_node_move_transfers_state() {
  sparkplug::EdgeNode::Config config{.broker_url = "tcp://localhost:1883",
                                     .client_id = "test_move_en",
                                     .group_id = "Test",
                                     .edge_node_id = "MoveNode01"};

  sparkplug::EdgeNode node(std::move(config));

  assert(node.get_seq() == 0);
  assert(node.get_bd_seq() == 0);

  sparkplug::EdgeNode moved(std::move(node));

  assert(moved.get_seq() == 0);
  assert(moved.get_bd_seq() == 0);

  std::cout << "[OK] EdgeNode move constructor transfers state correctly\n";
}

// ---------------------------------------------------------------------------
// EdgeNode: move under mutex contention
// A reader thread repeatedly calls get_seq() (which locks mutex_) while
// the main thread moves.  The move constructor must lock before reading,
// so no data race occurs (verifiable under TSan).
// ---------------------------------------------------------------------------
void test_edge_node_move_under_contention() {
  sparkplug::EdgeNode::Config config{.broker_url = "tcp://localhost:1883",
                                     .client_id = "test_move_race_en",
                                     .group_id = "Test",
                                     .edge_node_id = "MoveNode02"};

  sparkplug::EdgeNode node(std::move(config));

  std::atomic<bool> stop{false};

  std::thread reader([&] {
    while (!stop.load(std::memory_order_relaxed)) {
      [[maybe_unused]] auto seq = node.get_seq();
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(1));

  sparkplug::EdgeNode moved(std::move(node));

  stop.store(true, std::memory_order_relaxed);
  reader.join();

  assert(moved.get_seq() == 0);
  assert(moved.get_bd_seq() == 0);

  std::cout << "[OK] EdgeNode move under contention: no race\n";
}

// ---------------------------------------------------------------------------
// EdgeNode: move assignment under contention
// ---------------------------------------------------------------------------
void test_edge_node_move_assign_under_contention() {
  sparkplug::EdgeNode::Config config_src{.broker_url = "tcp://localhost:1883",
                                         .client_id = "test_move_assign_src",
                                         .group_id = "Test",
                                         .edge_node_id = "MoveNode03"};
  sparkplug::EdgeNode::Config config_dst{.broker_url = "tcp://localhost:1883",
                                         .client_id = "test_move_assign_dst",
                                         .group_id = "Test",
                                         .edge_node_id = "MoveNode04"};

  sparkplug::EdgeNode src(std::move(config_src));
  sparkplug::EdgeNode dst(std::move(config_dst));

  std::atomic<bool> stop{false};

  std::thread reader([&] {
    while (!stop.load(std::memory_order_relaxed)) {
      [[maybe_unused]] auto seq = src.get_seq();
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(1));

  dst = std::move(src);

  stop.store(true, std::memory_order_relaxed);
  reader.join();

  assert(dst.get_seq() == 0);

  std::cout << "[OK] EdgeNode move assignment under contention: no race\n";
}

// ---------------------------------------------------------------------------
// HostApplication: verify state transfers correctly through move
// ---------------------------------------------------------------------------
void test_host_application_move_transfers_state() {
  auto callback = [](const sparkplug::Topic&, const auto&) {};

  sparkplug::HostApplication::Config config{.broker_url = "tcp://localhost:1883",
                                            .client_id = "test_move_ha",
                                            .host_id = "MoveHost01",
                                            .message_callback = callback};

  sparkplug::HostApplication host(std::move(config));

  auto state = host.get_node_state("g", "n");
  assert(!state.has_value());

  sparkplug::HostApplication moved(std::move(host));

  auto state2 = moved.get_node_state("g", "n");
  assert(!state2.has_value());

  std::cout << "[OK] HostApplication move constructor transfers state correctly\n";
}

// ---------------------------------------------------------------------------
// HostApplication: move under mutex contention
// ---------------------------------------------------------------------------
void test_host_application_move_under_contention() {
  auto callback = [](const sparkplug::Topic&, const auto&) {};

  sparkplug::HostApplication::Config config{.broker_url = "tcp://localhost:1883",
                                            .client_id = "test_move_race_ha",
                                            .host_id = "MoveHost02",
                                            .message_callback = callback};

  sparkplug::HostApplication host(std::move(config));

  std::atomic<bool> stop{false};

  std::thread reader([&] {
    while (!stop.load(std::memory_order_relaxed)) {
      [[maybe_unused]] auto s = host.get_node_state("g", "n");
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(1));

  sparkplug::HostApplication moved(std::move(host));

  stop.store(true, std::memory_order_relaxed);
  reader.join();

  std::cout << "[OK] HostApplication move under contention: no race\n";
}

int main() {
  std::cout << "=== Move Constructor Race Tests ===\n\n";

  test_edge_node_move_transfers_state();
  test_edge_node_move_under_contention();
  test_edge_node_move_assign_under_contention();
  test_host_application_move_transfers_state();
  test_host_application_move_under_contention();

  std::cout << "\n=== All move constructor race tests passed! ===\n";
  return 0;
}
