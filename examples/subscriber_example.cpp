// examples/subscriber_example.cpp
#include <atomic>
#include <csignal>
#include <iostream>
#include <thread>
#include <utility>

#include <sparkplug/datatype.hpp>
#include <sparkplug/host_application.hpp>

std::atomic<bool> running{true};

void signal_handler(int signal) {
  (void)signal;
  running = false;
}

void print_metric(const org::eclipse::tahu::protobuf::Payload::Metric& metric) {
  std::cout << "    " << metric.name() << " = ";

  switch (metric.datatype()) {
  case std::to_underlying(sparkplug::DataType::Int32):
    std::cout << metric.int_value();
    break;
  case std::to_underlying(sparkplug::DataType::Int64):
    std::cout << metric.long_value();
    break;
  case std::to_underlying(sparkplug::DataType::UInt32):
    std::cout << metric.int_value();
    break;
  case std::to_underlying(sparkplug::DataType::UInt64):
    std::cout << metric.long_value();
    break;
  case std::to_underlying(sparkplug::DataType::Float):
    std::cout << metric.float_value();
    break;
  case std::to_underlying(sparkplug::DataType::Double):
    std::cout << metric.double_value();
    break;
  case std::to_underlying(sparkplug::DataType::Boolean):
    std::cout << (metric.boolean_value() ? "true" : "false");
    break;
  case std::to_underlying(sparkplug::DataType::String):
    std::cout << "\"" << metric.string_value() << "\"";
    break;
  default:
    std::cout << "<unsupported type>";
  }

  std::cout << "\n";
}

int main() {
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  // Optional: Set up logging callback to see library warnings
  auto log_callback = [](sparkplug::LogLevel level, std::string_view message) {
    const char* level_str = "UNKNOWN";
    switch (level) {
    case sparkplug::LogLevel::DEBUG:
      level_str = "DEBUG";
      break;
    case sparkplug::LogLevel::INFO:
      level_str = "INFO";
      break;
    case sparkplug::LogLevel::WARN:
      level_str = "WARN";
      break;
    case sparkplug::LogLevel::ERROR:
      level_str = "ERROR";
      break;
    }
    std::cerr << "[" << level_str << "] " << message << "\n";
  };

  auto message_handler = [](const sparkplug::Topic& topic,
                            const org::eclipse::tahu::protobuf::Payload& payload) {
    std::cout << "\n=== Message Received ===\n";
    std::cout << "Topic: " << topic.to_string() << "\n";
    std::cout << "Group: " << topic.group_id << "\n";
    std::cout << "Edge Node: " << topic.edge_node_id << "\n";
    if (!topic.device_id.empty()) {
      std::cout << "Device: " << topic.device_id << "\n";
    }

    if (payload.has_timestamp()) {
      std::cout << "Timestamp: " << payload.timestamp() << "\n";
    }
    if (payload.has_seq()) {
      std::cout << "Sequence: " << payload.seq() << "\n";
    }

    std::cout << "Metrics (" << payload.metrics_size() << "):\n";
    for (const auto& metric : payload.metrics()) {
      print_metric(metric);
    }
    std::cout << "=======================\n";
  };

  sparkplug::HostApplication::Config config{.broker_url = "tcp://localhost:1883",
                                            .client_id = "sparkplug_subscriber_example",
                                            .host_id = "SubscriberExample",
                                            .message_callback = message_handler,
                                            .log_callback = log_callback};

  sparkplug::HostApplication subscriber(std::move(config));

  auto connect_result = subscriber.connect();
  if (!connect_result) {
    std::cerr << "Failed to connect: " << connect_result.error() << "\n";
    return 1;
  }

  std::cout << "Connected to broker\n";

  auto subscribe_result = subscriber.subscribe_all_groups();
  if (!subscribe_result) {
    std::cerr << "Failed to subscribe: " << subscribe_result.error() << "\n";
    return 1;
  }

  std::cout << "Subscribed to all Sparkplug messages (all groups)\n";
  std::cout << "Press Ctrl+C to exit...\n\n";

  while (running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::cout << "\nShutting down...\n";

  auto disconnect_result = subscriber.disconnect();
  if (!disconnect_result) {
    std::cerr << "Failed to disconnect: " << disconnect_result.error() << "\n";
  }

  std::cout << "Disconnected\n";

  return 0;
}