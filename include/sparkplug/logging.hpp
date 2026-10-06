#pragma once

#include <format>
#include <functional>
#include <string>
#include <string_view>

namespace sparkplug {

enum class LogLevel { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3 };

using LogCallback = std::function<void(LogLevel, std::string_view)>;

// Escapes control characters so attacker-controlled topic/metric strings
// cannot forge log entries or inject terminal escape sequences (CWE-117).
inline std::string sanitize_log_message(std::string_view message) {
  std::string sanitized;
  sanitized.reserve(message.size());
  for (char c : message) {
    const auto ch = static_cast<unsigned char>(c);
    if (ch < 0x20 || ch == 0x7f) {
      sanitized += std::format("\\x{:02x}", ch);
    } else {
      sanitized += c;
    }
  }
  return sanitized;
}

} // namespace sparkplug
