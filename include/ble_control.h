#pragma once

#include "boot_disk.h"
#include "settings.h"

namespace bootswitch {
enum class BleCommand : uint8_t { None, Linux, Windows, Toggle };
BleCommand parseBleCommand(const uint8_t* bytes, size_t length);

// BLE callbacks only enqueue commands. The application applies them through
// its normal persistence path; the state characteristic reports saved modes.
const char* startBle(const Settings& settings, Mode mode, bool forgetBonds);
void stopBle();
bool bleActive();
BleCommand nextBleCommand();
void publishBleMode(Mode mode);
}  // namespace bootswitch
