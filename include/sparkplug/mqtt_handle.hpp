// include/sparkplug/mqtt_handle.hpp
#pragma once

typedef void* MQTTAsync;

namespace sparkplug {

// RAII wrapper for MQTTAsync client handle
class MQTTAsyncHandle {
public:
  MQTTAsyncHandle() noexcept : client_(nullptr) {
  }
  explicit MQTTAsyncHandle(MQTTAsync client) noexcept : client_(client) {
  }

  ~MQTTAsyncHandle() noexcept;

  // Non-copyable
  MQTTAsyncHandle(const MQTTAsyncHandle&) = delete;
  MQTTAsyncHandle& operator=(const MQTTAsyncHandle&) = delete;

  // Movable
  MQTTAsyncHandle(MQTTAsyncHandle&& other) noexcept : client_(other.client_) {
    other.client_ = nullptr;
  }

  MQTTAsyncHandle& operator=(MQTTAsyncHandle&& other) noexcept {
    if (this != &other) {
      reset();
      client_ = other.client_;
      other.client_ = nullptr;
    }
    return *this;
  }

  // Access
  MQTTAsync get() const noexcept {
    return client_;
  }
  MQTTAsync operator*() const noexcept {
    return client_;
  }
  explicit operator bool() const noexcept {
    return client_ != nullptr;
  }

  // Modifiers
  void reset() noexcept;
  MQTTAsync release() noexcept {
    MQTTAsync tmp = client_;
    client_ = nullptr;
    return tmp;
  }

private:
  MQTTAsync client_;
};

} // namespace sparkplug
