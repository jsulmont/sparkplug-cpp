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
