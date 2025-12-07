#pragma once

#include <functional>
#include <string_view>

namespace sparkplug {

enum class LogLevel { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3 };

using LogCallback = std::function<void(LogLevel, std::string_view)>;

} // namespace sparkplug
