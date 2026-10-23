#include "ble_control.h"

#include <cassert>
#include <cstring>
#include <iostream>

using namespace bootswitch;

static BleCommand command(const char* value) {
  return parseBleCommand(reinterpret_cast<const uint8_t*>(value), std::strlen(value));
}

int main() {
  assert(command("toggle") == BleCommand::Toggle);
  assert(command("linux") == BleCommand::Linux);
  assert(command("windows") == BleCommand::Windows);
  assert(command("wake") == BleCommand::Wake);
  for (const char* invalid : {"", "Toggle", "toggle\n", "linux ", " windows", "1", "togglejunk",
                              "Wake", "wake\n", "wake "})
    assert(command(invalid) == BleCommand::None);
  assert(parseBleCommand(nullptr, 7) == BleCommand::None);
  const uint8_t embeddedZero[] = {'l', 'i', 'n', 'u', 'x', 0, 'x'};
  assert(parseBleCommand(embeddedZero, sizeof(embeddedZero)) == BleCommand::None);
  // BLE values are length-delimited; no trailing NUL or out-of-bounds read.
  const uint8_t raw[] = {'w', 'i', 'n', 'd', 'o', 'w', 's'};
  assert(parseBleCommand(raw, sizeof(raw)) == BleCommand::Windows);
  for (size_t length = 0; length < sizeof(raw); ++length)
    assert(parseBleCommand(raw, length) == BleCommand::None);
  std::cout << "BLE commands accept exact values and reject malformed payloads.\n";
}
