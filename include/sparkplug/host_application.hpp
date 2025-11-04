// include/sparkplug/host_application.hpp
#pragma once

#include "detail/compat.hpp"
#include "mqtt_handle.hpp"
#include "payload_builder.hpp"
#include "sparkplug_b.pb.h"
#include "topic.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>

#include <MQTTAsync.h>

namespace sparkplug {

/**
 * @brief Log severity levels for library diagnostics.
 */
enum class LogLevel {
  DEBUG = 0, ///< Detailed debugging information
  INFO = 1,  ///< Informational messages
  WARN = 2,  ///< Warning messages (potential issues)
  ERROR = 3  ///< Error messages (serious problems)
};

/**
 * @brief Callback function type for receiving log messages from the library.
 */
using LogCallback = std::function<void(LogLevel, std::string_view)>;

/**
 * @brief Callback function type for receiving Sparkplug B messages.
 *
 * @param topic Parsed Sparkplug B topic containing group_id, message_type, edge_node_id, etc.
 * @param payload Decoded Sparkplug B protobuf payload with metrics
 */
using MessageCallback =
    std::function<void(const Topic&, const org::eclipse::tahu::protobuf::Payload&)>;

/**
 * @brief Sparkplug B Host Application for SCADA/Primary Applications.
 *
 * The HostApplication class implements the complete Sparkplug B protocol for Host Applications:
 * - Subscribes to spBv1.0/# to receive all Edge Node messages (NBIRTH/NDATA/NDEATH)
 * - Validates message sequences and tracks node/device state
 * - Publishes STATE messages (JSON format) to indicate online/offline status
 * - Publishes NCMD/DCMD commands to control Edge Nodes and Devices
 * - Does NOT publish NBIRTH/NDATA/NDEATH (those are for Edge Nodes only)
 *
 * This is the authoritative consumer and command source in a Sparkplug B topology.
 * A Host Application should use a single MQTT client that both receives data and sends commands.
 *
 * @par Thread Safety
 * This class is fully thread-safe with coarse-grained locking:
 * - All public methods use a single internal mutex to protect shared state
 * - Methods can be safely called from any thread concurrently
 * - Callbacks (message_callback, log_callback) invoked on MQTT thread WITHOUT holding mutex
 * - Mutex is released before MQTT publish to prevent callback deadlocks
 *
 * @par Rust FFI Compatibility
 * - Implements Send: Can transfer between threads safely (all state mutex-protected)
 * - Implements Sync: Can access from multiple threads concurrently (mutex-guarded methods)
ill  *
 * @par Example Usage
 * @code
 * auto message_callback = [](const sparkplug::Topic& topic, const auto& payload) {
 *   std::cout << "Received: " << topic.to_string() << "\n";
 * };
 *
 * sparkplug::HostApplication::Config config{
 *   .broker_url = "tcp://localhost:1883",
 *   .client_id = "scada_host",
 *   .host_id = "SCADA01",
 *   .message_callback = message_callback
 * };
 *
 * sparkplug::HostApplication host_app(std::move(config));
 * host_app.connect();
 *
 * // Subscribe to all Sparkplug messages
 * host_app.subscribe_all_groups();
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
 * @see EdgeNode for Edge Node implementation
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
   * @brief Tracks the state of a device attached to an edge node.
   */
  struct DeviceState {
    bool is_online{false};      ///< True if DBIRTH received and device is online
    uint64_t last_seq{255};     ///< Last received device sequence number
    bool birth_received{false}; ///< True if DBIRTH has been received
    std::unordered_map<uint64_t, std::string>
        alias_map; ///< Maps metric alias to name (from DBIRTH)
  };

  /**
   * @brief Transparent hash for string keys to enable heterogeneous lookup.
   */
  struct TransparentStringHash {
    using is_transparent = void;
    using hash_type = std::hash<std::string_view>;
    [[nodiscard]] size_t operator()(std::string_view str) const noexcept {
      return hash_type{}(str);
    }
    [[nodiscard]] size_t operator()(const std::string& str) const noexcept {
      return hash_type{}(str);
    }
  };

  /**
   * @brief Tracks the state of an individual edge node.
   */
  struct NodeState {
    bool is_online{false};       ///< True if NBIRTH received and node is online
    uint64_t last_seq{255};      ///< Last received node sequence number (starts at 255)
    uint64_t bd_seq{0};          ///< Current birth/death sequence number
    uint64_t birth_timestamp{0}; ///< Timestamp of last NBIRTH
    bool birth_received{false};  ///< True if NBIRTH has been received
    std::unordered_map<std::string, DeviceState, TransparentStringHash, std::equal_to<>>
        devices; ///< Attached devices (device_id -> state)
    std::unordered_map<uint64_t, std::string>
        alias_map; ///< Maps metric alias to name (from NBIRTH)
  };

