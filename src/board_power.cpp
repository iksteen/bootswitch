#include "board_power.h"

#include <M5Unified.h>

namespace bootswitch {
namespace {
constexpr uint8_t kAddress = 0x6E;
constexpr uint8_t kI2cConfig = 0x09;
constexpr uint32_t kFrequency = 100000;
}

void preparePowerController() {
  // StickS3 internal I2C. PM1 remains powered through ESP.restart()/watchdog
  // resets, and M5GFX's board probe does not retry a sleeping PM1's first NACK.
  if (!M5.In_I2C.begin(I2C_NUM_1, 47, 48)) return;
  M5.In_I2C.start(kAddress, false, kFrequency);
  M5.In_I2C.stop();
  delay(10);
  M5.In_I2C.writeRegister8(kAddress, kI2cConfig, 0, kFrequency);
  M5.In_I2C.release();
}

bool preparePowerAccess() {
  if (!M5.Power.M5pm1.isEnabled()) return false;
  uint8_t deviceId = 0;
  if (M5.Power.M5pm1.readRegister(0x00, &deviceId, 1)) return true;
  // Datasheet: the first transaction wakes the PM1; retry within its 300 ms
  // wake window. The PM1 library uses a 10 ms delay after the wake signal.
  delay(10);
  return M5.Power.M5pm1.readRegister(0x00, &deviceId, 1);
}

bool enablePowerControllerSleep() {
  // I2C idle sleep preserves the supply outputs; SYS_CMD shutdown does not.
  // While the screen is awake, its 1 Hz battery reads keep the PM1 awake.
  return preparePowerAccess() && M5.Power.M5pm1.writeRegister8(kI2cConfig, 5);
}
}
