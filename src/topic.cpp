// src/topic.cpp
#include "sparkplug/topic.hpp"

#include <format>
#include <ranges>
#include <utility>

namespace sparkplug {

namespace {

using namespace std::string_view_literals;

constexpr std::string_view message_type_to_string(MessageType type) noexcept {
  switch (type) {
  case MessageType::NBIRTH:
    return "NBIRTH";
  case MessageType::NDEATH:
    return "NDEATH";
  case MessageType::DBIRTH:
    return "DBIRTH";
  case MessageType::DDEATH:
    return "DDEATH";
  case MessageType::NDATA:
    return "NDATA";
  case MessageType::DDATA:
    return "DDATA";
  case MessageType::NCMD:
    return "NCMD";
  case MessageType::DCMD:
    return "DCMD";
  case MessageType::STATE:
    return "STATE";
  }
  std::unreachable();
}

stdx::expected<MessageType, std::string> parse_message_type(std::string_view str) {
  if (str == "NBIRTH")
    return MessageType::NBIRTH;
  if (str == "NDEATH")
    return MessageType::NDEATH;
  if (str == "DBIRTH")
    return MessageType::DBIRTH;
  if (str == "DDEATH")
    return MessageType::DDEATH;
  if (str == "NDATA")
    return MessageType::NDATA;
  if (str == "DDATA")
    return MessageType::DDATA;
  if (str == "NCMD")
    return MessageType::NCMD;
  if (str == "DCMD")
    return MessageType::DCMD;
  if (str == "STATE")
    return MessageType::STATE;
  return stdx::unexpected(std::format("Unknown message type: {}", str));
}

// Reject empty or oversized ids: these become permanent state keys and log
// content, so unvalidated attacker input would bloat or poison them.
bool valid_component(std::string_view id) {
  return !id.empty() && id.size() <= kMaxComponentLength;
}
} // namespace

std::string Topic::to_string() const {
  if (message_type == MessageType::STATE) {
    return std::format("{}/STATE/{}", NAMESPACE, edge_node_id);
  }

  auto base = std::format("{}/{}/{}/{}", NAMESPACE, group_id,
                          message_type_to_string(message_type), edge_node_id);

  if (!device_id.empty()) {
    return std::format("{}/{}", base, device_id);
  }
  return base;
}

stdx::expected<Topic, std::string> Topic::parse(std::string_view topic_str) {
  // Parse without allocating vector - use iterators directly
  auto parts = topic_str | std::views::split('/') | std::views::transform([](auto&& rng) {
                 return std::string_view(rng.begin(),
                                         std::ranges::distance(rng.begin(), rng.end()));
               });

  auto it = parts.begin();
  auto end = parts.end();

  if (it == end) {
    return stdx::unexpected("Invalid topic format");
  }

  std::string_view part0 = *it++;
  if (it == end) {
    return stdx::unexpected("Invalid topic format");
  }
  std::string_view part1 = *it++;

  // Sparkplug B topic: spBv1.0/{group_id}/{message_type}/{edge_node_id}[/{device_id}]
  // or STATE message: spBv1.0/STATE/{host_id}
  if (part0 != NAMESPACE) {
    return stdx::unexpected("Invalid Sparkplug B topic");
  }

  // Check for STATE message: spBv1.0/STATE/{host_id}
  if (part1 == "STATE") {
    if (it == end) {
      return stdx::unexpected("STATE topic requires host_id");
    }
    std::string_view host_id = *it++;
    if (it != end) {
      return stdx::unexpected("Invalid STATE topic: trailing components");
    }
    if (!valid_component(host_id)) {
      return stdx::unexpected("STATE topic has empty or oversized host_id");
    }
    return Topic{.group_id = "",
                 .message_type = MessageType::STATE,
                 .edge_node_id = std::string(host_id),
                 .device_id = ""};
  }

  if (it == end) {
    return stdx::unexpected("Invalid Sparkplug B topic");
  }
  std::string_view part2 = *it++;

  if (it == end) {
    return stdx::unexpected("Invalid Sparkplug B topic");
  }
  std::string_view part3 = *it++;

  auto msg_type = parse_message_type(part2);
  if (!msg_type) {
    return stdx::unexpected(msg_type.error());
  }

  // Reject empty/oversized ids: these become permanent state keys and log content.
  if (!valid_component(part1) || !valid_component(part3)) {
    return stdx::unexpected("Topic has empty or oversized group_id/edge_node_id");
  }

  const bool device_scoped = *msg_type == MessageType::DBIRTH ||
                             *msg_type == MessageType::DDATA ||
                             *msg_type == MessageType::DDEATH ||
                             *msg_type == MessageType::DCMD;
  std::string device_id;
  if (it != end) {
    // A 5th component is only valid for device messages; reject it otherwise.
    if (!device_scoped) {
      return stdx::unexpected("Node-level topic must not have a device_id component");
    }
    device_id = std::string(*it++);
    if (!valid_component(device_id)) {
      return stdx::unexpected("Topic has empty or oversized device_id");
    }
  }
  if (it != end) {
    return stdx::unexpected("Invalid Sparkplug B topic: trailing components");
  }
  if (device_scoped && device_id.empty()) {
    // Device messages must carry a device id; an empty one would corrupt state keys.
    return stdx::unexpected("Device-level topic requires a device_id");
  }

  return Topic{.group_id = std::string(part1),
               .message_type = *msg_type,
               .edge_node_id = std::string(part3),
               .device_id = std::move(device_id)};
}

} // namespace sparkplug
