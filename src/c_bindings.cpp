// src/c_bindings.cpp
#include "sparkplug/edge_node.hpp"
#include "sparkplug/host_application.hpp"
#include "sparkplug/payload_builder.hpp"
#include "sparkplug/sparkplug_c.h"

#include <cstring>
#include <memory>

struct sparkplug_publisher {
  sparkplug::EdgeNode impl;
};

struct sparkplug_subscriber {
  std::unique_ptr<sparkplug::HostApplication> impl;
  std::string default_group_id; // For backwards compatibility with old C API
  sparkplug_message_callback_t callback;
  sparkplug_command_callback_t command_callback;
  sparkplug_log_callback_t log_callback;
  void* user_data;
  void* command_user_data;
  void* log_user_data;
};

struct sparkplug_payload {
  sparkplug::PayloadBuilder impl;
};

struct sparkplug_host_application {
  sparkplug::HostApplication impl;
};

static void copy_metrics_to_builder(sparkplug::PayloadBuilder& builder,
                                    const org::eclipse::tahu::protobuf::Payload& proto_payload,
                                    bool copy_seq = true) {
  if (proto_payload.has_timestamp()) {
    builder.set_timestamp(proto_payload.timestamp());
  }

  if (copy_seq && proto_payload.has_seq()) {
    builder.set_seq(proto_payload.seq());
  }

  for (const auto& metric : proto_payload.metrics()) {
    const char* name = metric.has_name() ? metric.name().c_str() : "";
    std::optional<uint64_t> alias =
        metric.has_alias() ? std::optional<uint64_t>(metric.alias()) : std::nullopt;

    auto datatype = static_cast<sparkplug::DataType>(metric.datatype());

    switch (datatype) {
    case sparkplug::DataType::Int8:
    case sparkplug::DataType::Int16:
    case sparkplug::DataType::Int32:
      if (alias.has_value() && std::string(name).empty()) {
        builder.add_metric_by_alias(*alias, static_cast<int32_t>(metric.int_value()));
      } else if (alias.has_value()) {
        builder.add_metric_with_alias(name, *alias, static_cast<int32_t>(metric.int_value()));
      } else {
        builder.add_metric(name, static_cast<int32_t>(metric.int_value()));
      }
      break;

    case sparkplug::DataType::Int64:
      if (alias.has_value() && std::string(name).empty()) {
        builder.add_metric_by_alias(*alias, metric.long_value());
      } else if (alias.has_value()) {
        builder.add_metric_with_alias(name, *alias, metric.long_value());
      } else {
        builder.add_metric(name, metric.long_value());
      }
      break;

    case sparkplug::DataType::UInt8:
    case sparkplug::DataType::UInt16:
    case sparkplug::DataType::UInt32:
      if (alias.has_value() && std::string(name).empty()) {
        builder.add_metric_by_alias(*alias, static_cast<uint32_t>(metric.int_value()));
      } else if (alias.has_value()) {
        builder.add_metric_with_alias(name, *alias, static_cast<uint32_t>(metric.int_value()));
      } else {
        builder.add_metric(name, static_cast<uint32_t>(metric.int_value()));
      }
      break;

    case sparkplug::DataType::UInt64:
      if (alias.has_value() && std::string(name).empty()) {
        builder.add_metric_by_alias(*alias, static_cast<uint64_t>(metric.long_value()));
      } else if (alias.has_value()) {
        builder.add_metric_with_alias(name, *alias, static_cast<uint64_t>(metric.long_value()));
      } else {
        builder.add_metric(name, static_cast<uint64_t>(metric.long_value()));
      }
      break;

    case sparkplug::DataType::Float:
      if (alias.has_value() && std::string(name).empty()) {
        builder.add_metric_by_alias(*alias, metric.float_value());
      } else if (alias.has_value()) {
        builder.add_metric_with_alias(name, *alias, metric.float_value());
      } else {
        builder.add_metric(name, metric.float_value());
      }
      break;

    case sparkplug::DataType::Double:
      if (alias.has_value() && std::string(name).empty()) {
        builder.add_metric_by_alias(*alias, metric.double_value());
      } else if (alias.has_value()) {
        builder.add_metric_with_alias(name, *alias, metric.double_value());
      } else {
        builder.add_metric(name, metric.double_value());
      }
      break;

    case sparkplug::DataType::Boolean:
      if (alias.has_value() && std::string(name).empty()) {
        builder.add_metric_by_alias(*alias, metric.boolean_value());
      } else if (alias.has_value()) {
        builder.add_metric_with_alias(name, *alias, metric.boolean_value());
      } else {
        builder.add_metric(name, metric.boolean_value());
      }
      break;

    case sparkplug::DataType::String:
    case sparkplug::DataType::Text:
      if (alias.has_value() && std::string(name).empty()) {
        builder.add_metric_by_alias(*alias, metric.string_value());
      } else if (alias.has_value()) {
        builder.add_metric_with_alias(name, *alias, metric.string_value());
      } else {
        builder.add_metric(name, metric.string_value());
      }
      break;

    default:
      break;
    }
  }
}

