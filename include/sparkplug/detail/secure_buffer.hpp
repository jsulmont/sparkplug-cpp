// include/sparkplug/detail/secure_buffer.hpp
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sparkplug::detail {

// NUL-terminated buffer for secrets that zeroes its memory on clear, move,
// and destruction — std::string never scrubs freed memory (CWE-316).
// The volatile writes cannot be removed by dead-store elimination.
class SecureBuffer {
public:
  SecureBuffer() = default;
  explicit SecureBuffer(std::string_view value) {
    assign(value);
  }

  SecureBuffer(const SecureBuffer&) = delete;
  SecureBuffer& operator=(const SecureBuffer&) = delete;

  SecureBuffer(SecureBuffer&& other) noexcept : data_(std::move(other.data_)) {
    other.scrub();
    other.data_.clear();
  }

  SecureBuffer& operator=(SecureBuffer&& other) noexcept {
    if (this != &other) {
      clear();
      data_ = std::move(other.data_);
      other.scrub();
      other.data_.clear();
    }
    return *this;
  }

  ~SecureBuffer() {
    scrub();
  }

  void assign(std::string_view value) {
    clear();
    if (!value.empty()) {
      data_.reserve(value.size() + 1);
      data_.assign(value.begin(), value.end());
      data_.push_back('\0');
    }
  }

  void clear() {
    scrub();
    data_.clear();
  }

  // Stable pointer valid until the next assign/clear/destruction; nullptr when empty.
  [[nodiscard]] const char* c_str() const noexcept {
    return data_.empty() ? nullptr : data_.data();
  }

  [[nodiscard]] bool empty() const noexcept {
    return data_.empty();
  }

  // Zeroes a std::string's storage in place; call before its buffer is freed.
  static void scrub_string(std::string& s) {
    if (!s.empty()) {
      volatile char* p = s.data();
      for (size_t i = 0; i < s.size(); ++i) {
        p[i] = 0;
      }
    }
    s.clear();
  }

private:
  void scrub() {
    if (!data_.empty()) {
      volatile char* p = data_.data();
      for (size_t i = 0; i < data_.size(); ++i) {
        p[i] = 0;
      }
    }
  }

  std::vector<char> data_;
};

} // namespace sparkplug::detail
