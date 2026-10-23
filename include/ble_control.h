#pragma once

#include "boot_disk.h"
#include "settings.h"
#include "build_features.h"

namespace bootswitch {
enum class BleCommand : uint8_t { None, Linux, Windows, Toggle, Wake
#if BOOTSWITCH_ENABLE_REMOTE_FLASH
  , Flash
#endif
};
BleCommand parseBleCommand(const uint8_t* bytes, size_t length);

// BLE callbacks only enqueue commands. The application processes them in the
// main loop; the state characteristic reports saved OS selections only.
const char* startBle(const Settings& settings, Mode mode, bool forgetBonds);
void stopBle();
bool bleActive();
BleCommand nextBleCommand();
void publishBleMode(Mode mode);
}  // namespace bootswitch
