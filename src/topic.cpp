// src/topic.cpp
#include "sparkplug/topic.hpp"

#include "sparkplug/detail/compat.hpp"

#include <algorithm>
#include <string>
#include <vector>

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
  stdx::unreachable();
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
  return stdx::unexpected(stdx::format("Unknown message type: {}", str));
}

// Simple string_view split without C++20 ranges
std::vector<std::string_view> split_string_view(std::string_view str, char delim) {
  std::vector<std::string_view> parts;
  size_t start = 0;
  while (start < str.size()) {
    size_t end = str.find(delim, start);
    if (end == std::string_view::npos) {
      parts.push_back(str.substr(start));
      break;
    }
    parts.push_back(str.substr(start, end - start));
    start = end + 1;
  }
  return parts;
}

} // namespace

std::string Topic::to_string() const {
  if (message_type == MessageType::STATE) {
    return stdx::format("{}/STATE/{}", NAMESPACE, edge_node_id);
  }

  auto base = stdx::format("{}/{}/{}/{}", NAMESPACE, group_id,
                           message_type_to_string(message_type), edge_node_id);

  if (!device_id.empty()) {
    return stdx::format("{}/{}", base, device_id);
  }
  return base;
}

stdx::expected<Topic, std::string> Topic::parse(std::string_view topic_str) {
  auto parts = split_string_view(topic_str, '/');

  if (parts.size() < 2) {
    return stdx::unexpected("Invalid topic format");
  }

  std::string_view part0 = parts[0];
  std::string_view part1 = parts[1];

  // Sparkplug B topic: spBv1.0/{group_id}/{message_type}/{edge_node_id}[/{device_id}]
  // or STATE message: spBv1.0/STATE/{host_id}
  if (part0 != NAMESPACE) {
    return stdx::unexpected("Invalid Sparkplug B topic");
  }

  // Check for STATE message: spBv1.0/STATE/{host_id}
  if (part1 == "STATE") {
    if (parts.size() < 3) {
      return stdx::unexpected("STATE topic requires host_id");
    }
    std::string_view host_id = parts[2];
    return Topic{.group_id = "",
                 .message_type = MessageType::STATE,
                 .edge_node_id = std::string(host_id),
                 .device_id = ""};
  }

  if (parts.size() < 4) {
    return stdx::unexpected("Invalid Sparkplug B topic");
  }

  std::string_view part2 = parts[2];
  std::string_view part3 = parts[3];

  auto msg_type = parse_message_type(part2);
  if (!msg_type) {
    return stdx::unexpected(msg_type.error());
  }

  std::string device_id;
  if (parts.size() > 4) {
    device_id = std::string(parts[4]);
  }

  return Topic{.group_id = std::string(part1),
               .message_type = *msg_type,
               .edge_node_id = std::string(part3),
               .device_id = std::move(device_id)};
}

} // namespace sparkplug
