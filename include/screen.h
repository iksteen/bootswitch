#pragma once

#include "boot_disk.h"
#include "display_telemetry.h"

namespace lgfx { inline namespace v1 { class LGFX_Device; } }

namespace bootswitch {
struct ScreenLayout;
enum class ScreenNetworkState { Disabled, Connecting, Connected };

// A value snapshot: rendering never reads radios, storage, buttons or clocks.
// status is borrowed for the duration of the synchronous draw call.
struct ScreenState {
  Mode selection = Mode::Linux;
  const char* status = nullptr;
  int firmwarePercent = -1;
  bool usbReady = false;
  bool configDisk = false;
  ScreenNetworkState network = ScreenNetworkState::Disabled;
  char ipAddress[16] = {};
  bool apiEnabled = false;
  bool bleEnabled = false;
  DisplayTelemetryValues telemetry;
};

class ScreenRenderer {
 public:
  // Bind after the hardware driver initializes its dimensions. Select once.
  bool begin(lgfx::LGFX_Device& display);
  void draw(const ScreenState& state);
  void drawStatus(const ScreenState& state);
  void drawTelemetry(const DisplayTelemetryValues& values);

 private:
  void text(const char* value, int32_t x, int32_t y, uint8_t scale = 1,
            uint16_t color = 0xFFFF);
  lgfx::LGFX_Device* display_ = nullptr;
  const ScreenLayout* layout_ = nullptr;
};
}
