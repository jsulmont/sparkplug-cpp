#pragma once

// Compatibility layer for C++23 features
// Provides fallbacks for older compilers that lack full C++23 stdlib support

// std::expected compatibility
// Feature test macro from: https://en.cppreference.com/w/cpp/feature_test
#if __cpp_lib_expected >= 202202L
// Compiler has C++23 std::expected in stdlib
#  include <expected>
namespace sparkplug::stdx {
using std::expected;
using std::unexpected;
} // namespace sparkplug::stdx
#else
// Fallback to tl::expected for older compilers (e.g., Ubuntu 24.04 LTS)
#  include <tl/expected.hpp>
namespace sparkplug::stdx {
using tl::expected;
using tl::unexpected;
} // namespace sparkplug::stdx
#endif

// std::format compatibility
// Use fmt library on platforms where std::format is unavailable or broken
// (e.g., macOS < 13.3 lacks to_chars for floating point required by std::format)
#if defined(SPARKPLUG_USE_FMT) ||                                                        \
    (defined(__APPLE__) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 130300)
#  include <fmt/format.h>
namespace sparkplug::stdx {
using fmt::format;
} // namespace sparkplug::stdx
#else
#  include <format>
namespace sparkplug::stdx {
using std::format;
} // namespace sparkplug::stdx
#endif

// std::unreachable compatibility
#if __cpp_lib_unreachable >= 202202L
#  include <utility>
namespace sparkplug::stdx {
using std::unreachable;
} // namespace sparkplug::stdx
#else
namespace sparkplug::stdx {
[[noreturn]] inline void unreachable() {
#  if defined(__GNUC__) || defined(__clang__)
  __builtin_unreachable();
#  elif defined(_MSC_VER)
  __assume(false);
#  endif
}
} // namespace sparkplug::stdx
#endif
