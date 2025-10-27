// include/sparkplug/payload_builder.hpp
#pragma once

#include "datatype.hpp"
#include "sparkplug_b.pb.h"

#include <chrono>
#include <concepts>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace sparkplug {

// Concepts for Sparkplug B type system

/// Signed integer types supported by Sparkplug B
template <typename T>
concept SparkplugSignedInteger =
    std::same_as<std::remove_cvref_t<T>, int8_t> || std::same_as<std::remove_cvref_t<T>, int16_t> ||
    std::same_as<std::remove_cvref_t<T>, int32_t> || std::same_as<std::remove_cvref_t<T>, int64_t>;

/// Unsigned integer types supported by Sparkplug B
template <typename T>
concept SparkplugUnsignedInteger = std::same_as<std::remove_cvref_t<T>, uint8_t> ||
                                   std::same_as<std::remove_cvref_t<T>, uint16_t> ||
                                   std::same_as<std::remove_cvref_t<T>, uint32_t> ||
                                   std::same_as<std::remove_cvref_t<T>, uint64_t>;

/// Any integer type supported by Sparkplug B (signed or unsigned)
template <typename T>
concept SparkplugInteger = SparkplugSignedInteger<T> || SparkplugUnsignedInteger<T>;

/// Floating-point types supported by Sparkplug B
template <typename T>
concept SparkplugFloat =
    std::same_as<std::remove_cvref_t<T>, float> || std::same_as<std::remove_cvref_t<T>, double>;

/// Boolean type
template <typename T>
concept SparkplugBoolean = std::same_as<std::remove_cvref_t<T>, bool>;

/// String-like types supported by Sparkplug B
template <typename T>
concept SparkplugString =
    std::convertible_to<T, std::string_view> && !SparkplugBoolean<T> && !SparkplugInteger<T>;

/// Numeric types (integers and floats)
template <typename T>
concept SparkplugNumeric = SparkplugInteger<T> || SparkplugFloat<T>;

/// Any metric type supported by Sparkplug B
template <typename T>
concept SparkplugMetricType =
    SparkplugInteger<T> || SparkplugFloat<T> || SparkplugBoolean<T> || SparkplugString<T>;

namespace detail {

template <SparkplugMetricType T>
consteval DataType get_datatype() noexcept {
  using BaseT = std::remove_cvref_t<T>;
  if constexpr (std::is_same_v<BaseT, int8_t>)
    return DataType::Int8;
  else if constexpr (std::is_same_v<BaseT, int16_t>)
    return DataType::Int16;
  else if constexpr (std::is_same_v<BaseT, int32_t>)
    return DataType::Int32;
  else if constexpr (std::is_same_v<BaseT, int64_t>)
    return DataType::Int64;
  else if constexpr (std::is_same_v<BaseT, uint8_t>)
    return DataType::UInt8;
  else if constexpr (std::is_same_v<BaseT, uint16_t>)
    return DataType::UInt16;
  else if constexpr (std::is_same_v<BaseT, uint32_t>)
    return DataType::UInt32;
  else if constexpr (std::is_same_v<BaseT, uint64_t>)
    return DataType::UInt64;
  else if constexpr (std::is_same_v<BaseT, float>)
    return DataType::Float;
  else if constexpr (std::is_same_v<BaseT, double>)
    return DataType::Double;
  else if constexpr (std::is_same_v<BaseT, bool>)
    return DataType::Boolean;
  else
    return DataType::String;
}

template <SparkplugMetricType T>
void set_metric_value(org::eclipse::tahu::protobuf::Payload::Metric* metric, T&& value) {
  using BaseT = std::remove_cvref_t<T>;

  if constexpr (std::is_same_v<BaseT, int8_t> || std::is_same_v<BaseT, int16_t> ||
                std::is_same_v<BaseT, int32_t> || std::is_same_v<BaseT, uint8_t> ||
                std::is_same_v<BaseT, uint16_t> || std::is_same_v<BaseT, uint32_t>) {
    metric->set_int_value(value);
  } else if constexpr (std::is_same_v<BaseT, int64_t> || std::is_same_v<BaseT, uint64_t>) {
    metric->set_long_value(value);
  } else if constexpr (std::is_same_v<BaseT, float>) {
    metric->set_float_value(value);
  } else if constexpr (std::is_same_v<BaseT, double>) {
    metric->set_double_value(value);
  } else if constexpr (std::is_same_v<BaseT, bool>) {
    metric->set_boolean_value(value);
  } else {
    // Handle all string-like types
    metric->set_string_value(std::string(value));
  }
}

template <SparkplugMetricType T>
void add_metric_to_payload(org::eclipse::tahu::protobuf::Payload& payload, std::string_view name,
                           T&& value, std::optional<uint64_t> alias,
                           std::optional<uint64_t> timestamp_ms) {
  auto* metric = payload.add_metrics();

  if (!name.empty()) {
    metric->set_name(std::string(name));
  }
  if (alias.has_value()) {
    metric->set_alias(*alias);
  }

  metric->set_datatype(std::to_underlying(get_datatype<T>()));
  set_metric_value(metric, std::forward<T>(value));

  // Use provided timestamp or current time
  uint64_t ts;
  if (timestamp_ms.has_value()) {
    ts = *timestamp_ms;
  } else {
    auto now = std::chrono::system_clock::now();
    ts = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  }
  metric->set_timestamp(ts);
}

} // namespace detail

