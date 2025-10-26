// examples/torture_test_publisher.cpp - Publisher for torture testing

#include <atomic>
#include <chrono>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <thread>

#include <sparkplug/payload_builder.hpp>
#include <sparkplug/publisher.hpp>

std::atomic<bool> running{true};
std::atomic<bool> do_rebirth{false};
std::atomic<bool> connection_lost{false};
std::atomic<int64_t> scan_rate_ms{1000};

void signal_handler(int signal) {
  (void)signal;
  std::cout << "\n[PUBLISHER] Caught signal, shutting down gracefully...\n";
  running = false;
}

class TortureTestPublisher {
public:
  TortureTestPublisher(std::string broker_url, std::string group_id, std::string edge_node_id)
      : broker_url_(std::move(broker_url)), group_id_(std::move(group_id)),
        edge_node_id_(std::move(edge_node_id)), message_count_(0), reconnect_count_(0) {
  }

  bool initialize() {
    // Create publisher with command callback
    auto command_callback = [this](const sparkplug::Topic& topic,
                                   const org::eclipse::tahu::protobuf::Payload& payload) {
      handle_command(topic, payload);
    };

    sparkplug::Publisher::Config pub_config{.broker_url = broker_url_,
                                            .client_id = "torture_test_publisher",
                                            .group_id = group_id_,
                                            .edge_node_id = edge_node_id_,
                                            .data_qos = 0,
                                            .death_qos = 1,
                                            .clean_session = true,
                                            .keep_alive_interval = 60,
                                            .tls = {},
                                            .command_callback = command_callback};

    publisher_ = std::make_unique<sparkplug::Publisher>(std::move(pub_config));

    return connect();
  }

  bool connect() {
    std::cout << "[PUBLISHER] Connecting to broker: " << broker_url_ << "\n";

    auto pub_result = publisher_->connect();
    if (!pub_result) {
      std::cerr << "[PUBLISHER] Failed to connect: " << pub_result.error() << "\n";
      return false;
    }

    std::cout << "[PUBLISHER] Connected successfully (NCMD subscribed)\n";

    if (reconnect_count_ > 0) {
      std::cout << "[PUBLISHER] This is reconnection #" << reconnect_count_ << "\n";
    }

    return publish_birth();
  }

  bool publish_birth() {
    sparkplug::PayloadBuilder birth;

    birth.add_metric("bdSeq", static_cast<uint64_t>(publisher_->get_bd_seq()));
    birth.add_node_control_rebirth(false);
    birth.add_node_control_reboot(false);
    birth.add_node_control_scan_rate(scan_rate_ms.load());

    birth.add_metric("Properties/Software", "Torture Test Publisher");
    birth.add_metric("Properties/Version", "1.0.0");
    birth.add_metric("Properties/Reconnects", static_cast<int64_t>(reconnect_count_));

    birth.add_metric_with_alias("Temperature", 1, 20.0);
    birth.add_metric_with_alias("Pressure", 2, 101.3);
    birth.add_metric_with_alias("Humidity", 3, 45.0);
    birth.add_metric_with_alias("Uptime", 4, static_cast<int64_t>(0));
    birth.add_metric_with_alias("MessageCount", 5, static_cast<int64_t>(message_count_));

    auto result = publisher_->publish_birth(birth);
    if (!result) {
      std::cerr << "[PUBLISHER] Failed to publish NBIRTH: " << result.error() << "\n";
      return false;
    }

    std::cout << "[PUBLISHER] Published NBIRTH (bdSeq=" << publisher_->get_bd_seq()
              << ", seq=" << publisher_->get_seq() << ")\n";

    return publish_device_births();
  }

  bool publish_device_births() {
    sparkplug::PayloadBuilder motor_birth;
    motor_birth.add_metric_with_alias("RPM", 1, 1500.0);
    motor_birth.add_metric_with_alias("Running", 2, true);
    motor_birth.add_metric_with_alias("Temperature", 3, 65.0);

    auto motor_result = publisher_->publish_device_birth("Motor01", motor_birth);
    if (!motor_result) {
      std::cerr << "[PUBLISHER] Failed to publish DBIRTH for Motor01: " << motor_result.error()
                << "\n";
      return false;
    }
    std::cout << "[PUBLISHER] Published DBIRTH for Motor01 (seq=" << publisher_->get_seq() << ")\n";

    sparkplug::PayloadBuilder sensor_birth;
    sensor_birth.add_metric_with_alias("Level", 1, 75.5);
    sensor_birth.add_metric_with_alias("Flow", 2, 120.0);

    auto sensor_result = publisher_->publish_device_birth("Sensor01", sensor_birth);
    if (!sensor_result) {
      std::cerr << "[PUBLISHER] Failed to publish DBIRTH for Sensor01: " << sensor_result.error()
                << "\n";
      return false;
    }
    std::cout << "[PUBLISHER] Published DBIRTH for Sensor01 (seq=" << publisher_->get_seq()
              << ")\n";

    return true;
  }

  void handle_command(const sparkplug::Topic& topic,
                      const org::eclipse::tahu::protobuf::Payload& payload) {
    std::cout << "[PUBLISHER] Received command: " << topic.to_string() << "\n";

    if (topic.message_type == sparkplug::MessageType::NCMD) {
      for (const auto& metric : payload.metrics()) {
        std::cout << "[PUBLISHER]   Command: " << metric.name() << "\n";

        if (metric.name() == "Node Control/Rebirth" && metric.boolean_value()) {
          std::cout << "[PUBLISHER]   -> Rebirth requested\n";
          do_rebirth = true;
        } else if (metric.name() == "Node Control/Scan Rate") {
          auto new_rate = static_cast<int64_t>(metric.long_value());
          std::cout << "[PUBLISHER]   -> Scan rate changed to " << new_rate << "ms\n";
          scan_rate_ms = new_rate;
        } else if (metric.name() == "Node Control/Reboot" && metric.boolean_value()) {
          std::cout << "[PUBLISHER]   -> Reboot requested (simulating crash in 2s...)\n";
          std::thread([]() {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            std::cout << "[PUBLISHER] CRASH SIMULATION (exit without graceful shutdown)\n";
            std::_Exit(0);
          }).detach();
        }
      }
    }
  }

