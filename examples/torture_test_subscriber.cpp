// examples/torture_test_subscriber.cpp - Subscriber for torture testing

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <thread>

#include <sparkplug/edge_node.hpp>
#include <sparkplug/host_application.hpp>
#include <sparkplug/payload_builder.hpp>

std::atomic<bool> running{true};
std::atomic<int64_t> messages_received{0};
std::atomic<int64_t> sequence_errors{0};
std::atomic<int64_t> reconnect_count{0};

void signal_handler(int signal) {
  (void)signal;
  std::cout << "\n[SUBSCRIBER] Caught signal, shutting down...\n";
  running = false;
}

enum class NodeSleepState { UNKNOWN, AWAKE, SLEEPING, WAKE_PENDING };

struct NodeStats {
  int64_t birth_count = 0;
  int64_t death_count = 0;
  int64_t data_count = 0;
  uint64_t current_bd_seq = 0;
  uint8_t last_seq = 0;
  NodeSleepState state = NodeSleepState::UNKNOWN;
  std::chrono::steady_clock::time_point last_death_time{};
  std::chrono::steady_clock::time_point last_wake_attempt{};
  int wake_attempt_count = 0;

  bool online() const {
    return state == NodeSleepState::AWAKE;
  }
  bool sleeping() const {
    return state == NodeSleepState::SLEEPING;
  }
  bool wake_pending() const {
    return state == NodeSleepState::WAKE_PENDING;
  }

  const char* state_code() const {
    switch (state) {
    case NodeSleepState::UNKNOWN:
      return "UNK";
    case NodeSleepState::AWAKE:
      return "AWK";
    case NodeSleepState::SLEEPING:
      return "SLP";
    case NodeSleepState::WAKE_PENDING:
      return "WKP";
    }
    return "???";
  }
};

class TortureTestSubscriber {
public:
  TortureTestSubscriber(std::string broker_url, std::string group_id, std::string subscriber_id,
                        bool send_commands, int cycle_interval_sec)
      : broker_url_(std::move(broker_url)), group_id_(std::move(group_id)),
        subscriber_id_(std::move(subscriber_id)), send_commands_(send_commands),
        cycle_interval_sec_(cycle_interval_sec) {
  }

  bool initialize() {
    return connect();
  }

  std::string log_prefix(const std::string& node_id = "") const {
    std::string prefix = "[SUB" + subscriber_id_ + "]";
    if (!node_id.empty()) {
      auto it = node_stats_.find(node_id);
      if (it != node_stats_.end()) {
        prefix += " [" + std::string(it->second.state_code()) + "]";
      }
    }
    return prefix;
  }

  bool connect() {
    std::cout << log_prefix() << " Connecting to broker: " << broker_url_ << "\n";

    if (!command_publisher_) {
      sparkplug::EdgeNode::Config pub_config{.broker_url = broker_url_,
                                             .client_id = "torture_test_cmd_" + subscriber_id_,
                                             .group_id = group_id_,
                                             .edge_node_id = "CommandHost_" + subscriber_id_,
                                             .data_qos = 0,
                                             .death_qos = 1,
                                             .clean_session = true};

      command_publisher_ = std::make_unique<sparkplug::EdgeNode>(std::move(pub_config));
    }

    auto pub_result = command_publisher_->connect();
    if (!pub_result) {
      std::cerr << log_prefix() << " Command publisher connection failed: " << pub_result.error()
                << "\n";
      return false;
    }

    sparkplug::PayloadBuilder birth;
    birth.add_metric("Host Type", "Torture Test Command Host");
    auto birth_result = command_publisher_->publish_birth(birth);
    if (!birth_result) {
      std::cerr << log_prefix() << " Command publisher NBIRTH failed: " << birth_result.error()
                << "\n";
    }

    std::cout << log_prefix() << " Command publisher ready\n";

    sparkplug::HostApplication::Config sub_config{
        .broker_url = broker_url_,
        .client_id = "torture_test_sub_" + subscriber_id_,
        .host_id = group_id_,
        .qos = 1,
        .clean_session = true,
        .validate_sequence = true,
        .message_callback = [this](const sparkplug::Topic& topic,
                                   const org::eclipse::tahu::protobuf::Payload& payload) {
          handle_message(topic, payload);
        }};

    subscriber_ = std::make_unique<sparkplug::HostApplication>(std::move(sub_config));

    auto result = subscriber_->connect();
    if (!result) {
      std::cerr << log_prefix() << " Connection failed: " << result.error() << "\n";
      return false;
    }

    auto subscribe_result = subscriber_->subscribe_group(group_id_);
    if (!subscribe_result) {
      std::cerr << log_prefix() << " Subscribe failed: " << subscribe_result.error() << "\n";
      return false;
    }

    std::cout << log_prefix() << " Connected and subscribed to group: " << group_id_ << "\n";

    if (reconnect_count > 0) {
      std::cout << log_prefix() << " This is reconnection #" << reconnect_count << "\n";
    }

    request_rebirth_for_known_nodes();

    return true;
  }