/**
 * @brief Type-safe builder for Sparkplug B payloads with automatic type detection.
 *
 * PayloadBuilder provides a fluent API for constructing Sparkplug B payloads
 * with compile-time type safety. It automatically:
 * - Maps C++ types to Sparkplug DataTypes
 * - Generates timestamps (milliseconds since Unix epoch)
 * - Manages sequence numbers (when used with Publisher)
 *
 * @par Supported Types
 * - Integers: int8_t, int16_t, int32_t, int64_t, uint8_t, uint16_t, uint32_t, uint64_t
 * - Floating-point: float, double
 * - Boolean: bool
 * - String: std::string, std::string_view, const char*
 *
 * @par Usage Patterns
 *
 * **NBIRTH messages:**
 * Use add_metric_with_alias() to establish metric names with their aliases.
 * @code
 * sparkplug::PayloadBuilder birth;
 * birth.add_metric_with_alias("Temperature", 1, 20.5);
 * birth.add_metric_with_alias("Pressure", 2, 101.3);
 * publisher.publish_birth(birth);
 * @endcode
 *
 * **NDATA messages:**
 * Use add_metric_by_alias() for bandwidth-efficient updates (Report by Exception).
 * @code
 * sparkplug::PayloadBuilder data;
 * data.add_metric_by_alias(1, 21.0);  // Only Temperature changed
 * publisher.publish_data(data);
 * @endcode
 *
 * @see Publisher::publish_birth()
 * @see Publisher::publish_data()
 */
class PayloadBuilder {
public:
  /**
   * @brief Constructs an empty payload.
   */
  PayloadBuilder();

  /**
   * @brief Adds a metric by name only (for NBIRTH without aliases).
   *
   * @tparam T Value type (automatically deduced, must satisfy SparkplugMetricType)
   * @param name Metric name
   * @param value Metric value
   *
   * @return Reference to this builder for method chaining
   *
   * @note Timestamp is automatically generated.
   */
  template <SparkplugMetricType T>
  PayloadBuilder& add_metric(std::string_view name, T&& value) {
    detail::add_metric_to_payload(payload_, name, std::forward<T>(value), std::nullopt,
                                  std::nullopt);
    return *this;
  }

  /**
   * @brief Adds a metric by name with a custom timestamp.
   *
   * @tparam T Value type (automatically deduced, must satisfy SparkplugMetricType)
   * @param name Metric name
   * @param value Metric value
   * @param timestamp_ms Custom timestamp in milliseconds since Unix epoch
   *
   * @return Reference to this builder for method chaining
   *
   * @note Useful for historical data or backdated metrics.
   */
  template <SparkplugMetricType T>
  PayloadBuilder& add_metric(std::string_view name, T&& value, uint64_t timestamp_ms) {
    detail::add_metric_to_payload(payload_, name, std::forward<T>(value), std::nullopt,
                                  timestamp_ms);
    return *this;
  }

