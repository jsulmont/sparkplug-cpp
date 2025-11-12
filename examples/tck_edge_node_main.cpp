#include "tck_edge_node.hpp"

#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <thread>

std::unique_ptr<sparkplug::tck::TCKEdgeNode> g_node;

void signal_handler(int signal) {
  std::cout << "\nReceived signal " << signal << ", shutting down...\n";
  if (g_node) {
    g_node->stop();
  }
  exit(0);
}

int main(int argc, char* argv[]) {
  std::cout << "========================================\n";
  std::cout << "Sparkplug TCK Edge Node\n";
  std::cout << "========================================\n\n";

  sparkplug::tck::TCKEdgeNodeConfig config;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--broker" && i + 1 < argc) {
      config.broker_url = argv[++i];
    } else if (arg == "--group-id" && i + 1 < argc) {
      config.group_id = argv[++i];
    } else if (arg == "--edge-node-id" && i + 1 < argc) {
      config.edge_node_id = argv[++i];
    } else if (arg == "--username" && i + 1 < argc) {
      config.username = argv[++i];
    } else if (arg == "--password" && i + 1 < argc) {
      config.password = argv[++i];
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: " << argv[0] << " [options]\n\n";
      std::cout << "Options:\n";
      std::cout
          << "  --broker <url>         MQTT broker URL (default: tcp://localhost:1883)\n";
      std::cout << "  --group-id <id>        Group ID (default: tck_group)\n";
      std::cout << "  --edge-node-id <id>    Edge Node ID (default: tck_edge)\n";
      std::cout << "  --username <user>      MQTT username (optional)\n";
      std::cout << "  --password <pass>      MQTT password (optional)\n";
      std::cout << "  --help, -h             Show this help message\n\n";
      std::cout << "Example:\n";
      std::cout << "  " << argv[0]
                << " --broker tcp://localhost:1883 --group-id MyGroup --edge-node-id "
                   "Edge01\n\n";
      return 0;
    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      std::cerr << "Use --help for usage information\n";
      return 1;
    }
  }

  std::cout << "Configuration:\n";
  std::cout << "  Broker URL: " << config.broker_url << "\n";
  std::cout << "  Group ID: " << config.group_id << "\n";
  std::cout << "  Edge Node ID: " << config.edge_node_id << "\n";
  if (!config.username.empty()) {
    std::cout << "  Username: " << config.username << "\n";
  }
  std::cout << "\n";

  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  try {
    g_node = std::make_unique<sparkplug::tck::TCKEdgeNode>(config);

    auto result = g_node->start();
    if (!result) {
      std::cerr << "Failed to start TCK Edge Node: " << result.error() << "\n";
      return 1;
    }

    std::cout << "\nTCK Edge Node is running.\n";
    std::cout << "Waiting for test commands from TCK Console...\n";
    std::cout << "Press Ctrl+C to exit.\n\n";

    while (g_node->is_running()) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }

  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  std::cout << "TCK Edge Node terminated.\n";
  return 0;
}
