#include "ble_control.h"

#include <cstring>

namespace bootswitch {
BleCommand parseBleCommand(const uint8_t* bytes, size_t length) {
  if (!bytes) return BleCommand::None;
  if (length == 4 && std::memcmp(bytes, "wake", 4) == 0) return BleCommand::Wake;
  if (length == 5 && std::memcmp(bytes, "linux", 5) == 0) return BleCommand::Linux;
  if (length == 7 && std::memcmp(bytes, "windows", 7) == 0) return BleCommand::Windows;
  if (length == 6 && std::memcmp(bytes, "toggle", 6) == 0) return BleCommand::Toggle;
  return BleCommand::None;
}
}  // namespace bootswitch
