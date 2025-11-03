// examples/subscriber_example_debug.cpp
#include <atomic>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <thread>
#include <utility>

#include <sparkplug/datatype.hpp>
#include <sparkplug/host_application.hpp>

std::atomic<bool> running{true};
std::atomic<int> message_count{0};

void signal_handler(int signal) {
  (void)signal;
  running = false;
}

void print_metric(const org::eclipse::tahu::protobuf::Payload::Metric& metric) {
  std::cout << "    ";

  if (metric.has_name() && !metric.name().empty()) {
    std::cout << metric.name();
  } else if (metric.has_alias()) {
    std::cout << "[alias:" << metric.alias() << "]";
  } else {
    std::cout << "[unnamed]";
  }

  std::cout << " = ";

  switch (metric.datatype()) {
  case std::to_underlying(sparkplug::DataType::Int32):
  case std::to_underlying(sparkplug::DataType::UInt32):
    std::cout << metric.int_value();
    break;
  case std::to_underlying(sparkplug::DataType::Int64):
  case std::to_underlying(sparkplug::DataType::UInt64):
    std::cout << metric.long_value();
    break;
  case std::to_underlying(sparkplug::DataType::Float):
    std::cout << std::fixed << std::setprecision(2) << metric.float_value();
    break;
  case std::to_underlying(sparkplug::DataType::Double):
    std::cout << std::fixed << std::setprecision(2) << metric.double_value();
    break;
  case std::to_underlying(sparkplug::DataType::Boolean):
    std::cout << (metric.boolean_value() ? "true" : "false");
    break;
  case std::to_underlying(sparkplug::DataType::String):
    std::cout << "\"" << metric.string_value() << "\"";
    break;
  default:
    std::cout << "<unsupported type " << metric.datatype() << ">";
  }

  if (metric.has_timestamp()) {
    std::cout << " [ts:" << metric.timestamp() << "]";
  }

  std::cout << "\n";
}

const char* message_type_name(sparkplug::MessageType type) {
  switch (type) {
  case sparkplug::MessageType::NBIRTH:
    return "NBIRTH";
  case sparkplug::MessageType::NDEATH:
    return "NDEATH";
  case sparkplug::MessageType::DBIRTH:
    return "DBIRTH";
  case sparkplug::MessageType::DDEATH:
    return "DDEATH";
  case sparkplug::MessageType::NDATA:
    return "NDATA";
  case sparkplug::MessageType::DDATA:
    return "DDATA";
  case sparkplug::MessageType::NCMD:
    return "NCMD";
  case sparkplug::MessageType::DCMD:
    return "DCMD";
  case sparkplug::MessageType::STATE:
    return "STATE";
  default:
    return "UNKNOWN";
  }
}

int main() {
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  auto message_handler = [](const sparkplug::Topic& topic,
                            const org::eclipse::tahu::protobuf::Payload& payload) {
    int count = ++message_count;

    std::cout << "\n╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║ Message #" << std::setw(3) << count << " - " << std::setw(7)
              << message_type_name(topic.message_type) << std::string(39, ' ') << "║\n";
    std::cout << "╠════════════════════════════════════════════════════════════╣\n";

    std::cout << "║ Topic: " << std::left << std::setw(51) << topic.to_string() << "║\n";
    std::cout << "║ Group: " << std::setw(51) << topic.group_id << "║\n";
    std::cout << "║ Edge Node: " << std::setw(47) << topic.edge_node_id << "║\n";

    if (!topic.device_id.empty()) {
      std::cout << "║ Device: " << std::setw(50) << topic.device_id << "║\n";
    }

    if (payload.has_timestamp()) {
      std::cout << "║ Payload Timestamp: " << std::setw(39) << payload.timestamp() << "║\n";
    }

    if (payload.has_seq()) {
      std::cout << "║ Sequence: " << std::setw(48) << payload.seq() << "║\n";
    } else {
      std::cout << "║ Sequence: " << std::setw(48) << "(none)" << "║\n";
    }

    std::cout << "╠════════════════════════════════════════════════════════════╣\n";
    std::cout << "║ Metrics: " << std::setw(49) << payload.metrics_size() << "║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n";

    for (const auto& metric : payload.metrics()) {
      print_metric(metric);
    }

    std::cout << std::endl; // Flush immediately
  };

  sparkplug::HostApplication::Config config{.broker_url = "tcp://localhost:1883",
                                            .client_id = "sparkplug_subscriber_debug",
                                            .host_id = "Energy",
                                            .qos = 1,
                                            .clean_session = true,
                                            .validate_sequence =
                                                true, // Enable validation to see warnings
                                            .message_callback = message_handler};

  sparkplug::HostApplication subscriber(std::move(config));

  std::cout << "🔧 Debug Subscriber Starting...\n";

  auto connect_result = subscriber.connect();
  if (!connect_result) {
    std::cerr << "❌ Failed to connect: " << connect_result.error() << "\n";
    return 1;
  }

  std::cout << "✓ Connected to broker at tcp://localhost:1883\n";

  auto subscribe_result = subscriber.subscribe_all_groups();
  if (!subscribe_result) {
    std::cerr << "❌ Failed to subscribe: " << subscribe_result.error() << "\n";
    return 1;
  }

  std::cout << "✓ Subscribed to: spBv1.0/Energy/#\n";
  std::cout << "✓ Validation: ENABLED\n";
  std::cout << "\n⏳ Waiting for messages...\n";
  std::cout << "   (Press Ctrl+C to exit)\n";
  std::cout << "   (Try: kill -9 <publisher_pid> to send NDEATH)\n\n";

  auto last_count = 0;
  while (running) {
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Show we're alive every 10 seconds if no messages
    auto current_count = message_count.load();
    if (current_count == last_count) {
      static int idle_count = 0;
      if (++idle_count % 10 == 0) {
        std::cout << "💤 Still waiting... (received " << current_count << " messages so far)\n"
                  << std::flush;
      }
    } else {
      last_count = current_count;
    }
  }

  std::cout << "\n\n⏹ Shutting down...\n";
  std::cout << "📊 Total messages received: " << message_count.load() << "\n";

  auto disconnect_result = subscriber.disconnect();
  if (!disconnect_result) {
    std::cerr << "❌ Failed to disconnect: " << disconnect_result.error() << "\n";
  } else {
    std::cout << "✓ Disconnected\n";
  }

  return 0;
}