// include/sparkplug/host_application.hpp
#pragma once

#include "mqtt_handle.hpp"
#include "payload_builder.hpp"

#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>

#include <MQTTAsync.h>

namespace sparkplug {

/**
 * @brief Sparkplug B Host Application for SCADA/Primary Applications.
 *
 * The HostApplication class implements the Sparkplug B protocol for Host Applications,
 * which have fundamentally different behavior than Edge Nodes:
 * - Publishes STATE messages (JSON format, not protobuf) to indicate online/offline status
 * - Publishes NCMD/DCMD commands to control Edge Nodes and Devices
 * - Does NOT publish NBIRTH/NDATA/NDEATH (those are for Edge Nodes only)
 * - Does NOT track sequence numbers or bdSeq (no session management needed)
 *
 * @par Thread Safety
 * This class is thread-safe. All methods may be called from any thread concurrently.
 * Internal synchronization is handled via mutex locking.
 *
 * @par Example Usage
 * @code
 * sparkplug::HostApplication::Config config{
 *   .broker_url = "tcp://localhost:1883",
 *   .client_id = "scada_host",
 *   .host_id = "SCADA01"
 * };
 *
 * sparkplug::HostApplication host_app(std::move(config));
 * host_app.connect();
 *
 * // Publish STATE birth (Host App is online)
 * auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
 *     std::chrono::system_clock::now().time_since_epoch()).count();
 * host_app.publish_state_birth(timestamp);
 *
 * // Send rebirth command to an Edge Node
 * sparkplug::PayloadBuilder cmd;
 * cmd.add_metric("Node Control/Rebirth", true);
 * host_app.publish_node_command("Energy", "Gateway01", cmd);
 *
 * // Publish STATE death before disconnecting
 * host_app.publish_state_death(timestamp);
 * host_app.disconnect();
 * @endcode
 *
 * @see Publisher for Edge Node implementation
 * @see Subscriber for consuming Sparkplug B messages
 */
class HostApplication {
public:
  /**
   * @brief TLS/SSL configuration options for secure MQTT connections.
   */
  struct TlsOptions {
    std::string trust_store;             ///< Path to CA certificate file (PEM format)
    std::string key_store;               ///< Path to client certificate file (PEM format, optional)
    std::string private_key;             ///< Path to client private key file (PEM format, optional)
    std::string private_key_password;    ///< Password for encrypted private key (optional)
    std::string enabled_cipher_suites;   ///< Colon-separated list of cipher suites (optional)
    bool enable_server_cert_auth = true; ///< Verify server certificate (default: true)
  };

  /**
   * @brief Configuration parameters for the Sparkplug B Host Application.
   */
  struct Config {
    std::string broker_url;          ///< MQTT broker URL (e.g., "tcp://localhost:1883" or
                                     ///< "ssl://localhost:8883")
    std::string client_id;           ///< Unique MQTT client identifier
    std::string host_id;             ///< Host Application identifier (for STATE messages)
    int qos = 1;                     ///< MQTT QoS for STATE messages and commands (default: 1)
    bool clean_session = true;       ///< MQTT clean session flag
    int keep_alive_interval = 60;    ///< MQTT keep-alive interval in seconds (default: 60)
    std::optional<TlsOptions> tls{}; ///< TLS/SSL options (required if broker_url uses ssl://)
    std::optional<std::string> username{}; ///< MQTT username for authentication (optional)
    std::optional<std::string> password{}; ///< MQTT password for authentication (optional)
  };

  /**
   * @brief Constructs a HostApplication with the given configuration.
   *
   * @param config HostApplication configuration (moved)
   *
   * @note Unlike Publisher, HostApplication does NOT set up MQTT Last Will Testament.
   *       Host Applications should explicitly publish STATE death before disconnecting.
   */
  HostApplication(Config config);

  /**
   * @brief Destroys the HostApplication and cleans up MQTT resources.
   */
  ~HostApplication();

  HostApplication(const HostApplication&) = delete;
  HostApplication& operator=(const HostApplication&) = delete;
  HostApplication(HostApplication&&) noexcept;
  HostApplication& operator=(HostApplication&&) noexcept;

  /**
   * @brief Sets MQTT username and password for authentication.
   *
   * @param username MQTT username (empty string or std::nullopt to unset)
   * @param password MQTT password (empty string or std::nullopt to unset)
   *
   * @note Must be called before connect().
   */
  void set_credentials(std::optional<std::string> username, std::optional<std::string> password);

  /**
   * @brief Configures TLS/SSL options for secure MQTT connections.
   *
   * @param tls TLS configuration options
   *
   * @note Must be called before connect().
   * @note broker_url must use ssl:// prefix for TLS connections.
   */
  void set_tls(std::optional<TlsOptions> tls);

  /**
   * @brief Connects to the MQTT broker.
   *
   * Unlike Publisher::connect(), this does NOT automatically publish any messages.
   * You should call publish_state_birth() after connecting to declare the Host App online.
   *
   * @return void on success, error message on failure
   *
   * @see publish_state_birth() to declare Host Application online
   */
  [[nodiscard]] std::expected<void, std::string> connect();