  void request_rebirth_for_known_nodes() {
    if (node_stats_.empty()) {
      std::cout << log_prefix()
                << " No known nodes yet, will request rebirth as nodes are discovered\n";
      return;
    }

    int awake_count = 0;
    int sleeping_count = 0;
    int unknown_count = 0;

    for (auto& [node_id, stats] : node_stats_) {
      if (stats.online()) {
        awake_count++;
      } else if (stats.sleeping()) {
        sleeping_count++;
      } else {
        unknown_count++;
      }
    }

    std::cout << log_prefix() << " Requesting rebirth from " << node_stats_.size()
              << " known nodes (Sparkplug B 2.2 PRIMARY behavior)\n";
    std::cout << "  Awake: " << awake_count << ", Sleeping: " << sleeping_count
              << ", Unknown: " << unknown_count << "\n";

    for (auto& [node_id, stats] : node_stats_) {
      send_rebirth_command(node_id);

      if (stats.sleeping() || stats.state == NodeSleepState::UNKNOWN) {
        stats.state = NodeSleepState::WAKE_PENDING;
        stats.last_wake_attempt = std::chrono::steady_clock::now();
        stats.wake_attempt_count++;
      }
    }
  }

  void handle_message(const sparkplug::Topic& topic,
                      const org::eclipse::tahu::protobuf::Payload& payload) {
    messages_received++;

    auto& stats = node_stats_[topic.edge_node_id];

    switch (topic.message_type) {
    case sparkplug::MessageType::NBIRTH: {
      stats.birth_count++;

      uint64_t bd_seq = 0;
      for (const auto& metric : payload.metrics()) {
        if (metric.name() == "bdSeq" || metric.name() == "Node Control/bdSeq") {
          bd_seq = metric.long_value();
          break;
        }
      }

      stats.current_bd_seq = bd_seq;
      stats.last_seq = payload.seq();

      auto prev_state = stats.state;
      stats.state = NodeSleepState::AWAKE;
      stats.wake_attempt_count = 0;

      std::cout << log_prefix(topic.edge_node_id) << " NBIRTH from " << topic.edge_node_id
                << " (bdSeq=" << bd_seq << ", seq=" << static_cast<int>(payload.seq())
                << ", metrics=" << payload.metrics_size();
      if (prev_state == NodeSleepState::SLEEPING) {
        std::cout << ", WOKE UP from sleep";
      } else if (prev_state == NodeSleepState::WAKE_PENDING) {
        std::cout << ", wake successful";
      }
      std::cout << ")\n";
      break;
    }

    case sparkplug::MessageType::NDEATH: {
      stats.death_count++;

      uint64_t bd_seq = 0;
      if (payload.has_seq()) {
        bd_seq = payload.seq();
      }

      stats.state = NodeSleepState::SLEEPING;
      stats.last_death_time = std::chrono::steady_clock::now();
      stats.wake_attempt_count = 0;

      std::cout << log_prefix(topic.edge_node_id) << " NDEATH from " << topic.edge_node_id
                << " (bdSeq=" << bd_seq << ") - entering SLEEP mode\n";
      break;
    }

    case sparkplug::MessageType::NDATA: {
      if (stats.sleeping() || stats.state == NodeSleepState::UNKNOWN) {
        std::cout << log_prefix(topic.edge_node_id) << " NDATA from " << topic.edge_node_id
                  << " (seq=" << static_cast<int>(payload.seq())
                  << ") - node alive! Requesting rebirth\n";
        send_rebirth_command(topic.edge_node_id);
        stats.state = NodeSleepState::WAKE_PENDING;
        stats.last_wake_attempt = std::chrono::steady_clock::now();
        stats.wake_attempt_count++;
        return;
      }

      if (stats.wake_pending()) {
        std::cout << log_prefix(topic.edge_node_id) << " NDATA from " << topic.edge_node_id
                  << " (seq=" << static_cast<int>(payload.seq())
                  << ") - waiting for NBIRTH, ignoring\n";
        return;
      }

      if (!stats.online()) {
        std::cerr << log_prefix(topic.edge_node_id) << " NDATA from " << topic.edge_node_id
                  << " in unexpected state, requesting rebirth\n";
        send_rebirth_command(topic.edge_node_id);
        stats.state = NodeSleepState::WAKE_PENDING;
        stats.last_wake_attempt = std::chrono::steady_clock::now();
        stats.wake_attempt_count++;
        return;
      }

      stats.data_count++;

      uint8_t expected_seq = (stats.last_seq + 1) % 256;
      if (payload.seq() != expected_seq && stats.last_seq != 255) {
        std::cerr << log_prefix(topic.edge_node_id) << " SEQUENCE ERROR on " << topic.edge_node_id
                  << ": expected " << static_cast<int>(expected_seq) << ", got "
                  << static_cast<int>(payload.seq()) << "\n";
        sequence_errors++;
      }

      stats.last_seq = payload.seq();

      std::cout << log_prefix(topic.edge_node_id) << " NDATA from " << topic.edge_node_id
                << " (seq=" << static_cast<int>(payload.seq())
                << ", metrics=" << payload.metrics_size() << ", count=" << stats.data_count
                << ")\n";
      break;
    }

    case sparkplug::MessageType::DBIRTH: {
      std::cout << log_prefix() << " DBIRTH from " << topic.edge_node_id << "/" << topic.device_id
                << " (seq=" << static_cast<int>(payload.seq())
                << ", metrics=" << payload.metrics_size() << ")\n";
      break;
    }

    case sparkplug::MessageType::DDATA: {
      std::cout << log_prefix() << " DDATA from " << topic.edge_node_id << "/" << topic.device_id
                << " (seq=" << static_cast<int>(payload.seq())
                << ", metrics=" << payload.metrics_size() << ")\n";
      break;
    }

    case sparkplug::MessageType::DDEATH: {
      std::cout << log_prefix() << " DDEATH from " << topic.edge_node_id << "/" << topic.device_id
                << "\n";
      break;
    }

    default:
      break;
    }
  }