extern "C" {

// ============================================================================
// Publisher Functions
// ============================================================================

sparkplug_publisher_t* sparkplug_publisher_create(const char* broker_url, const char* client_id,
                                                  const char* group_id, const char* edge_node_id) {
  if (!broker_url || !client_id || !group_id || !edge_node_id) {
    return nullptr;
  }

  sparkplug::EdgeNode::Config config{.broker_url = broker_url,
                                     .client_id = client_id,
                                     .group_id = group_id,
                                     .edge_node_id = edge_node_id};
  return new sparkplug_publisher{sparkplug::EdgeNode(std::move(config))};
}

void sparkplug_publisher_destroy(sparkplug_publisher_t* pub) {
  delete pub;
}

int sparkplug_publisher_set_credentials(sparkplug_publisher_t* pub, const char* username,
                                        const char* password) {
  if (!pub) {
    return -1;
  }

  std::optional<std::string> user = username ? std::optional<std::string>(username) : std::nullopt;
  std::optional<std::string> pass = password ? std::optional<std::string>(password) : std::nullopt;
  pub->impl.set_credentials(std::move(user), std::move(pass));
  return 0;
}

int sparkplug_publisher_set_tls(sparkplug_publisher_t* pub, const char* trust_store,
                                const char* key_store, const char* private_key,
                                const char* private_key_password, int enable_server_cert_auth) {
  if (!pub || !trust_store) {
    return -1;
  }

  sparkplug::EdgeNode::TlsOptions tls{
      .trust_store = trust_store,
      .key_store = key_store ? std::string(key_store) : "",
      .private_key = private_key ? std::string(private_key) : "",
      .private_key_password = private_key_password ? std::string(private_key_password) : "",
      .enabled_cipher_suites = "",
      .enable_server_cert_auth = enable_server_cert_auth != 0};

  pub->impl.set_tls(tls);
  return 0;
}

int sparkplug_publisher_connect(sparkplug_publisher_t* pub) {
  if (!pub)
    return -1;
  return pub->impl.connect().has_value() ? 0 : -1;
}

int sparkplug_publisher_disconnect(sparkplug_publisher_t* pub) {
  if (!pub)
    return -1;
  return pub->impl.disconnect().has_value() ? 0 : -1;
}

int sparkplug_publisher_publish_birth(sparkplug_publisher_t* pub, const uint8_t* payload_data,
                                      size_t payload_len) {
  if (!pub || !payload_data)
    return -1;

  org::eclipse::tahu::protobuf::Payload proto_payload;
  if (!proto_payload.ParseFromArray(payload_data, static_cast<int>(payload_len))) {
    return -1;
  }

  sparkplug::PayloadBuilder builder;
  copy_metrics_to_builder(builder, proto_payload);

  return pub->impl.publish_birth(builder).has_value() ? 0 : -1;
}

int sparkplug_publisher_publish_data(sparkplug_publisher_t* pub, const uint8_t* payload_data,
                                     size_t payload_len) {
  if (!pub || !payload_data)
    return -1;

  org::eclipse::tahu::protobuf::Payload proto_payload;
  if (!proto_payload.ParseFromArray(payload_data, static_cast<int>(payload_len))) {
    return -1;
  }

  sparkplug::PayloadBuilder builder;
  copy_metrics_to_builder(builder, proto_payload, false);

  return pub->impl.publish_data(builder).has_value() ? 0 : -1;
}

int sparkplug_publisher_publish_death(sparkplug_publisher_t* pub) {
  if (!pub)
    return -1;
  return pub->impl.publish_death().has_value() ? 0 : -1;
}

int sparkplug_publisher_rebirth(sparkplug_publisher_t* pub) {
  if (!pub)
    return -1;
  return pub->impl.rebirth().has_value() ? 0 : -1;
}

uint64_t sparkplug_publisher_get_seq(const sparkplug_publisher_t* pub) {
  if (!pub)
    return 0;
  return pub->impl.get_seq();
}

uint64_t sparkplug_publisher_get_bd_seq(const sparkplug_publisher_t* pub) {
  if (!pub)
    return 0;
  return pub->impl.get_bd_seq();
}

int sparkplug_publisher_publish_device_birth(sparkplug_publisher_t* pub, const char* device_id,
                                             const uint8_t* payload_data, size_t payload_len) {
  if (!pub || !device_id || !payload_data)
    return -1;

  org::eclipse::tahu::protobuf::Payload proto_payload;
  if (!proto_payload.ParseFromArray(payload_data, static_cast<int>(payload_len))) {
    return -1;
  }

  sparkplug::PayloadBuilder builder;
  copy_metrics_to_builder(builder, proto_payload);

  return pub->impl.publish_device_birth(device_id, builder).has_value() ? 0 : -1;
}

int sparkplug_publisher_publish_device_data(sparkplug_publisher_t* pub, const char* device_id,
                                            const uint8_t* payload_data, size_t payload_len) {
  if (!pub || !device_id || !payload_data)
    return -1;

  org::eclipse::tahu::protobuf::Payload proto_payload;
  if (!proto_payload.ParseFromArray(payload_data, static_cast<int>(payload_len))) {
    return -1;
  }

  sparkplug::PayloadBuilder builder;
  copy_metrics_to_builder(builder, proto_payload, false);

  return pub->impl.publish_device_data(device_id, builder).has_value() ? 0 : -1;
}

int sparkplug_publisher_publish_device_death(sparkplug_publisher_t* pub, const char* device_id) {
  if (!pub || !device_id)
    return -1;
  return pub->impl.publish_device_death(device_id).has_value() ? 0 : -1;
}

int sparkplug_publisher_publish_node_command(sparkplug_publisher_t* pub,
                                             const char* target_edge_node_id,
                                             const uint8_t* payload_data, size_t payload_len) {
  if (!pub || !target_edge_node_id || !payload_data)
    return -1;

  org::eclipse::tahu::protobuf::Payload proto_payload;
  if (!proto_payload.ParseFromArray(payload_data, static_cast<int>(payload_len))) {
    return -1;
  }

  sparkplug::PayloadBuilder builder;
  copy_metrics_to_builder(builder, proto_payload);

  return pub->impl.publish_node_command(target_edge_node_id, builder).has_value() ? 0 : -1;
}

int sparkplug_publisher_publish_device_command(sparkplug_publisher_t* pub,
                                               const char* target_edge_node_id,
                                               const char* target_device_id,
                                               const uint8_t* payload_data, size_t payload_len) {
  if (!pub || !target_edge_node_id || !target_device_id || !payload_data)
    return -1;

  org::eclipse::tahu::protobuf::Payload proto_payload;
  if (!proto_payload.ParseFromArray(payload_data, static_cast<int>(payload_len))) {
    return -1;
  }

  sparkplug::PayloadBuilder builder;
  copy_metrics_to_builder(builder, proto_payload);

  return pub->impl.publish_device_command(target_edge_node_id, target_device_id, builder)
                 .has_value()
             ? 0
             : -1;
}

int sparkplug_publisher_publish_state_birth(sparkplug_publisher_t* pub, const char* host_id,
                                            uint64_t timestamp) {
  if (!pub || !host_id)
    return -1;
  return pub->impl.publish_state_birth(host_id, timestamp).has_value() ? 0 : -1;
}

int sparkplug_publisher_publish_state_death(sparkplug_publisher_t* pub, const char* host_id,
                                            uint64_t timestamp) {
  if (!pub || !host_id)
    return -1;
  return pub->impl.publish_state_death(host_id, timestamp).has_value() ? 0 : -1;
}

// ============================================================================
// Subscriber Functions
// ============================================================================

sparkplug_subscriber_t* sparkplug_subscriber_create(const char* broker_url, const char* client_id,
                                                    const char* group_id,
                                                    sparkplug_message_callback_t callback,
                                                    void* user_data) {
  if (!broker_url || !client_id || !group_id || !callback) {
    return nullptr;
  }

  auto* sub = new sparkplug_subscriber;
  sub->default_group_id = group_id;
  sub->callback = callback;
  sub->command_callback = nullptr;
  sub->log_callback = nullptr;
  sub->user_data = user_data;
  sub->command_user_data = nullptr;
  sub->log_user_data = nullptr;

  sparkplug::LogCallback log_wrapper = [sub](sparkplug::LogLevel level, std::string_view message) {
    if (sub->log_callback) {
      int c_level = static_cast<int>(level);
      sub->log_callback(c_level, message.data(), message.size(), sub->log_user_data);
    }
  };

  sparkplug::MessageCallback message_handler =
      [sub](const sparkplug::Topic& topic, const org::eclipse::tahu::protobuf::Payload& payload) {
        std::vector<uint8_t> data(payload.ByteSizeLong());
        payload.SerializeToArray(data.data(), static_cast<int>(data.size()));

        auto topic_str = topic.to_string();

        // Check if this is a command message and if command_callback is set
        if ((topic.message_type == sparkplug::MessageType::NCMD ||
             topic.message_type == sparkplug::MessageType::DCMD) &&
            sub->command_callback) {
          sub->command_callback(topic_str.c_str(), data.data(), data.size(),
                                sub->command_user_data);
        } else {
          sub->callback(topic_str.c_str(), data.data(), data.size(), sub->user_data);
        }
      };

  sparkplug::HostApplication::Config config{
      .broker_url = broker_url,
      .client_id = client_id,
      .host_id = group_id, // Use group_id as host_id for compatibility
      .message_callback = std::move(message_handler),
      .log_callback = std::move(log_wrapper)};

  sub->impl = std::make_unique<sparkplug::HostApplication>(std::move(config));

  return sub;
}

void sparkplug_subscriber_destroy(sparkplug_subscriber_t* sub) {
  delete sub;
}

int sparkplug_subscriber_set_credentials(sparkplug_subscriber_t* sub, const char* username,
                                         const char* password) {
  if (!sub || !sub->impl) {
    return -1;
  }

  // NOTE: HostApplication requires credentials to be set in Config at construction time
  // This function is kept for C API compatibility but returns -1 (unsupported)
  (void)username;
  (void)password;
  return -1;
}

int sparkplug_subscriber_set_tls(sparkplug_subscriber_t* sub, const char* trust_store,
                                 const char* key_store, const char* private_key,
                                 const char* private_key_password, int enable_server_cert_auth) {
  if (!sub || !sub->impl || !trust_store) {
    return -1;
  }

  // NOTE: HostApplication requires TLS to be set in Config at construction time
  // This function is kept for C API compatibility but returns -1 (unsupported)
  (void)trust_store;
  (void)key_store;
  (void)private_key;
  (void)private_key_password;
  (void)enable_server_cert_auth;
  return -1;
}

int sparkplug_subscriber_connect(sparkplug_subscriber_t* sub) {
  if (!sub || !sub->impl)
    return -1;
  return sub->impl->connect().has_value() ? 0 : -1;
}

int sparkplug_subscriber_disconnect(sparkplug_subscriber_t* sub) {
  if (!sub || !sub->impl)
    return -1;
  return sub->impl->disconnect().has_value() ? 0 : -1;
}

int sparkplug_subscriber_subscribe_all(sparkplug_subscriber_t* sub) {
  if (!sub || !sub->impl)
    return -1;
  return sub->impl->subscribe_all_groups().has_value() ? 0 : -1;
}

int sparkplug_subscriber_subscribe_node(sparkplug_subscriber_t* sub, const char* edge_node_id) {
  if (!sub || !sub->impl || !edge_node_id)
    return -1;
  return sub->impl->subscribe_node(sub->default_group_id, edge_node_id).has_value() ? 0 : -1;
}

int sparkplug_subscriber_subscribe_group(sparkplug_subscriber_t* sub, const char* group_id) {
  if (!sub || !sub->impl || !group_id)
    return -1;
  return sub->impl->subscribe_group(group_id).has_value() ? 0 : -1;
}

int sparkplug_subscriber_subscribe_state(sparkplug_subscriber_t* sub, const char* host_id) {
  if (!sub || !sub->impl || !host_id)
    return -1;
  return sub->impl->subscribe_state(host_id).has_value() ? 0 : -1;
}

void sparkplug_subscriber_set_log_callback(sparkplug_subscriber_t* sub,
                                           sparkplug_log_callback_t callback, void* user_data) {
  if (!sub) {
    return;
  }

  sub->log_callback = callback;
  sub->log_user_data = user_data;
}

void sparkplug_subscriber_set_command_callback(sparkplug_subscriber_t* sub,
                                               sparkplug_command_callback_t callback,
                                               void* user_data) {
  if (!sub || !sub->impl) {
    return;
  }

  sub->command_callback = callback;
  sub->command_user_data = user_data;

  // NOTE: HostApplication doesn't have a separate set_command_callback method
  // Commands (NCMD/DCMD) are received through the main message_callback
  // which now dispatches to command_callback if set (see sparkplug_subscriber_create)
}

int sparkplug_subscriber_get_metric_name(sparkplug_subscriber_t* sub, const char* group_id,
                                         const char* edge_node_id, const char* device_id,
                                         uint64_t alias, char* name_buffer, size_t buffer_size) {
  if (!sub || !sub->impl || !group_id || !edge_node_id || !name_buffer || buffer_size == 0) {
    return -1;
  }

  // Handle NULL device_id as empty string
  std::string_view device_id_view = device_id ? device_id : "";

  auto name = sub->impl->get_metric_name(group_id, edge_node_id, device_id_view, alias);

  if (!name) {
    // Alias not found or node/device not seen yet
    return 0;
  }

  // Check if buffer is large enough (including null terminator)
  if (name->size() + 1 > buffer_size) {
    return -1;
  }

  // Copy name to buffer and null-terminate
  std::memcpy(name_buffer, name->data(), name->size());
  name_buffer[name->size()] = '\0';

  return static_cast<int>(name->size() + 1);
}

// ============================================================================
// Payload Functions
// ============================================================================

sparkplug_payload_t* sparkplug_payload_create() {
  return new sparkplug_payload{sparkplug::PayloadBuilder()};
}

void sparkplug_payload_destroy(sparkplug_payload_t* payload) {
  delete payload;
}

void sparkplug_payload_set_timestamp(sparkplug_payload_t* payload, uint64_t ts) {
  if (payload) {
    payload->impl.set_timestamp(ts);
  }
}

void sparkplug_payload_set_seq(sparkplug_payload_t* payload, uint64_t seq) {
  if (payload) {
    payload->impl.set_seq(seq);
  }
}

// Add metrics by name (for NBIRTH)
void sparkplug_payload_add_int8(sparkplug_payload_t* payload, const char* name, int8_t value) {
  if (payload && name)
    payload->impl.add_metric(name, value);
}

void sparkplug_payload_add_int16(sparkplug_payload_t* payload, const char* name, int16_t value) {
  if (payload && name)
    payload->impl.add_metric(name, value);
}

void sparkplug_payload_add_int32(sparkplug_payload_t* payload, const char* name, int32_t value) {
  if (payload && name)
    payload->impl.add_metric(name, value);
}

void sparkplug_payload_add_int64(sparkplug_payload_t* payload, const char* name, int64_t value) {
  if (payload && name)
    payload->impl.add_metric(name, value);
}

void sparkplug_payload_add_uint8(sparkplug_payload_t* payload, const char* name, uint8_t value) {
  if (payload && name)
    payload->impl.add_metric(name, value);
}

void sparkplug_payload_add_uint16(sparkplug_payload_t* payload, const char* name, uint16_t value) {
  if (payload && name)
    payload->impl.add_metric(name, value);
}

void sparkplug_payload_add_uint32(sparkplug_payload_t* payload, const char* name, uint32_t value) {
  if (payload && name)
    payload->impl.add_metric(name, value);
}

void sparkplug_payload_add_uint64(sparkplug_payload_t* payload, const char* name, uint64_t value) {
  if (payload && name)
    payload->impl.add_metric(name, value);
}

void sparkplug_payload_add_float(sparkplug_payload_t* payload, const char* name, float value) {
  if (payload && name)
    payload->impl.add_metric(name, value);
}

void sparkplug_payload_add_double(sparkplug_payload_t* payload, const char* name, double value) {
  if (payload && name)
    payload->impl.add_metric(name, value);
}

void sparkplug_payload_add_bool(sparkplug_payload_t* payload, const char* name, bool value) {
  if (payload && name)
    payload->impl.add_metric(name, value);
}

void sparkplug_payload_add_string(sparkplug_payload_t* payload, const char* name,
                                  const char* value) {
  if (payload && name && value)
    payload->impl.add_metric(name, std::string_view(value));
}

// Add metrics with alias (for NBIRTH with aliases)
void sparkplug_payload_add_int32_with_alias(sparkplug_payload_t* payload, const char* name,
                                            uint64_t alias, int32_t value) {
  if (payload && name)
    payload->impl.add_metric_with_alias(name, alias, value);
}

void sparkplug_payload_add_int64_with_alias(sparkplug_payload_t* payload, const char* name,
                                            uint64_t alias, int64_t value) {
  if (payload && name)
    payload->impl.add_metric_with_alias(name, alias, value);
}

void sparkplug_payload_add_uint32_with_alias(sparkplug_payload_t* payload, const char* name,
                                             uint64_t alias, uint32_t value) {
  if (payload && name)
    payload->impl.add_metric_with_alias(name, alias, value);
}

void sparkplug_payload_add_uint64_with_alias(sparkplug_payload_t* payload, const char* name,
                                             uint64_t alias, uint64_t value) {
  if (payload && name)
    payload->impl.add_metric_with_alias(name, alias, value);
}

void sparkplug_payload_add_float_with_alias(sparkplug_payload_t* payload, const char* name,
                                            uint64_t alias, float value) {
  if (payload && name)
    payload->impl.add_metric_with_alias(name, alias, value);
}

void sparkplug_payload_add_double_with_alias(sparkplug_payload_t* payload, const char* name,
                                             uint64_t alias, double value) {
  if (payload && name)
    payload->impl.add_metric_with_alias(name, alias, value);
}

void sparkplug_payload_add_bool_with_alias(sparkplug_payload_t* payload, const char* name,
                                           uint64_t alias, bool value) {
  if (payload && name)
    payload->impl.add_metric_with_alias(name, alias, value);
}

// Add metrics by alias only (for NDATA)
void sparkplug_payload_add_int32_by_alias(sparkplug_payload_t* payload, uint64_t alias,
                                          int32_t value) {
  if (payload)
    payload->impl.add_metric_by_alias(alias, value);
}

void sparkplug_payload_add_int64_by_alias(sparkplug_payload_t* payload, uint64_t alias,
                                          int64_t value) {
  if (payload)
    payload->impl.add_metric_by_alias(alias, value);
}

void sparkplug_payload_add_uint32_by_alias(sparkplug_payload_t* payload, uint64_t alias,
                                           uint32_t value) {
  if (payload)
    payload->impl.add_metric_by_alias(alias, value);
}

void sparkplug_payload_add_uint64_by_alias(sparkplug_payload_t* payload, uint64_t alias,
                                           uint64_t value) {
  if (payload)
    payload->impl.add_metric_by_alias(alias, value);
}

void sparkplug_payload_add_float_by_alias(sparkplug_payload_t* payload, uint64_t alias,
                                          float value) {
  if (payload)
    payload->impl.add_metric_by_alias(alias, value);
}

void sparkplug_payload_add_double_by_alias(sparkplug_payload_t* payload, uint64_t alias,
                                           double value) {
  if (payload)
    payload->impl.add_metric_by_alias(alias, value);
}

void sparkplug_payload_add_bool_by_alias(sparkplug_payload_t* payload, uint64_t alias, bool value) {
  if (payload)
    payload->impl.add_metric_by_alias(alias, value);
}

size_t sparkplug_payload_serialize(const sparkplug_payload_t* payload, uint8_t* buffer,
                                   size_t buffer_size) {
  if (!payload || !buffer)
    return 0;

  auto data = payload->impl.build();
  if (data.size() > buffer_size) {
    return 0;
  }

  std::memcpy(buffer, data.data(), data.size());
  return data.size();
}

// ============================================================================
// Payload Parsing and Reading Functions
// ============================================================================

sparkplug_payload_t* sparkplug_payload_parse(const uint8_t* data, size_t data_len) {
  if (!data || data_len == 0)
    return nullptr;

  org::eclipse::tahu::protobuf::Payload proto_payload;
  if (!proto_payload.ParseFromArray(data, static_cast<int>(data_len))) {
    return nullptr;
  }

  // Create a new payload and copy the parsed data
  auto* payload = new sparkplug_payload{sparkplug::PayloadBuilder()};
  copy_metrics_to_builder(payload->impl, proto_payload);

  return payload;
}

bool sparkplug_payload_get_timestamp(const sparkplug_payload_t* payload, uint64_t* out_timestamp) {
  if (!payload || !out_timestamp)
    return false;

  auto& proto_payload = const_cast<sparkplug_payload_t*>(payload)->impl.mutable_payload();
  if (proto_payload.has_timestamp()) {
    *out_timestamp = proto_payload.timestamp();
    return true;
  }

  return false;
}

bool sparkplug_payload_get_seq(const sparkplug_payload_t* payload, uint64_t* out_seq) {
  if (!payload || !out_seq)
    return false;

  auto& proto_payload = const_cast<sparkplug_payload_t*>(payload)->impl.mutable_payload();
  if (proto_payload.has_seq()) {
    *out_seq = proto_payload.seq();
    return true;
  }

  return false;
}

const char* sparkplug_payload_get_uuid(const sparkplug_payload_t* payload) {
  if (!payload)
    return nullptr;

  auto& proto_payload = const_cast<sparkplug_payload_t*>(payload)->impl.mutable_payload();
  if (proto_payload.has_uuid()) {
    return proto_payload.uuid().c_str();
  }

  return nullptr;
}

size_t sparkplug_payload_get_metric_count(const sparkplug_payload_t* payload) {
  if (!payload)
    return 0;

  auto& proto_payload = const_cast<sparkplug_payload_t*>(payload)->impl.mutable_payload();
  return static_cast<size_t>(proto_payload.metrics_size());
}

bool sparkplug_payload_get_metric_at(const sparkplug_payload_t* payload, size_t index,
                                     sparkplug_metric_t* out_metric) {
  if (!payload || !out_metric)
    return false;

  auto& proto_payload = const_cast<sparkplug_payload_t*>(payload)->impl.mutable_payload();
  if (index >= static_cast<size_t>(proto_payload.metrics_size())) {
    return false;
  }

  const auto& metric = proto_payload.metrics(static_cast<int>(index));

  // Clear the output struct
  std::memset(out_metric, 0, sizeof(sparkplug_metric_t));

  // Set name
  out_metric->has_name = metric.has_name();
  out_metric->name = out_metric->has_name ? metric.name().c_str() : nullptr;

  // Set alias
  out_metric->has_alias = metric.has_alias();
  out_metric->alias = out_metric->has_alias ? metric.alias() : 0;

  // Set timestamp
  out_metric->has_timestamp = metric.has_timestamp();
  out_metric->timestamp = out_metric->has_timestamp ? metric.timestamp() : 0;

  // Set is_null
  out_metric->is_null = metric.has_is_null() ? metric.is_null() : false;

  // Set datatype
  out_metric->datatype =
      static_cast<sparkplug_data_type_t>(metric.has_datatype() ? metric.datatype() : 0);

  // Set value based on datatype (only if not null)
  if (!out_metric->is_null) {
    switch (out_metric->datatype) {
    case SPARKPLUG_DATA_TYPE_INT8:
    case SPARKPLUG_DATA_TYPE_INT16:
    case SPARKPLUG_DATA_TYPE_INT32:
      out_metric->value.int32_value = static_cast<int32_t>(metric.int_value());
      break;

    case SPARKPLUG_DATA_TYPE_INT64:
      out_metric->value.int64_value = static_cast<int64_t>(metric.long_value());
      break;

    case SPARKPLUG_DATA_TYPE_UINT8:
    case SPARKPLUG_DATA_TYPE_UINT16:
    case SPARKPLUG_DATA_TYPE_UINT32:
      out_metric->value.uint32_value = static_cast<uint32_t>(metric.int_value());
      break;

    case SPARKPLUG_DATA_TYPE_UINT64:
      out_metric->value.uint64_value = static_cast<uint64_t>(metric.long_value());
      break;

    case SPARKPLUG_DATA_TYPE_FLOAT:
      out_metric->value.float_value = metric.float_value();
      break;

    case SPARKPLUG_DATA_TYPE_DOUBLE:
      out_metric->value.double_value = metric.double_value();
      break;

    case SPARKPLUG_DATA_TYPE_BOOLEAN:
      out_metric->value.boolean_value = metric.boolean_value();
      break;

    case SPARKPLUG_DATA_TYPE_STRING:
    case SPARKPLUG_DATA_TYPE_TEXT:
      out_metric->value.string_value = metric.string_value().c_str();
      break;

    default:
      // Unsupported type - leave value uninitialized
      break;
    }
  }

  return true;
}

/* ============================================================================
 * Host Application API Implementation
 * ========================================================================= */

sparkplug_host_application_t* sparkplug_host_application_create(const char* broker_url,
                                                                const char* client_id,
                                                                const char* host_id) {
  if (!broker_url || !client_id || !host_id) {
    return nullptr;
  }

  try {
    sparkplug::HostApplication::Config config{
        .broker_url = broker_url, .client_id = client_id, .host_id = host_id};

    auto* host =
        new sparkplug_host_application{.impl = sparkplug::HostApplication(std::move(config))};
    return host;
  } catch (...) {
    return nullptr;
  }
}

void sparkplug_host_application_destroy(sparkplug_host_application_t* host) {
  delete host;
}

int sparkplug_host_application_set_credentials(sparkplug_host_application_t* host,
                                               const char* username, const char* password) {
  if (!host) {
    return -1;
  }

  std::optional<std::string> user = username ? std::optional<std::string>(username) : std::nullopt;
  std::optional<std::string> pass = password ? std::optional<std::string>(password) : std::nullopt;
  host->impl.set_credentials(std::move(user), std::move(pass));
  return 0;
}

int sparkplug_host_application_set_tls(sparkplug_host_application_t* host, const char* trust_store,
                                       const char* key_store, const char* private_key,
                                       const char* private_key_password,
                                       int enable_server_cert_auth) {
  if (!host || !trust_store) {
    return -1;
  }

  sparkplug::HostApplication::TlsOptions tls{
      .trust_store = trust_store,
      .key_store = key_store ? std::string(key_store) : "",
      .private_key = private_key ? std::string(private_key) : "",
      .private_key_password = private_key_password ? std::string(private_key_password) : "",
      .enabled_cipher_suites = "",
      .enable_server_cert_auth = enable_server_cert_auth != 0};

  host->impl.set_tls(tls);
  return 0;
}

int sparkplug_host_application_connect(sparkplug_host_application_t* host) {
  if (!host) {
    return -1;
  }

  auto result = host->impl.connect();
  return result.has_value() ? 0 : -1;
}

int sparkplug_host_application_disconnect(sparkplug_host_application_t* host) {
  if (!host) {
    return -1;
  }

  auto result = host->impl.disconnect();
  return result.has_value() ? 0 : -1;
}

int sparkplug_host_application_publish_state_birth(sparkplug_host_application_t* host,
                                                   uint64_t timestamp) {
  if (!host) {
    return -1;
  }

  auto result = host->impl.publish_state_birth(timestamp);
  return result.has_value() ? 0 : -1;
}

int sparkplug_host_application_publish_state_death(sparkplug_host_application_t* host,
                                                   uint64_t timestamp) {
  if (!host) {
    return -1;
  }

  auto result = host->impl.publish_state_death(timestamp);
  return result.has_value() ? 0 : -1;
}

int sparkplug_host_application_publish_node_command(sparkplug_host_application_t* host,
                                                    const char* group_id,
                                                    const char* target_edge_node_id,
                                                    const uint8_t* payload_data,
                                                    size_t payload_len) {
  if (!host || !group_id || !target_edge_node_id || !payload_data) {
    return -1;
  }

  try {
    org::eclipse::tahu::protobuf::Payload proto_payload;
    if (!proto_payload.ParseFromArray(payload_data, static_cast<int>(payload_len))) {
      return -1;
    }

    sparkplug::PayloadBuilder builder;
    copy_metrics_to_builder(builder, proto_payload);

    auto result = host->impl.publish_node_command(group_id, target_edge_node_id, builder);
    return result.has_value() ? 0 : -1;
  } catch (...) {
    return -1;
  }
}

int sparkplug_host_application_publish_device_command(
    sparkplug_host_application_t* host, const char* group_id, const char* target_edge_node_id,
    const char* target_device_id, const uint8_t* payload_data, size_t payload_len) {
  if (!host || !group_id || !target_edge_node_id || !target_device_id || !payload_data) {
    return -1;
  }

  try {
    org::eclipse::tahu::protobuf::Payload proto_payload;
    if (!proto_payload.ParseFromArray(payload_data, static_cast<int>(payload_len))) {
      return -1;
    }

    sparkplug::PayloadBuilder builder;
    copy_metrics_to_builder(builder, proto_payload);

    auto result =
        host->impl.publish_device_command(group_id, target_edge_node_id, target_device_id, builder);
    return result.has_value() ? 0 : -1;
  } catch (...) {
    return -1;
  }
}

} // extern "C"
