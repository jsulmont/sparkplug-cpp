// examples/host_application_mimic.cpp
// Host Application example for MIMIC MQTT Lab broker
//
// MIMIC Lab: https://mqttlab.iotsim.io/sparkplug/
// Broker: broker.hivemq.com:1883
//
// This Host Application (SCADA) sends commands to edge nodes, including rebirth commands.
// Use with publisher_mimic.cpp to see the rebirth mechanism in action.

#include "sparkplug/host_application.hpp"
#include "sparkplug/payload_builder.hpp"

#include <chrono>
#include <iostream>
#include <thread>

int main() {
  std::cout << "=================================================================\n";
  std::cout << "  MIMIC MQTT Lab Host Application (SCADA)\n";
  std::cout << "=================================================================\n";
  std::cout << "Connecting to: broker.hivemq.com:1883\n";
  std::cout << "Host ID: SCADA_SparkplugCPP\n";
  std::cout << "Info: https://mqttlab.iotsim.io/sparkplug/\n";
  std::cout << "=================================================================\n\n";

  sparkplug::HostApplication::Config config{
      .broker_url = "tcp://broker.hivemq.com:1883",
      .client_id = "scada_host_sparkplug_cpp",
      .host_id = "SCADA_SparkplugCPP",
      .qos = 1,
      .clean_session = true,
      .keep_alive_interval = 60,
  };

  std::cout << "Creating Host Application...\n";
  sparkplug::HostApplication host_app(std::move(config));

  std::cout << "Connecting to broker...\n";
  auto result = host_app.connect();
  if (!result) {
    std::cerr << "Failed to connect: " << result.error() << "\n";
    std::cerr << "\nNote: This requires internet access to broker.hivemq.com\n";
    return 1;
  }

  auto timestamp = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::system_clock::now().time_since_epoch())
                                             .count());

  // Publish STATE birth (declare Host Application is online)
  std::cout << "Publishing STATE birth (Host Application online)...\n";
  result = host_app.publish_state_birth(timestamp);
  if (!result) {
    std::cerr << "Failed to publish STATE birth: " << result.error() << "\n";
    return 1;
  }

  std::cout << "Published STATE birth\n";
  std::cout << "  Topic: STATE/SCADA_SparkplugCPP\n";
  std::cout << "  Payload: {\"online\":true,\"timestamp\":" << timestamp << "}\n\n";

  std::this_thread::sleep_for(std::chrono::seconds(2));

  // Send rebirth command to the publisher_mimic edge node
  std::cout << "=================================================================\n";
  std::cout << "  Sending NCMD Rebirth Command\n";
  std::cout << "=================================================================\n";
  std::cout << "Target: spBv1.0/TestGroup/NCMD/SparkplugCPP\n";
  std::cout << "Command: Node Control/Rebirth = true\n";
  std::cout << "\nIf publisher_mimic is running, it should:\n";
  std::cout << "  1. Receive this NCMD\n";
  std::cout << "  2. Increment its bdSeq\n";
  std::cout << "  3. Republish NBIRTH with new bdSeq\n";
  std::cout << "  4. Reset sequence number to 0\n";
  std::cout << "=================================================================\n\n";

  sparkplug::PayloadBuilder rebirth_cmd;
  rebirth_cmd.add_metric("Node Control/Rebirth", true);

  result = host_app.publish_node_command("TestGroup", "SparkplugCPP", rebirth_cmd);
  if (!result) {
    std::cerr << "Failed to publish NCMD: " << result.error() << "\n";
  } else {
    std::cout << "Successfully sent rebirth command!\n";
    std::cout << "Check the publisher_mimic terminal to see it respond.\n\n";
  }

  std::this_thread::sleep_for(std::chrono::seconds(2));

  // Send another rebirth command (demonstrate multiple rebirths)
  std::cout << "Sending SECOND rebirth command (5 seconds later)...\n";
  std::this_thread::sleep_for(std::chrono::seconds(5));

  result = host_app.publish_node_command("TestGroup", "SparkplugCPP", rebirth_cmd);
  if (!result) {
    std::cerr << "Failed to publish second NCMD: " << result.error() << "\n";
  } else {
    std::cout << "Successfully sent second rebirth command!\n";
    std::cout << "bdSeq should increment again.\n\n";
  }

  std::this_thread::sleep_for(std::chrono::seconds(2));

  // Example: Send a device command (optional, for demonstration)
  std::cout << "\n=================================================================\n";
  std::cout << "  Example: Sending DCMD Device Command\n";
  std::cout << "=================================================================\n";
  std::cout << "Target: spBv1.0/TestGroup/DCMD/SparkplugCPP/Sensor01\n";
  std::cout << "Command: SetPoint = 75.0\n";
  std::cout << "(publisher_mimic does not handle devices, so this is just a demo)\n";
  std::cout << "=================================================================\n\n";

  sparkplug::PayloadBuilder device_cmd;
  device_cmd.add_metric("SetPoint", 75.0);

  result = host_app.publish_device_command("TestGroup", "SparkplugCPP", "Sensor01", device_cmd);
  if (!result) {
    std::cerr << "Failed to publish DCMD: " << result.error() << "\n";
  } else {
    std::cout << "Successfully sent device command.\n\n";
  }

  std::this_thread::sleep_for(std::chrono::seconds(2));

  // Publish STATE death
  std::cout << "Publishing STATE death (Host Application going offline)...\n";
  result = host_app.publish_state_death(timestamp);
  if (!result) {
    std::cerr << "Failed to publish STATE death: " << result.error() << "\n";
  } else {
    std::cout << "Published STATE death\n";
    std::cout << "  Topic: STATE/SCADA_SparkplugCPP\n";
    std::cout << "  Payload: {\"online\":false,\"timestamp\":" << timestamp << "}\n\n";
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  std::cout << "Disconnecting from broker...\n";
  result = host_app.disconnect();
  if (!result) {
    std::cerr << "Failed to disconnect: " << result.error() << "\n";
    return 1;
  }

  std::cout << "Host Application shutdown complete.\n";
  std::cout << "\n=================================================================\n";
  std::cout << "To test the rebirth mechanism:\n";
  std::cout << "  Terminal 1: ./build/examples/publisher_mimic\n";
  std::cout << "  Terminal 2: ./build/examples/host_application_mimic\n";
  std::cout << "  Terminal 3: ./build/examples/subscriber_mimic_debug (optional)\n";
  std::cout << "=================================================================\n";

  return 0;
}