  void run() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0.0, 1.0);

    double temperature = 20.0;
    double pressure = 101.3;
    double humidity = 45.0;
    int64_t uptime = 0;

    auto start_time = std::chrono::steady_clock::now();

    std::cout << "[PUBLISHER] Starting data publishing loop...\n";
    std::cout << "[PUBLISHER] Send SIGINT (Ctrl+C) for graceful shutdown\n";
    std::cout << "[PUBLISHER] Send NCMD 'Node Control/Reboot' for ungraceful crash\n\n";

    while (running) {
      if (do_rebirth) {
        std::cout << "\n[PUBLISHER] *** EXECUTING REBIRTH ***\n";
        auto rebirth_result = publisher_->rebirth();
        if (!rebirth_result) {
          std::cerr << "[PUBLISHER] Rebirth failed: " << rebirth_result.error() << "\n";
        } else {
          std::cout << "[PUBLISHER] Rebirth complete (new bdSeq=" << publisher_->get_bd_seq()
                    << ")\n";
          if (!publish_device_births()) {
            std::cerr << "[PUBLISHER] Failed to publish device births after rebirth\n";
          }
          std::cout << "\n";
        }
        do_rebirth = false;
      }

      temperature += dis(gen) - 0.5;
      pressure += (dis(gen) - 0.5) * 0.1;
      humidity += (dis(gen) - 0.5) * 2.0;
      uptime = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() -
                                                                start_time)
                   .count();

      sparkplug::PayloadBuilder data;
      data.add_metric_by_alias(1, temperature);
      data.add_metric_by_alias(2, pressure);
      data.add_metric_by_alias(3, humidity);
      data.add_metric_by_alias(4, uptime);
      data.add_metric_by_alias(5, static_cast<int64_t>(message_count_));

      auto result = publisher_->publish_data(data);
      if (!result) {
        std::cerr << "[PUBLISHER] Failed to publish NDATA: " << result.error() << "\n";
        connection_lost = true;
        break;
      }

      message_count_++;

      if (message_count_ % 10 == 0) {
        std::cout << "[PUBLISHER] Messages: " << message_count_
                  << ", Seq: " << publisher_->get_seq() << ", Temp: " << std::fixed
                  << std::setprecision(1) << temperature << "C\n";
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(scan_rate_ms.load()));
    }
  }

  void disconnect() {
    std::cout << "\n[PUBLISHER] Disconnecting...\n";

    if (publisher_) {
      auto death_result = publisher_->publish_death();
      if (!death_result) {
        std::cerr << "[PUBLISHER] Failed to publish NDEATH: " << death_result.error() << "\n";
      } else {
        std::cout << "[PUBLISHER] Published NDEATH (bdSeq=" << publisher_->get_bd_seq() << ")\n";
      }

      auto pub_result = publisher_->disconnect();
      if (!pub_result) {
        std::cerr << "[PUBLISHER] Publisher disconnect failed: " << pub_result.error() << "\n";
      } else {
        std::cout << "[PUBLISHER] Disconnected gracefully\n";
      }
    }

    print_statistics();
  }

  void reconnect() {
    reconnect_count_++;
    std::cout << "\n[PUBLISHER] *** RECONNECTION ATTEMPT #" << reconnect_count_ << " ***\n";

    disconnect();

    std::this_thread::sleep_for(std::chrono::seconds(2));

    if (connect()) {
      connection_lost = false;
    } else {
      std::cerr << "[PUBLISHER] Reconnection failed, will retry...\n";
    }
  }

  void print_statistics() const {
    std::cout << "\n[PUBLISHER] Session Statistics:\n";
    std::cout << "  Total messages published: " << message_count_ << "\n";
    std::cout << "  Reconnection count: " << reconnect_count_ << "\n";
    std::cout << "  Final sequence: " << publisher_->get_seq() << "\n";
    std::cout << "  Final bdSeq: " << publisher_->get_bd_seq() << "\n";
  }

private:
  std::string broker_url_;
  std::string group_id_;
  std::string edge_node_id_;

  std::unique_ptr<sparkplug::Publisher> publisher_;

  std::atomic<int64_t> message_count_;
  std::atomic<int> reconnect_count_;
};

int main(int argc, char* argv[]) {
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  std::string broker_url = "tcp://localhost:1883";
  std::string group_id = "TortureTest";
  std::string edge_node_id = "Publisher01";

  if (argc > 1) {
    broker_url = argv[1];
  }
  if (argc > 2) {
    group_id = argv[2];
  }
  if (argc > 3) {
    edge_node_id = argv[3];
  }

  std::cout << "=== Sparkplug Torture Test Publisher ===\n";
  std::cout << "Broker: " << broker_url << "\n";
  std::cout << "Group: " << group_id << "\n";
  std::cout << "Edge Node: " << edge_node_id << "\n\n";

  TortureTestPublisher publisher(broker_url, group_id, edge_node_id);

  if (!publisher.initialize()) {
    std::cerr << "[PUBLISHER] Initialization failed\n";
    return 1;
  }

  while (running) {
    publisher.run();

    if (connection_lost && running) {
      publisher.reconnect();
    }
  }

  publisher.disconnect();

  std::cout << "\n[PUBLISHER] Shutdown complete\n";
  return 0;
}