  /**
   * @brief Configuration parameters for the Sparkplug B Host Application.
   */
  struct Config {
    std::string broker_url;       ///< MQTT broker URL (e.g., "tcp://localhost:1883" or
                                  ///< "ssl://localhost:8883")
    std::string client_id;        ///< Unique MQTT client identifier
    std::string host_id;          ///< Host Application identifier (for STATE messages)
    int qos = 1;                  ///< MQTT QoS for STATE messages and commands (default: 1)
    bool clean_session = true;    ///< MQTT clean session flag (should be true per Sparkplug spec)
    int keep_alive_interval = 60; ///< MQTT keep-alive interval in seconds (default: 60)
    int max_inflight = 100; ///< Maximum number of QoS 1/2 messages allowed in-flight (default: 100,
                            ///< paho default: 10)
    bool validate_sequence = true;   ///< Enable sequence number validation (detects packet loss)
    std::optional<TlsOptions> tls{}; ///< TLS/SSL options (required if broker_url uses ssl://)
    std::optional<std::string> username{}; ///< MQTT username for authentication (optional)
    std::optional<std::string> password{}; ///< MQTT password for authentication (optional)
    MessageCallback message_callback{};    ///< Callback for received Sparkplug messages
    LogCallback log_callback{};            ///< Optional callback for library log messages
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
   * @brief Sets the message callback for receiving Sparkplug messages.
   *
   * @param callback Callback function to invoke for received messages
   *
   * @note Must be called before connect().
   */
  void set_message_callback(MessageCallback callback);

  /**
   * @brief Sets the log callback for receiving library diagnostic messages.
   *
   * @param callback Callback function to invoke for log messages
   *
   * @note Can be called at any time.
   */
  void set_log_callback(LogCallback callback);

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
  [[nodiscard]] stdx::expected<void, std::string> connect();

  /**
   * @brief Gracefully disconnects from the MQTT broker.
   *
   * @return void on success, error message on failure
   *
   * @note You should call publish_state_death() before disconnect() to properly
   *       signal that the Host Application is going offline.
   */
  [[nodiscard]] stdx::expected<void, std::string> disconnect();

  /**
   * @brief Subscribes to all Sparkplug B messages across all groups.
   *
   * Subscribes to the wildcard topic: spBv1.0/#
   *
   * This receives all message types (NBIRTH, NDATA, NDEATH, DBIRTH, DDATA, DDEATH)
   * from all edge nodes in all groups.
   *
   * @return void on success, error message on failure
   *
   * @note Must call connect() first.
   * @note The message_callback will be invoked for every message received.
   */
  [[nodiscard]] stdx::expected<void, std::string> subscribe_all_groups();

  /**
   * @brief Subscribes to messages from a specific group.
   *
   * Subscribes to: spBv1.0/{group_id}/#
   *
   * @param group_id The group ID to subscribe to
   *
   * @return void on success, error message on failure
   *
   * @note Allows subscribing to multiple groups on a single MQTT connection.
   */
  [[nodiscard]] stdx::expected<void, std::string> subscribe_group(std::string_view group_id);

  /**
   * @brief Subscribes to messages from a specific edge node in a group.
   *
   * Subscribes to: spBv1.0/{group_id}/+/{edge_node_id}/#
   *
   * @param group_id The group ID
   * @param edge_node_id The edge node ID to subscribe to
   *
   * @return void on success, error message on failure
   *
   * @note More efficient than subscribe_all_groups() if you only need specific nodes.
   */
  [[nodiscard]] stdx::expected<void, std::string> subscribe_node(std::string_view group_id,
                                                                 std::string_view edge_node_id);

  /**
   * @brief Subscribes to STATE messages from another primary application.
   *
   * STATE messages indicate whether another SCADA/Primary Application is online.
   * Subscribe to: spBv1.0/STATE/{host_id}
   *
   * @param host_id The host application identifier
   *
   * @return void on success, error message on failure
   *
   * @note STATE messages are outside the normal Sparkplug topic namespace.
   * @note Useful for High Availability (HA) setups with multiple host applications.
   */
  [[nodiscard]] stdx::expected<void, std::string> subscribe_state(std::string_view host_id);

