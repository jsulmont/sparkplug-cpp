// tests/soak_test.cpp
// Self-contained soak test. Requires a local MQTT broker (mosquitto).
// Usage: ./soak_test [duration_seconds]   (default: 60)
//
// Exercises the full lifecycle: create, connect, publish, rebirth,
// disconnect, destroy, recreate — in a loop for all components.

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <format>
#include <future>
#include <iostream>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#include <sparkplug/edge_node.hpp>
#include <sparkplug/host_application.hpp>

static constexpr const char* BROKER = "tcp://localhost:1883";
static constexpr int NUM_PUBLISHERS = 3;
static constexpr int PUBLISH_INTERVAL_MS = 10;
static constexpr int REBIRTH_INTERVAL_S = 10;
static constexpr int FULL_RESTART_INTERVAL_S = 30;
static constexpr int STATE_INTERVAL_S = 5;
static constexpr int HOST_RESTART_INTERVAL_S = 45;
static constexpr int STATS_INTERVAL_S = 5;

struct Stats {
  std::atomic<int64_t> msgs_sent{0};
  std::atomic<int64_t> msgs_received{0};
  std::atomic<int64_t> births_sent{0};
  std::atomic<int64_t> births_received{0};
  std::atomic<int64_t> rebirths{0};
  std::atomic<int64_t> full_restarts{0};
  std::atomic<int64_t> host_restarts{0};
  std::atomic<int64_t> state_publishes{0};
  std::atomic<int64_t> snapshot_checks{0};
  std::atomic<int64_t> alias_checks{0};
  std::atomic<int64_t> publish_errors{0};
  std::atomic<int64_t> sequence_errors{0};
  std::atomic<int64_t> invariant_violations{0};
};

static Stats stats;
static std::atomic<bool> running{true};

static std::mutex log_mutex;

static void log_error(const std::string& msg) {
  std::scoped_lock lock(log_mutex);
  std::cerr << "[ERROR] " << msg << "\n";
}

static void log_info(const std::string& msg) {
  std::scoped_lock lock(log_mutex);
  std::cout << msg << "\n";
}

static auto elapsed_s(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::steady_clock::now() - start)
      .count();
}