  void check_sleeping_nodes(std::chrono::steady_clock::time_point now) {
    for (auto& [node_id, stats] : node_stats_) {
      if (!stats.sleeping() && !stats.wake_pending()) {
        continue;
      }

      int backoff_seconds = std::min(60, 5 * (1 << stats.wake_attempt_count));

      auto time_since_last_attempt =
          std::chrono::duration_cast<std::chrono::seconds>(now - stats.last_wake_attempt).count();

      if (time_since_last_attempt >= backoff_seconds) {
        if (stats.wake_pending()) {
          std::cout << log_prefix() << " Node " << node_id << " did not respond to wake attempt #"
                    << stats.wake_attempt_count << " (waited " << time_since_last_attempt
                    << "s), back to SLEEPING\n";
          stats.state = NodeSleepState::SLEEPING;
        }

        if (stats.sleeping()) {
          std::cout << log_prefix() << " Attempting to wake sleeping node " << node_id
                    << " (attempt #" << (stats.wake_attempt_count + 1) << ", backoff "
                    << backoff_seconds << "s)\n";
          send_rebirth_command(node_id);
          stats.state = NodeSleepState::WAKE_PENDING;
          stats.last_wake_attempt = now;
          stats.wake_attempt_count++;
        }
      }
    }
  }

