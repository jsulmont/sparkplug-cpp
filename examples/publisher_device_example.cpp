// examples/publisher_device_example.cpp - Device-level Sparkplug B example
#include <atomic>
#include <csignal>
#include <iostream>
#include <thread>

#include <sparkplug/edge_node.hpp>
#include <sparkplug/payload_builder.hpp>

std::atomic<bool> running{true};

void signal_handler(int signal) {
  (void)signal;
  running = false;
}

int main() {
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  sparkplug::EdgeNode::Config config{.broker_url = "tcp://localhost:1883",
                                     .client_id = "sparkplug_device_publisher",
                                     .group_id = "Factory",
                                     .edge_node_id = "Gateway01",
                                     .data_qos = 0,
                                     .death_qos = 1,
                                     .clean_session = true,
                                     .keep_alive_interval = 60};

  sparkplug::EdgeNode publisher(std::move(config));

  auto connect_result = publisher.connect();
  if (!connect_result) {
    std::cerr << "Failed to connect: " << connect_result.error() << "\n";
    return 1;
  }

  std::cout << "Connected to broker\n";

  // STEP 1: Publish NBIRTH (required before any device births)
  sparkplug::PayloadBuilder node_birth;
  node_birth.add_metric("bdSeq", static_cast<uint64_t>(publisher.get_bd_seq()));
  node_birth.add_node_control_rebirth(false);
  node_birth.add_metric("Properties/Gateway Version", "1.0.0");

  auto birth_result = publisher.publish_birth(node_birth);
  if (!birth_result) {
    std::cerr << "Failed to publish NBIRTH: " << birth_result.error() << "\n";
    return 1;
  }

  std::cout << "Published NBIRTH\n";

  // STEP 2: Publish DBIRTH for first device (Temperature Sensor)
  sparkplug::PayloadBuilder sensor_birth;
  sensor_birth.add_metric_with_alias("Temperature", 1, 20.5);
  sensor_birth.add_metric_with_alias("Humidity", 2, 65.0);
  sensor_birth.add_metric("Device Type", "DHT22");

  auto sensor_birth_result = publisher.publish_device_birth("Sensor01", sensor_birth);
  if (!sensor_birth_result) {
    std::cerr << "Failed to publish DBIRTH for Sensor01: " << sensor_birth_result.error() << "\n";
    return 1;
  }

  std::cout << "Published DBIRTH for Sensor01\n";

  // STEP 3: Publish DBIRTH for second device (Motor)
  sparkplug::PayloadBuilder motor_birth;
  motor_birth.add_metric_with_alias("RPM", 1, 0.0);
  motor_birth.add_metric_with_alias("Running", 2, false);
  motor_birth.add_metric_with_alias("Power", 3, 0.0);
  motor_birth.add_metric("Device Type", "AC Motor");

  auto motor_birth_result = publisher.publish_device_birth("Motor01", motor_birth);
  if (!motor_birth_result) {
    std::cerr << "Failed to publish DBIRTH for Motor01: " << motor_birth_result.error() << "\n";
    return 1;
  }

  std::cout << "Published DBIRTH for Motor01\n";

  // STEP 4: Publish DDATA updates for devices
  int count = 0;
  double temperature = 20.5;
  double rpm = 0.0;

  std::cout << "\nPublishing DDATA messages (Ctrl+C to stop)...\n";

  while (running) {
    count++;

    // Update Sensor01 data
    temperature += 0.1;
    sparkplug::PayloadBuilder sensor_data;
    sensor_data.add_metric_by_alias(1, temperature); // Temperature changed

    auto sensor_data_result = publisher.publish_device_data("Sensor01", sensor_data);
    if (!sensor_data_result) {
      std::cerr << "Failed to publish DDATA for Sensor01: " << sensor_data_result.error() << "\n";
    }

    // Update Motor01 data
    if (count == 5) {
      // Start motor
      sparkplug::PayloadBuilder motor_data;
      motor_data.add_metric_by_alias(2, true);   // Running
      motor_data.add_metric_by_alias(1, 1500.0); // RPM
      motor_data.add_metric_by_alias(3, 2.5);    // Power (kW)

      auto motor_data_result = publisher.publish_device_data("Motor01", motor_data);
      if (!motor_data_result) {
        std::cerr << "Failed to publish DDATA for Motor01: " << motor_data_result.error() << "\n";
      } else {
        std::cout << "Motor01 started\n";
      }
      rpm = 1500.0;
    } else if (count > 5) {
      // Vary RPM
      rpm += 10.0;
      sparkplug::PayloadBuilder motor_data;
      motor_data.add_metric_by_alias(1, rpm); // Only RPM changed

      auto motor_data_result = publisher.publish_device_data("Motor01", motor_data);
      if (!motor_data_result) {
        std::cerr << "Failed to publish DDATA for Motor01: " << motor_data_result.error() << "\n";
      }
    }

    if (count % 10 == 0) {
      std::cout << "Published " << count << " DDATA messages (Temp: " << temperature
                << ", RPM: " << rpm << ")\n";
    }

    // STEP 5: Demonstrate device death and rebirth
    if (count == 30) {
      std::cout << "\nSimulating Sensor01 disconnect...\n";
      auto death_result = publisher.publish_device_death("Sensor01");
      if (!death_result) {
        std::cerr << "Failed to publish DDEATH: " << death_result.error() << "\n";
      } else {
        std::cout << "Published DDEATH for Sensor01\n";
      }
    }

    if (count == 35) {
      std::cout << "\nSensor01 reconnected, publishing new DBIRTH...\n";
      sparkplug::PayloadBuilder sensor_rebirth;
      sensor_rebirth.add_metric_with_alias("Temperature", 1, 22.0);
      sensor_rebirth.add_metric_with_alias("Humidity", 2, 60.0);
      sensor_rebirth.add_metric("Device Type", "DHT22");

      auto rebirth_result = publisher.publish_device_birth("Sensor01", sensor_rebirth);
      if (!rebirth_result) {
        std::cerr << "Failed to publish DBIRTH: " << rebirth_result.error() << "\n";
      } else {
        std::cout << "Published new DBIRTH for Sensor01\n";
        temperature = 22.0; // Reset temperature
      }
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  std::cout << "\nShutting down...\n";

  auto disconnect_result = publisher.disconnect();
  if (!disconnect_result) {
    std::cerr << "Failed to disconnect: " << disconnect_result.error() << "\n";
  } else {
    std::cout << "Disconnected (NDEATH sent via MQTT Will)\n";
  }

  std::cout << "\nSession Statistics:\n";
  std::cout << "  Total updates: " << count << "\n";
  std::cout << "  Final bdSeq: " << publisher.get_bd_seq() << "\n";

  return 0;
}
