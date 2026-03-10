// src/payload_builder.cpp
#include "sparkplug/payload_builder.hpp"

#include <chrono>

namespace sparkplug {

PayloadBuilder::PayloadBuilder() {
  auto now = std::chrono::system_clock::now();
  auto timestamp =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch())
          .count();
  payload_.set_timestamp(timestamp);
}

std::vector<uint8_t> PayloadBuilder::build() const {
  std::vector<uint8_t> buffer(payload_.ByteSizeLong());
  (void)payload_.SerializeToArray(buffer.data(), static_cast<int>(buffer.size()));
  return buffer;
}

const org::eclipse::tahu::protobuf::Payload& PayloadBuilder::payload() const noexcept {
  return payload_;
}

} // namespace sparkplug