// include/sparkplug/topic.hpp
#pragma once

#include "detail/compat.hpp"

#include <string>
#include <string_view>

namespace sparkplug {

inline constexpr std::string_view NAMESPACE = "spBv1.0";

/**
 * @brief Sparkplug B message types.
 *
 * Defines all valid message types in the Sparkplug B specification:
 * - NBIRTH/NDEATH: Node (edge node) birth/death
 * - DBIRTH/DDEATH: Device birth/death
 * - NDATA/DDATA: Node/device data updates
 * - NCMD/DCMD: Node/device commands
 * - STATE: Primary application state
 */
enum class MessageType {
  NBIRTH, ///< Node Birth Certificate
  NDEATH, ///< Node Death Certificate
  DBIRTH, ///< Device Birth Certificate
  DDEATH, ///< Device Death Certificate
  NDATA,  ///< Node Data
  DDATA,  ///< Device Data
  NCMD,   ///< Node Command
  DCMD,   ///< Device Command
  STATE   ///< Primary Application State (not part of spBv1.0 namespace)
};

/**
 * @brief Represents a parsed Sparkplug B MQTT topic.
 *
 * Sparkplug B topics follow the format:
 * - spBv1.0/{group_id}/{message_type}/{edge_node_id}[/{device_id}]
 * - spBv1.0/STATE/{host_id} (for primary application state)
 *
 * @par Example Topics
 * - `spBv1.0/Energy/NBIRTH/Gateway01` - Node birth
 * - `spBv1.0/Energy/DBIRTH/Gateway01/Sensor01` - Device birth
 * - `spBv1.0/STATE/ScadaHost1` - Primary application state
 */
struct Topic {
  std::string group_id;     ///< Group ID (topic namespace)
  MessageType message_type; ///< Message type (NBIRTH, NDATA, etc.)
  std::string edge_node_id; ///< Edge node identifier
  std::string device_id;    ///< Device identifier (empty for node-level messages)

  /**
   * @brief Converts the topic back to a string.
   *
   * @return Formatted topic string (e.g., "spBv1.0/Energy/NBIRTH/Gateway01")
   *
   * @note May throw if std::format throws (e.g., out of memory), but this is rare.
   */
  [[nodiscard]] std::string to_string() const;

  /**
   * @brief Parses a Sparkplug B topic string.
   *
   * @param topic_str Topic string to parse
   *
   * @return Parsed Topic on success, error message on failure
   *
   * @par Example
   * @code
   * auto result = sparkplug::Topic::parse("spBv1.0/Energy/NBIRTH/Gateway01");
   * if (result) {
   *   const auto& topic = *result;
   *   std::cout << "Group: " << topic.group_id << "\n";
   * }
   * @endcode
   */
  [[nodiscard]] static stdx::expected<Topic, std::string> parse(std::string_view topic_str);
};

} // namespace sparkplug