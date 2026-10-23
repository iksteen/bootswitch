#pragma once

namespace bootswitch {
// Before M5.begin(): wake a PM1 retained across ESP resets for board detection.
void preparePowerController();
// After M5.begin(): allow the PM1 itself to sleep between I2C accesses.
bool enablePowerControllerSleep();
// Before battery reads or PM1 commands: its first transaction may only wake it.
bool preparePowerAccess();
}