  /**
   * @brief Gets the current state of a specific edge node.
   *
   * @param group_id The group ID
   * @param edge_node_id The edge node ID to query
   *
   * @return NodeState if the node has been seen, std::nullopt otherwise
   *
   * @note Useful for monitoring node online/offline status and bdSeq.
   */
  [[nodiscard]] std::optional<std::reference_wrapper<const NodeState>>
  get_node_state(std::string_view group_id, std::string_view edge_node_id) const;

  /**
   * @brief Resolves a metric alias to its name for a specific node or device.
   *
   * Looks up the metric name that corresponds to the given alias, based on
   * the alias mappings captured from NBIRTH (node metrics) or DBIRTH (device metrics).
   *
   * @param group_id The group ID
   * @param edge_node_id The edge node ID
   * @param device_id The device ID (empty string for node-level metrics)
   * @param alias The metric alias to resolve
   *
   * @return A string_view to the metric name if found, std::nullopt otherwise
   *
   * @note Returns std::nullopt if the node/device hasn't sent a birth message yet,
   *       or if the alias is not found in the birth message.
   */
  [[nodiscard]] std::optional<std::string_view> get_metric_name(std::string_view group_id,
                                                                std::string_view edge_node_id,
                                                                std::string_view device_id,
                                                                uint64_t alias) const;

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
   * @note Topic format: spBv1.0/STATE/<host_id>
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
  [[nodiscard]] stdx::expected<void, std::string> publish_state_birth(uint64_t timestamp);

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
   * @note Topic format: spBv1.0/STATE/<host_id>
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
  [[nodiscard]] stdx::expected<void, std::string> publish_state_death(uint64_t timestamp);

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
  [[nodiscard]] stdx::expected<void, std::string>
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
  [[nodiscard]] stdx::expected<void, std::string>
  publish_device_command(std::string_view group_id, std::string_view target_edge_node_id,
                         std::string_view target_device_id, PayloadBuilder& payload);

  /**
   * @brief Internal logging method accessible from C bindings.
   *
   * @param level Log severity level
   * @param message Log message content
   */
  void log(LogLevel level, std::string_view message) const noexcept;

private:
  Config config_;
  MQTTAsyncHandle client_;
  bool is_connected_{false};

  // MQTT connection options that must outlive async operations
  MQTTAsync_SSLOptions ssl_opts_{};

  // Node state tracking
  struct NodeKey {
    std::string group_id;
    std::string edge_node_id;

    [[nodiscard]] bool operator==(const NodeKey& other) const noexcept {
      return group_id == other.group_id && edge_node_id == other.edge_node_id;
    }
  };

  struct NodeKeyHash {
    using is_transparent = void;
    [[nodiscard]] size_t operator()(const NodeKey& key) const noexcept {
      size_t h1 = std::hash<std::string>{}(key.group_id);
      size_t h2 = std::hash<std::string>{}(key.edge_node_id);
      return h1 ^ (h2 << 1);
    }
    [[nodiscard]] size_t
    operator()(std::pair<std::string_view, std::string_view> key) const noexcept {
      size_t h1 = std::hash<std::string_view>{}(key.first);
      size_t h2 = std::hash<std::string_view>{}(key.second);
      return h1 ^ (h2 << 1);
    }
  };

  struct NodeKeyEqual {
    using is_transparent = void;
    [[nodiscard]] bool operator()(const NodeKey& lhs, const NodeKey& rhs) const noexcept {
      return lhs == rhs;
    }
    [[nodiscard]] bool
    operator()(const NodeKey& lhs,
               std::pair<std::string_view, std::string_view> rhs) const noexcept {
      return lhs.group_id == rhs.first && lhs.edge_node_id == rhs.second;
    }
    [[nodiscard]] bool operator()(std::pair<std::string_view, std::string_view> lhs,
                                  const NodeKey& rhs) const noexcept {
      return lhs.first == rhs.group_id && lhs.second == rhs.edge_node_id;
    }
  };

  std::unordered_map<NodeKey, NodeState, NodeKeyHash, NodeKeyEqual> node_states_;

  // Mutex for thread-safe access to all mutable state
  mutable std::mutex mutex_;

  [[nodiscard]] stdx::expected<void, std::string>
  publish_raw_message(std::string_view topic, std::span<const uint8_t> payload_data, int qos,
                      bool retain);

  [[nodiscard]] stdx::expected<void, std::string>
  publish_command_message(std::string_view topic, std::span<const uint8_t> payload_data);

  bool validate_message(const Topic& topic, const org::eclipse::tahu::protobuf::Payload& payload);

  // Static MQTT callback for message arrived
  static int on_message_arrived(void* context, char* topicName, int topicLen,
                                MQTTAsync_message* message);

  static void on_connection_lost(void* context, char* cause);
};

} // namespace sparkplug
