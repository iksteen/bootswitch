#pragma once

#include <cstdint>

namespace bootswitch {
struct ScreenPoint { int32_t x, y; };
struct ScreenRect { int32_t x, y, width, height; };

// Font0 is a fixed 6x8 font. Positions, reserved update regions and wording
// belong to the display profile, independently of the board driving it.
struct ScreenLayout {
  int32_t width, height;
  ScreenPoint heading, selection;
  uint8_t selectionScale;
  ScreenRect status;
  uint8_t statusLines;
  ScreenPoint usb, network, bluetooth;
  ScreenRect telemetry;
  ScreenPoint telemetryAnchor;
  const char* buttonHint;
  const char* configDiskLabel;
  const char* bluetoothLabel;
  const char* apiOffSuffix;
  const char* progressFormat;
  const char* rssiFormat;
  const char* telemetryFormat;
};

// Unsupported dimensions require an explicit profile; there is no implicit
// width threshold whose layout could extend beyond a new display's bounds.
const ScreenLayout* screenLayoutFor(int32_t width, int32_t height);
}