  /**
   * @brief Gracefully disconnects from the MQTT broker.
   *
   * @return void on success, error message on failure
   *
   * @note You should call publish_state_death() before disconnect() to properly
   *       signal that the Host Application is going offline.
   */
  [[nodiscard]] std::expected<void, std::string> disconnect();

  /**
   * @brief Publishes a STATE birth message to indicate Host Application is online.
   *
   * STATE messages are used by Host Applications (SCADA/Primary Applications) to
   * declare their online status to Edge Nodes. The birth message indicates the
   * Host Application is online and ready to process data.
   *
   * @param timestamp UTC milliseconds since epoch
   *
   * @return void on success, error message on failure
   *
   * @note Topic format: STATE/<host_id>
   * @note Payload format: JSON {"online": true, "timestamp": <timestamp>}
   * @note Message is published with QoS=1 and Retain=true (late-joining Edge Nodes see it)
   * @note This is NOT a Sparkplug protobuf message - uses raw JSON payload
   *
   * @par Example Usage
   * @code
   * auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
   *     std::chrono::system_clock::now().time_since_epoch()).count();
   * host_app.publish_state_birth(timestamp);
   * @endcode
   *
   * @see publish_state_death() for declaring Host Application offline
   */
  [[nodiscard]] std::expected<void, std::string> publish_state_birth(uint64_t timestamp);

  /**
   * @brief Publishes a STATE death message to indicate Host Application is offline.
   *
   * STATE messages are used by Host Applications (SCADA/Primary Applications) to
   * declare their online status to Edge Nodes. The death message indicates the
   * Host Application is going offline.
   *
   * @param timestamp UTC milliseconds since epoch (typically matches birth timestamp)
   *
   * @return void on success, error message on failure
   *
   * @note Topic format: STATE/<host_id>
   * @note Payload format: JSON {"online": false, "timestamp": <timestamp>}
   * @note Message is published with QoS=1 and Retain=true
   * @note This is NOT a Sparkplug protobuf message - uses raw JSON payload
   * @note Should be called BEFORE disconnect() to ensure proper delivery
   *
   * @par Example Usage
   * @code
   * auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
   *     std::chrono::system_clock::now().time_since_epoch()).count();
   * host_app.publish_state_death(timestamp);
   * host_app.disconnect();
   * @endcode
   *
   * @see publish_state_birth() for declaring Host Application online
   */
  [[nodiscard]] std::expected<void, std::string> publish_state_death(uint64_t timestamp);

  /**
   * @brief Publishes an NCMD (Node Command) message to an Edge Node.
   *
   * NCMD messages are commands sent from Host Applications to Edge Nodes to request
   * actions like rebirth, reboot, or custom operations.
   *
   * @param group_id The Sparkplug group ID containing the target Edge Node
   * @param target_edge_node_id The target Edge Node identifier
   * @param payload PayloadBuilder containing command metrics (e.g., "Node Control/Rebirth")
   *
   * @return void on success, error message on failure
   *
   * @note Common Node Control commands:
   *       - "Node Control/Rebirth" (bool): Request node to republish NBIRTH
   *       - "Node Control/Reboot" (bool): Request node to reboot
   *       - "Node Control/Next Server" (bool): Switch to backup server
   *       - "Node Control/Scan Rate" (int64): Change data acquisition rate
   *
   * @par Example Usage
   * @code
   * sparkplug::PayloadBuilder cmd;
   * cmd.add_metric("Node Control/Rebirth", true);
   * host_app.publish_node_command("Energy", "Gateway01", cmd);
   * @endcode
   */
  [[nodiscard]] std::expected<void, std::string>
  publish_node_command(std::string_view group_id, std::string_view target_edge_node_id,
                       PayloadBuilder& payload);

  /**
   * @brief Publishes a DCMD (Device Command) message to a device on an Edge Node.
   *
   * DCMD messages are commands sent from Host Applications to devices attached to Edge Nodes.
   *
   * @param group_id The Sparkplug group ID containing the target Edge Node
   * @param target_edge_node_id The target Edge Node identifier
   * @param target_device_id The target device identifier
   * @param payload PayloadBuilder containing command metrics
   *
   * @return void on success, error message on failure
   *
   * @par Example Usage
   * @code
   * sparkplug::PayloadBuilder cmd;
   * cmd.add_metric("SetPoint", 75.0);
   * host_app.publish_device_command("Energy", "Gateway01", "Motor01", cmd);
   * @endcode
   */
  [[nodiscard]] std::expected<void, std::string>
  publish_device_command(std::string_view group_id, std::string_view target_edge_node_id,
                         std::string_view target_device_id, PayloadBuilder& payload);

private:
  Config config_;
  MQTTAsyncHandle client_;
  bool is_connected_{false};

  // Mutex for thread-safe access to all mutable state
  mutable std::mutex mutex_;

  [[nodiscard]] std::expected<void, std::string>
  publish_raw_message(std::string_view topic, std::span<const uint8_t> payload_data, int qos,
                      bool retain);

  [[nodiscard]] std::expected<void, std::string>
  publish_command_message(std::string_view topic, std::span<const uint8_t> payload_data);
};

} // namespace sparkplug
