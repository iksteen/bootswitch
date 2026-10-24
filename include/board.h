#pragma once

#include <M5GFX.h>
#include <utility/Button_Class.hpp>
#include "build_features.h"

namespace bootswitch {
lgfx::LGFX_Device& boardDisplay();
m5::Button_Class& boardButtonA();
m5::Button_Class& boardButtonB();
void beginBoard();
void updateBoard();
bool boardHasBatteryTelemetry();
int boardBatteryLevel();
int boardChargingEnabled();
bool boardPowerSleep();
const char* boardUsbName();
#if BOOTSWITCH_ENABLE_REMOTE_FLASH
bool boardCanEnterDownload();
bool boardEnterDownload();
#endif
}