  void send_rebirth_command(const std::string& edge_node_id) {
    if (!command_publisher_) {
      return;
    }

    std::cout << log_prefix() << " Sending REBIRTH command to " << edge_node_id << "\n";

    sparkplug::PayloadBuilder cmd;
    cmd.add_metric("Node Control/Rebirth", true);

    auto result = command_publisher_->publish_node_command(edge_node_id, cmd);
    if (!result) {
      std::cerr << log_prefix() << " Failed to send rebirth command: " << result.error() << "\n";
    }
  }

  void send_reboot_command(const std::string& edge_node_id) {
    if (!command_publisher_) {
      return;
    }

    std::cout << log_prefix() << " Sending REBOOT command to " << edge_node_id
              << " (will cause crash)\n";

    sparkplug::PayloadBuilder cmd;
    cmd.add_metric("Node Control/Reboot", true);

    auto result = command_publisher_->publish_node_command(edge_node_id, cmd);
    if (!result) {
      std::cerr << log_prefix() << " Failed to send reboot command: " << result.error() << "\n";
    }
  }

  void run() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> action_dis(0, 100);

    auto last_cycle = std::chrono::steady_clock::now();
    auto last_command = std::chrono::steady_clock::now();

    std::cout << log_prefix() << " Starting monitoring loop...\n";
    if (cycle_interval_sec_ > 0) {
      std::cout << log_prefix() << " Will cycle connection every " << cycle_interval_sec_
                << " seconds\n";
    }
    std::cout << "\n";