  /**
   * @brief Adds a metric with both name and alias (for NBIRTH messages).
   *
   * @tparam T Value type (automatically deduced, must satisfy SparkplugMetricType)
   * @param name Metric name
   * @param alias Metric alias (numeric identifier for NDATA)
   * @param value Metric value
   *
   * @return Reference to this builder for method chaining
   *
   * @note This establishes the name-to-alias mapping for subsequent NDATA messages.
   */
  template <SparkplugMetricType T>
  PayloadBuilder& add_metric_with_alias(std::string_view name, uint64_t alias, T&& value) {
    detail::add_metric_to_payload(payload_, name, std::forward<T>(value), alias, std::nullopt);
    return *this;
  }

  /**
   * @brief Adds a metric by alias only (for NDATA messages).
   *
   * @tparam T Value type (automatically deduced, must satisfy SparkplugMetricType)
   * @param alias Metric alias (must be established in NBIRTH)
   * @param value Metric value
   *
   * @return Reference to this builder for method chaining
   *
   * @note Only include metrics that have changed (Report by Exception).
   * @note Reduces bandwidth by 60-80% vs. using full metric names.
   */
  template <SparkplugMetricType T>
  PayloadBuilder& add_metric_by_alias(uint64_t alias, T&& value) {
    detail::add_metric_to_payload(payload_, "", std::forward<T>(value), alias, std::nullopt);
    return *this;
  }

  /**
   * @brief Adds a metric by alias with a custom timestamp.
   *
   * @tparam T Value type (automatically deduced, must satisfy SparkplugMetricType)
   * @param alias Metric alias
   * @param value Metric value
   * @param timestamp_ms Custom timestamp in milliseconds since Unix epoch
   *
   * @return Reference to this builder for method chaining
   *
   * @note Useful for historical data with specific timestamps.
   */
  template <SparkplugMetricType T>
  PayloadBuilder& add_metric_by_alias(uint64_t alias, T&& value, uint64_t timestamp_ms) {
    detail::add_metric_to_payload(payload_, "", std::forward<T>(value), alias, timestamp_ms);
    return *this;
  }

  /**
   * @brief Sets the payload-level timestamp.
   *
   * @param ts Timestamp in milliseconds since Unix epoch
   *
   * @return Reference to this builder for method chaining
   *
   * @note Usually not needed; Publisher adds this automatically.
   */
  PayloadBuilder& set_timestamp(uint64_t ts) {
    payload_.set_timestamp(ts);
    timestamp_explicitly_set_ = true;
    return *this;
  }

  /**
   * @brief Sets the sequence number manually.
   *
   * @param seq Sequence number (0-255)
   *
   * @return Reference to this builder for method chaining
   *
   * @warning Do not use in normal operation; Publisher manages this automatically.
   */
  PayloadBuilder& set_seq(uint64_t seq) {
    payload_.set_seq(seq);
    seq_explicitly_set_ = true;
    return *this;
  }

  // Add Node Control metrics (convenience methods for NBIRTH)
  PayloadBuilder& add_node_control_rebirth(bool value = false) {
    add_metric("Node Control/Rebirth", value);
    return *this;
  }

  PayloadBuilder& add_node_control_reboot(bool value = false) {
    add_metric("Node Control/Reboot", value);
    return *this;
  }

  PayloadBuilder& add_node_control_next_server(bool value = false) {
    add_metric("Node Control/Next Server", value);
    return *this;
  }

  PayloadBuilder& add_node_control_scan_rate(int64_t value) {
    add_metric("Node Control/Scan Rate", value);
    return *this;
  }

  // Query methods
  [[nodiscard]] bool has_seq() const noexcept {
    return seq_explicitly_set_;
  }
  [[nodiscard]] bool has_timestamp() const noexcept {
    return timestamp_explicitly_set_;
  }

  // Build and access
  [[nodiscard]] std::vector<uint8_t> build() const;
  [[nodiscard]] const org::eclipse::tahu::protobuf::Payload& payload() const noexcept;
  [[nodiscard]] org::eclipse::tahu::protobuf::Payload& mutable_payload() noexcept {
    return payload_;
  }

private:
  org::eclipse::tahu::protobuf::Payload payload_;
  bool seq_explicitly_set_{false};
  bool timestamp_explicitly_set_{false};
};

} // namespace sparkplug