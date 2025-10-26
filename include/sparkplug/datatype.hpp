// include/sparkplug/datatype.hpp
#pragma once

#include <cstdint>

namespace sparkplug {

enum class DataType : uint32_t {
  Unknown = 0,
  Int8 = 1,
  Int16 = 2,
  Int32 = 3,
  Int64 = 4,
  UInt8 = 5,
  UInt16 = 6,
  UInt32 = 7,
  UInt64 = 8,
  Float = 9,
  Double = 10,
  Boolean = 11,
  String = 12,
  DateTime = 13,
  Text = 14,
  UUID = 15,
  DataSet = 16,
  Bytes = 17,
  File = 18,
  Template = 19,
  PropertySet = 20,
  PropertySetList = 21
};

}