    while (running) {
      auto now = std::chrono::steady_clock::now();

      check_sleeping_nodes(now);

      if (send_commands_ &&
          std::chrono::duration_cast<std::chrono::seconds>(now - last_command).count() >= 15) {
        int action = action_dis(gen);

        if (action < 30 && !node_stats_.empty()) {
          auto it = node_stats_.begin();
          std::advance(it, gen() % node_stats_.size());
          if (it->second.online()) {
            send_rebirth_command(it->first);
          }
        } else if (action < 35 && !node_stats_.empty()) {
          auto it = node_stats_.begin();
          std::advance(it, gen() % node_stats_.size());
          if (it->second.online()) {
            send_reboot_command(it->first);
          }
        }

        last_command = now;
      }

      if (cycle_interval_sec_ > 0 &&
          std::chrono::duration_cast<std::chrono::seconds>(now - last_cycle).count() >=
              cycle_interval_sec_) {
        std::cout << "\n" << log_prefix() << " *** CYCLING CONNECTION ***\n";
        disconnect();
        std::this_thread::sleep_for(std::chrono::seconds(2));
        reconnect_count++;
        if (!connect()) {
          std::cerr << log_prefix() << " Reconnection failed\n";
          break;
        }
        last_cycle = std::chrono::steady_clock::now();
        std::cout << "\n";
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  void disconnect() {
    std::cout << log_prefix() << " Disconnecting...\n";

    if (subscriber_) {
      auto sub_result = subscriber_->disconnect();
      if (!sub_result) {
        std::cerr << log_prefix() << " Subscriber disconnect failed: " << sub_result.error()
                  << "\n";
      }
    }

    if (command_publisher_) {
      auto pub_result = command_publisher_->disconnect();
      if (!pub_result) {
        std::cerr << log_prefix() << " Command publisher disconnect failed: " << pub_result.error()
                  << "\n";
      }
    }
  }

  void print_statistics() const {
    std::cout << "\n" << log_prefix() << " Statistics:\n";
    std::cout << "  Total messages received: " << messages_received << "\n";
    std::cout << "  Sequence errors: " << sequence_errors << "\n";
    std::cout << "  Reconnection count: " << reconnect_count << "\n";
    std::cout << "\n  Per-Node Statistics:\n";

    for (const auto& [node_id, stats] : node_stats_) {
      std::string state_str;
      switch (stats.state) {
      case NodeSleepState::UNKNOWN:
        state_str = "UNKNOWN";
        break;
      case NodeSleepState::AWAKE:
        state_str = "AWAKE (online)";
        break;
      case NodeSleepState::SLEEPING:
        state_str = "SLEEPING (waiting for NBIRTH)";
        break;
      case NodeSleepState::WAKE_PENDING:
        state_str = "WAKE_PENDING (rebirth requested)";
        break;
      }

      std::cout << "    " << node_id << ":\n";
      std::cout << "      State: " << state_str << "\n";
      std::cout << "      NBIRTH: " << stats.birth_count << "\n";
      std::cout << "      NDEATH: " << stats.death_count << "\n";
      std::cout << "      NDATA: " << stats.data_count << "\n";
      std::cout << "      Current bdSeq: " << stats.current_bd_seq << "\n";
      std::cout << "      Last seq: " << static_cast<int>(stats.last_seq) << "\n";
      std::cout << "      Wake attempts: " << stats.wake_attempt_count << "\n";
    }
  }

private:
  std::string broker_url_;
  std::string group_id_;
  std::string subscriber_id_;
  bool send_commands_;
  int cycle_interval_sec_;
  std::unique_ptr<sparkplug::HostApplication> subscriber_;
  std::unique_ptr<sparkplug::EdgeNode> command_publisher_;
  std::map<std::string, NodeStats> node_stats_;
};

int main(int argc, char* argv[]) {
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  std::string broker_url = "tcp://localhost:1883";
  std::string group_id = "TortureTest";
  std::string subscriber_id = "01";
  bool send_commands = false;
  int cycle_interval = 0;

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--broker" && i + 1 < argc) {
      broker_url = argv[++i];
    } else if (arg == "--group" && i + 1 < argc) {
      group_id = argv[++i];
    } else if (arg == "--id" && i + 1 < argc) {
      subscriber_id = argv[++i];
    } else if (arg == "--commands") {
      send_commands = true;
    } else if (arg == "--cycle" && i + 1 < argc) {
      cycle_interval = std::stoi(argv[++i]);
    } else if (arg == "--help") {
      std::cout << "Usage: " << argv[0] << " [options]\n";
      std::cout << "Options:\n";
      std::cout << "  --broker <url>    MQTT broker URL (default: tcp://localhost:1883)\n";
      std::cout << "  --group <id>      Sparkplug group ID (default: TortureTest)\n";
      std::cout << "  --id <id>         Subscriber identifier (default: 01)\n";
      std::cout << "  --commands        Enable sending commands to publishers\n";
      std::cout << "  --cycle <sec>     Cycle connection every N seconds (0=never, default: 0)\n";
      std::cout << "  --help            Show this help\n";
      return 0;
    }
  }

  std::cout << "=== Sparkplug Torture Test Subscriber ===\n";
  std::cout << "Broker: " << broker_url << "\n";
  std::cout << "Group: " << group_id << "\n";
  std::cout << "Subscriber ID: " << subscriber_id << "\n";
  std::cout << "Commands: " << (send_commands ? "ENABLED" : "DISABLED") << "\n";
  std::cout << "Connection cycling: "
            << (cycle_interval > 0 ? std::to_string(cycle_interval) + "s" : "DISABLED") << "\n\n";

  TortureTestSubscriber subscriber(broker_url, group_id, subscriber_id, send_commands,
                                   cycle_interval);

  if (!subscriber.initialize()) {
    std::cerr << "[SUBSCRIBER] Initialization failed\n";
    return 1;
  }

  subscriber.run();
  subscriber.disconnect();
  subscriber.print_statistics();

  std::cout << "\n[SUBSCRIBER] Shutdown complete\n";
  return 0;
}