static bool wait_for_host(sparkplug::EdgeNode& node, const std::string& node_id) {
  for (int i = 0; i < 50 && running && !node.is_primary_host_online(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (!node.is_primary_host_online()) {
    log_error(std::format("{}: primary host never came online", node_id));
    return false;
  }
  return true;
}

static bool do_birth(sparkplug::EdgeNode& node, const std::string& node_id) {
  sparkplug::PayloadBuilder birth;
  birth.add_metric_with_alias("Temperature", 0, 20.0);
  birth.add_metric_with_alias("Pressure", 1, 101.3);
  birth.add_metric_with_alias("Counter", 2, static_cast<int64_t>(0));
  birth.add_node_control_rebirth(false);
  auto r = node.publish_birth(birth);
  if (!r) {
    log_error(std::format("{}: birth failed: {}", node_id, r.error()));
    stats.publish_errors++;
    return false;
  }
  stats.births_sent++;
  return true;
}

// ---- Publisher thread ----
// Cycles through: create -> connect -> birth -> publish -> rebirth ->
// ... -> disconnect -> destroy -> recreate
static void publisher_thread(int id, int duration_s) {
  std::string node_id = std::format("SoakNode{:02d}", id);
  std::string client_id = std::format("soak_pub_{}", id);

  std::mt19937 gen(std::random_device{}());
  std::uniform_real_distribution<> jitter(-0.5, 0.5);

  auto global_start = std::chrono::steady_clock::now();
  int64_t counter = 0;

  while (running && elapsed_s(global_start) < duration_s) {
    // -- Create and connect --
    sparkplug::EdgeNode::Config config{.broker_url = BROKER,
                                       .client_id = client_id,
                                       .group_id = "SoakGroup",
                                       .edge_node_id = node_id,
                                       .data_qos = 0,
                                       .death_qos = 1,
                                       .clean_session = true,
                                       .primary_host_id = "SoakHost"};

    auto node = std::make_unique<sparkplug::EdgeNode>(std::move(config));

    if (!node->connect()) {
      log_error(std::format("{}: connect failed", node_id));
      stats.publish_errors++;
      std::this_thread::sleep_for(std::chrono::seconds(1));
      continue;
    }

    if (!wait_for_host(*node, node_id)) {
      (void)node->disconnect();
      continue;
    }

    if (!do_birth(*node, node_id)) {
      (void)node->disconnect();
      continue;
    }

    // -- Publish loop until it's time for rebirth or full restart --
    auto session_start = std::chrono::steady_clock::now();
    auto last_rebirth = session_start;
    while (running && elapsed_s(global_start) < duration_s) {
      auto now = std::chrono::steady_clock::now();
      auto session_age =
          std::chrono::duration_cast<std::chrono::seconds>(now - session_start).count();

      // Full restart: destroy and recreate the entire object
      if (session_age >= FULL_RESTART_INTERVAL_S + id * 5) {
        log_info(std::format("{}: full restart (destroy + recreate)", node_id));
        (void)node->disconnect();
        node.reset(); // destroy
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        stats.full_restarts++;
        break;
      }

      // Rebirth (disconnect+reconnect, same object)
      auto since_rebirth =
          std::chrono::duration_cast<std::chrono::seconds>(now - last_rebirth).count();
      if (since_rebirth >= REBIRTH_INTERVAL_S + id * 3) {
        auto r = node->rebirth();
        if (r) {
          stats.rebirths++;
          stats.births_sent++;
        } else {
          log_error(std::format("{}: rebirth failed: {}", node_id, r.error()));
          stats.publish_errors++;
        }
        last_rebirth = now;

        if (!wait_for_host(*node, node_id))
          break;
        continue;
      }

      // Normal NDATA publish
      sparkplug::PayloadBuilder data;
      data.add_metric_by_alias(0, 20.0 + jitter(gen));
      data.add_metric_by_alias(1, 101.3 + jitter(gen) * 0.1);
      data.add_metric_by_alias(2, ++counter);

      auto r = node->publish_data(data);
      if (r) {
        stats.msgs_sent++;
      } else {
        stats.publish_errors++;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(PUBLISH_INTERVAL_MS));
    }

    // Clean up if we didn't already destroy
    if (node) {
      (void)node->disconnect();
    }
  }
}

// ---- Host thread ----
// Receives messages, validates sequences, publishes STATE periodically.
// Periodically tears down and recreates the entire HostApplication.
static void host_thread(int duration_s) {
  std::mutex state_mutex;
  struct NodeTrack {
    uint64_t last_seq{255};
    uint64_t bd_seq{0};
    int64_t birth_count{0};
    bool online{false};
  };
  std::unordered_map<std::string, NodeTrack> nodes;

  auto callback = [&](const sparkplug::Topic& topic,
                      const org::eclipse::tahu::protobuf::Payload& payload) {
    stats.msgs_received++;

    std::scoped_lock lock(state_mutex);
    auto& nt = nodes[topic.edge_node_id];

    if (topic.message_type == sparkplug::MessageType::NBIRTH) {
      stats.births_received++;
      nt.birth_count++;
      nt.last_seq = 0;
      nt.online = true;

      for (const auto& m : payload.metrics()) {
        if (m.name() == "bdSeq") {
          uint64_t new_bd = m.long_value();
          if (nt.birth_count > 1 && new_bd <= nt.bd_seq) {
            // bdSeq going backwards means the node was destroyed and
            // recreated (new session). Only flag it if bdSeq is strictly
            // equal within what looks like the same session (rebirth).
            if (new_bd == nt.bd_seq) {
              log_error(std::format("{}: bdSeq did not increment on rebirth: {}",
                                    topic.edge_node_id, new_bd));
              stats.invariant_violations++;
            }
            // Otherwise it's a full restart — reset tracker silently.
          }
          nt.bd_seq = new_bd;
          break;
        }
      }
    } else if (topic.message_type == sparkplug::MessageType::NDEATH) {
      nt.online = false;
    } else if (topic.message_type == sparkplug::MessageType::NDATA) {
      if (nt.online && nt.last_seq != 255) {
        uint64_t expected = (nt.last_seq + 1) % 256;
        if (payload.seq() != expected) {
          stats.sequence_errors++;
        }
      }
      nt.last_seq = payload.seq();
    }
  };

  auto ts_now = []() -> uint64_t {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count());
  };

  auto global_start = std::chrono::steady_clock::now();

  while (running && elapsed_s(global_start) < duration_s) {
    // -- Create and connect --
    sparkplug::HostApplication::Config config{.broker_url = BROKER,
                                              .client_id = "soak_host",
                                              .host_id = "SoakHost",
                                              .qos = 1,
                                              .clean_session = true,
                                              .validate_sequence = true,
                                              .message_callback = callback};

    auto host = std::make_unique<sparkplug::HostApplication>(std::move(config));

    if (!host->connect()) {
      log_error("Host: connect failed");
      std::this_thread::sleep_for(std::chrono::seconds(1));
      continue;
    }
    if (!host->subscribe_all_groups()) {
      log_error("Host: subscribe failed");
      (void)host->disconnect();
      continue;
    }

    auto r = host->publish_state_birth(ts_now());
    if (!r) {
      log_error(std::format("Host: STATE birth failed: {}", r.error()));
    }
    stats.state_publishes++;

    auto session_start = std::chrono::steady_clock::now();
    auto last_state = session_start;

    while (running && elapsed_s(global_start) < duration_s) {
      auto now = std::chrono::steady_clock::now();
      auto session_age =
          std::chrono::duration_cast<std::chrono::seconds>(now - session_start).count();

      // Full host restart
      if (session_age >= HOST_RESTART_INTERVAL_S) {
        log_info("Host: full restart (destroy + recreate)");
        (void)host->publish_state_death(ts_now());
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        (void)host->disconnect();
        host.reset();
        stats.host_restarts++;
        std::this_thread::sleep_for(std::chrono::seconds(1));
        break;
      }

      // Periodic STATE cycle
      auto since_state =
          std::chrono::duration_cast<std::chrono::seconds>(now - last_state).count();
      if (since_state >= STATE_INTERVAL_S) {
        (void)host->publish_state_death(ts_now());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        (void)host->publish_state_birth(ts_now());
        stats.state_publishes += 2;
        last_state = now;
      }

      // Check snapshots and alias resolution
      for (int i = 0; i < NUM_PUBLISHERS; ++i) {
        std::string node_id = std::format("SoakNode{:02d}", i);

        auto snap = host->get_node_state("SoakGroup", node_id);
        if (snap) {
          stats.snapshot_checks++;
          if (snap->is_online && !snap->birth_received) {
            log_error(std::format("{}: online but birth_received=false", node_id));
            stats.invariant_violations++;
          }
        }

        auto name = host->get_metric_name("SoakGroup", node_id, "", 0);
        if (name) {
          stats.alias_checks++;
          if (*name != "Temperature") {
            log_error(std::format("{}: alias 0 = '{}', expected 'Temperature'", node_id,
                                  *name));
            stats.invariant_violations++;
          }
        }
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Clean up if we didn't already destroy
    if (host) {
      (void)host->publish_state_death(ts_now());
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      (void)host->disconnect();
    }
  }
}

static void print_stats(int elapsed) {
  auto line = std::format(
      "[{:3d}s] sent={} recv={} births={}/{} rebirths={} restarts={}+{} state={} "
      "snap={} alias={} err={} seq_err={} violations={}",
      elapsed, stats.msgs_sent.load(), stats.msgs_received.load(),
      stats.births_sent.load(), stats.births_received.load(), stats.rebirths.load(),
      stats.full_restarts.load(), stats.host_restarts.load(),
      stats.state_publishes.load(), stats.snapshot_checks.load(),
      stats.alias_checks.load(), stats.publish_errors.load(),
      stats.sequence_errors.load(), stats.invariant_violations.load());
  log_info(line);
}

int main(int argc, char* argv[]) {
  int duration_s = 60;
  if (argc > 1) {
    duration_s = std::atoi(argv[1]);
    if (duration_s <= 0)
      duration_s = 60;
  }

  std::cout << std::format("=== Soak Test ({} publishers, {}s) ===\n\n", NUM_PUBLISHERS,
                           duration_s);

  auto host_future = std::async(std::launch::async, host_thread, duration_s);

  std::this_thread::sleep_for(std::chrono::seconds(1));

  std::vector<std::future<void>> pub_futures;
  pub_futures.reserve(NUM_PUBLISHERS);
  for (int i = 0; i < NUM_PUBLISHERS; ++i) {
    pub_futures.push_back(
        std::async(std::launch::async, publisher_thread, i, duration_s));
  }

  auto start = std::chrono::steady_clock::now();
  while (running) {
    std::this_thread::sleep_for(std::chrono::seconds(STATS_INTERVAL_S));
    auto elapsed = elapsed_s(start);
    print_stats(static_cast<int>(elapsed));
    if (elapsed >= duration_s) {
      running = false;
      break;
    }
  }

  for (auto& f : pub_futures)
    f.get();
  host_future.get();

  std::cout << "\n=== Final Results ===\n";
  print_stats(duration_s);

  bool passed = true;
  if (stats.invariant_violations > 0) {
    std::cerr << "\nFAIL: " << stats.invariant_violations << " invariant violations\n";
    passed = false;
  }
  if (stats.sequence_errors > 0) {
    std::cout << "\nWARN: " << stats.sequence_errors
              << " sequence errors (may be expected around rebirth/restart windows)\n";
  }
  if (stats.msgs_received == 0) {
    std::cerr << "\nFAIL: no messages received\n";
    passed = false;
  }
  if (stats.births_received == 0) {
    std::cerr << "\nFAIL: no births received\n";
    passed = false;
  }
  if (stats.snapshot_checks == 0) {
    std::cerr << "\nFAIL: no snapshot checks performed\n";
    passed = false;
  }
  if (stats.alias_checks == 0) {
    std::cerr << "\nFAIL: no alias checks performed\n";
    passed = false;
  }
  if (stats.full_restarts == 0 && duration_s >= 60) {
    std::cerr << "\nFAIL: no full publisher restarts occurred\n";
    passed = false;
  }

  std::cout << "\n" << (passed ? "PASS" : "FAIL") << "\n";
  return passed ? 0 : 1;
}
