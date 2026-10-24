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
#if BOOTSWITCH_ENABLE_USB_UPDATE
  assert(command("update") == BleCommand::Update);
#else
  assert(command("update") == BleCommand::None);
#endif
#if BOOTSWITCH_ENABLE_REMOTE_FLASH
  assert(command("flash") == BleCommand::Flash);
#else
  assert(command("flash") == BleCommand::None);
#endif
  for (const char* invalid : {"", "Toggle", "toggle\n", "linux ", " windows", "1", "togglejunk",
                              "Wake", "wake\n", "wake ", "Flash", "flash\n", "flash ", "flashjunk",
                              "Update", "update\n", "update ", "updatejunk"})
    assert(command(invalid) == BleCommand::None);
  assert(parseBleCommand(nullptr, 7) == BleCommand::None);
  const uint8_t embeddedZero[] = {'l', 'i', 'n', 'u', 'x', 0, 'x'};
  assert(parseBleCommand(embeddedZero, sizeof(embeddedZero)) == BleCommand::None);
  const uint8_t flashZero[] = {'f', 'l', 'a', 's', 'h', 0};
  assert(parseBleCommand(flashZero, sizeof(flashZero)) == BleCommand::None);
#if BOOTSWITCH_ENABLE_REMOTE_FLASH
  assert(parseBleCommand(flashZero, 5) == BleCommand::Flash);
#else
  assert(parseBleCommand(flashZero, 5) == BleCommand::None);
#endif
  // BLE values are length-delimited; no trailing NUL or out-of-bounds read.
  const uint8_t raw[] = {'w', 'i', 'n', 'd', 'o', 'w', 's'};
  assert(parseBleCommand(raw, sizeof(raw)) == BleCommand::Windows);
  for (size_t length = 0; length < sizeof(raw); ++length)
    assert(parseBleCommand(raw, length) == BleCommand::None);
  std::cout << "BLE commands: remote flash " << BOOTSWITCH_ENABLE_REMOTE_FLASH
            << "; exact values and malformed payload checks passed.\n";
}
