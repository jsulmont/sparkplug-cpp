// Minimal host application - mimics der-agent behavior
// Quick and dirty KISS example to reproduce disconnect bug

#include <atomic>
#include <csignal>
#include <iostream>
#include <thread>

#include <sparkplug/host_application.hpp>

std::atomic<bool> running{true};

void signal_handler(int) {
  running = false;
}

int main() {
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  // Minimal config - just like der-agent
  sparkplug::HostApplication::Config config{
      .broker_url = "tcp://localhost:1883",
      .client_id = "minimal_host_test",
      .host_id = "MinimalHost",
      .message_callback =
          [](const sparkplug::Topic& topic,
             const org::eclipse::tahu::protobuf::Payload& payload) {
            std::cout << "Received: " << topic.to_string() << " (seq=" << payload.seq() << ")\n";
          },
      .log_callback =
          [](sparkplug::LogLevel /*level*/, std::string_view msg) {
            std::cout << "[LOG] " << msg << "\n";
          }};

  std::cout << "Creating HostApplication...\n";
  sparkplug::HostApplication host(std::move(config));

  std::cout << "Connecting to broker...\n";
  auto result = host.connect();
  if (!result) {
    std::cerr << "Connect failed: " << result.error() << "\n";
    return 1;
  }
  std::cout << "Connected!\n";

  std::cout << "Subscribing to all groups...\n";
  result = host.subscribe_all_groups();
  if (!result) {
    std::cerr << "Subscribe failed: " << result.error() << "\n";
    return 1;
  }
  std::cout << "Subscribed to spBv1.0/#\n";

  std::cout << "Listening... (Ctrl+C to exit)\n\n";

  // Just stay alive
  while (running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::cout << "\nDisconnecting...\n";
  result = host.disconnect();
  if (!result) {
    std::cerr << "Disconnect failed: " << result.error() << "\n";
  }
  std::cout << "Done.\n";

  return 0;
}
