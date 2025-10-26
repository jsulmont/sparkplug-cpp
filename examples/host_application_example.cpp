// examples/host_application_example.cpp
// Example of using the HostApplication class for a SCADA/Primary Application
/*
  STATE messages are a special type of Sparkplug message used by Host Applications (SCADA/Primary
  Applications) to declare their online/offline status. They work fundamentally differently from
  normal Sparkplug messages:


  1. Who Publishes STATE Messages?
  - Host Applications (SCADA systems, primary applications) publish STATE messages
  - NOT Edge Nodes (Edge Nodes publish NBIRTH/NDEATH instead)

  2. Topic Format:
  STATE/<host_id>
  Notice: This is NOT in the spBv1.0/ namespace! (see test_topic.cpp:36)

  Example:
  - STATE/SCADA01 - for a SCADA Host Application with ID "SCADA01"

  3. Payload Format:
  Unlike other Sparkplug messages, STATE messages use JSON, not Protocol Buffers:

  // STATE birth (Host App online)
  {"online": true, "timestamp": 1234567890123}

  // STATE death (Host App offline)
  {"online": false, "timestamp": 1234567890123}

  See host_application_example.cpp:46 and 87 for examples.

  4. MQTT Properties:
  - QoS: 1 (reliable delivery)
  - Retain: true (important! Late-joining Edge Nodes see the Host's status)

  How STATE Messages Work

  1. Connect to broker (no automatic messages)
  2. Publish STATE birth → tells Edge Nodes "I'm online and monitoring"
  3. Send NCMD/DCMD commands → control Edge Nodes and devices
  4. Publish STATE death → tells Edge Nodes "I'm going offline"
  5. Disconnect

*/

#include "sparkplug/host_application.hpp"
#include "sparkplug/payload_builder.hpp"

#include <chrono>
#include <iostream>
#include <thread>

int main() {
  sparkplug::HostApplication::Config config{
      .broker_url = "tcp://localhost:1883",
      .client_id = "scada_host",
      .host_id = "SCADA01",
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

  std::cout << "Published STATE birth with timestamp: " << timestamp << "\n";
  std::cout << "Topic: STATE/SCADA01\n";
  std::cout << "Payload: {\"online\":true,\"timestamp\":" << timestamp << "}\n\n";

  std::this_thread::sleep_for(std::chrono::seconds(1));

  std::cout << "Sending NCMD rebirth command to Edge Node 'Gateway01' in group 'Energy'...\n";

  sparkplug::PayloadBuilder rebirth_cmd;
  rebirth_cmd.add_metric("Node Control/Rebirth", true);

  result = host_app.publish_node_command("Energy", "Gateway01", rebirth_cmd);
  if (!result) {
    std::cerr << "Failed to publish NCMD: " << result.error() << "\n";
  } else {
    std::cout << "Successfully sent rebirth command\n";
    std::cout << "Topic: spBv1.0/Energy/NCMD/Gateway01\n\n";
  }

  std::this_thread::sleep_for(std::chrono::seconds(1));

  std::cout << "Sending DCMD to device 'Motor01' on Edge Node 'Gateway01' in group 'Energy'...\n";

  sparkplug::PayloadBuilder device_cmd;
  device_cmd.add_metric("SetPoint", 75.0);

  result = host_app.publish_device_command("Energy", "Gateway01", "Motor01", device_cmd);
  if (!result) {
    std::cerr << "Failed to publish DCMD: " << result.error() << "\n";
  } else {
    std::cout << "Successfully sent device command (SetPoint = 75.0)\n";
    std::cout << "Topic: spBv1.0/Energy/DCMD/Gateway01/Motor01\n\n";
  }

  std::this_thread::sleep_for(std::chrono::seconds(2));

  std::cout << "Publishing STATE death (Host Application going offline)...\n";
  result = host_app.publish_state_death(timestamp);
  if (!result) {
    std::cerr << "Failed to publish STATE death: " << result.error() << "\n";
  } else {
    std::cout << "Published STATE death\n";
    std::cout << "Topic: STATE/SCADA01\n";
    std::cout << "Payload: {\"online\":false,\"timestamp\":" << timestamp << "}\n\n";
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  std::cout << "Disconnecting from broker...\n";
  result = host_app.disconnect();
  if (!result) {
    std::cerr << "Failed to disconnect: " << result.error() << "\n";
    return 1;
  }

  std::cout << "Host Application shutdown complete.\n";
  return 0;
}
