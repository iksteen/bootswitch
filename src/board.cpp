#include "board.h"
#include "board_power.h"
#include <Arduino.h>

#if BOOTSWITCH_BOARD_TQT
#include <esp32-hal-tinyusb.h>
#include <lgfx/v1/panel/Panel_GC9A01.hpp>
#include <lgfx/v1/platforms/esp32/Bus_SPI.hpp>
#include <lgfx/v1/platforms/esp32/Light_PWM.hpp>
#else
#include <M5Unified.h>
#endif

namespace bootswitch {
namespace {
int charging = -1;
bool powerSleep = false;
#if BOOTSWITCH_BOARD_TQT
// LILYGO T-QT GC9107 (original panel), manufacturer's register sequence:
// https://github.com/Xinyuan-LilyGO/T-QT/tree/main/extras/old_panel
class TqtPanel : public lgfx::Panel_GC9107 {
 protected:
  const uint8_t* getInitCommands(uint8_t list) const override {
    static constexpr uint8_t commands[] = {
      0xFE,0, 0xEF,0, 0xB0,1,0xC0, 0xB1,1,0x80, 0xB2,1,0x27,
      0xB3,1,0x13, 0xB6,1,0x19, 0xB7,1,0x05, 0xAC,1,0xC8,
      0xAB,1,0x0F, 0x3A,1,0x05, 0xB4,1,0x04, 0xA8,1,0x08,
      0xB8,1,0x08, 0xEA,1,0x02, 0xE8,1,0x2A, 0xE9,1,0x47,
      0xE7,1,0x5F, 0xC6,1,0x21, 0xC7,1,0x15,
      0xF0,14,0x1D,0x38,0x09,0x4D,0x92,0x2F,0x35,0x52,0x1E,0x0C,0x04,0x12,0x14,0x1F,
      0xF1,14,0x16,0x40,0x1C,0x54,0xA9,0x2D,0x2E,0x56,0x10,0x0D,0x0C,0x1A,0x14,0x1E,
      0xF4,3,0x00,0x00,0xFF, 0xBA,2,0xFF,0xFF,
      0x20,0, 0x11,CMD_INIT_DELAY,120, 0x29,0, 0xFF,0xFF
    };
    return list == 0 ? commands : nullptr;
  }
};
class TqtDisplay : public lgfx::LGFX_Device {
  lgfx::Bus_SPI bus_;
  TqtPanel panel_;
  lgfx::Light_PWM light_;
 public:
  TqtDisplay() {
    auto bus = bus_.config();
    bus.spi_host = SPI2_HOST;
    bus.spi_mode = 0;
    bus.freq_write = 40000000;
    bus.pin_sclk = 3;
    bus.pin_mosi = 2;
    bus.pin_miso = -1;
    bus.pin_dc = 6;
    bus.dma_channel = SPI_DMA_CH_AUTO;
    bus_.config(bus);
    panel_.setBus(&bus_);
    auto panel = panel_.config();
    panel.pin_cs = 5;
    panel.pin_rst = 1;
    // This 128x128 GC9107 panel starts at RAM (0,0). The GC9A01
    // example's (2,1) offset leaves the leading rows/columns uncleared
    // and sends the right edge beyond the controller's column range.
    panel.memory_width = 128;
    panel.memory_height = 128;
    panel.panel_width = panel.panel_height = 128;
    panel.offset_x = 0;
    panel.offset_y = 0;
    panel.rgb_order = true;
    panel.invert = false;
    panel.readable = false;
    panel_.config(panel);
    auto light = light_.config();
    light.pin_bl = 10;
    light.invert = true;
    light.freq = 12000;
    light.pwm_channel = 7;
    light_.config(light);
    panel_.setLight(&light_);
    setPanel(&panel_);
  }
};
TqtDisplay display;
m5::Button_Class buttonA, buttonB;
#endif
}

lgfx::LGFX_Device& boardDisplay() {
#if BOOTSWITCH_BOARD_TQT
  return display;
#else
  return M5.Display;
#endif
}
m5::Button_Class& boardButtonA() {
#if BOOTSWITCH_BOARD_TQT
  return buttonA;
#else
  return M5.BtnA;
#endif
}
m5::Button_Class& boardButtonB() {
#if BOOTSWITCH_BOARD_TQT
  return buttonB;
#else
  return M5.BtnB;
#endif
}
void beginBoard() {
#if BOOTSWITCH_BOARD_TQT
  pinMode(0, INPUT_PULLUP);
  pinMode(47, INPUT_PULLUP);
  display.init();
  display.setRotation(0);
#else
  auto config = M5.config();
  config.internal_imu = false;
  config.internal_rtc = false;
  config.internal_spk = false;
  config.internal_mic = false;
  config.output_power = false;
  config.pmic_button = false;
  preparePowerController();
  M5.begin(config);
  M5.Power.M5pm1.setLedEnLevel(false);
  constexpr bool wantCharging = !BOOTSWITCH_DISABLE_BATTERY_CHARGING;
  bool actual = false;
  if (M5.Power.M5pm1.isEnabled() && M5.Power.M5pm1.setBatteryCharge(wantCharging) &&
      M5.Power.M5pm1.getBatteryCharge(&actual) && actual == wantCharging)
    charging = actual ? 1 : 0;
  powerSleep = enablePowerControllerSleep();
  M5.Display.setRotation(1);
#endif
  boardButtonB().setHoldThresh(1500);
  boardDisplay().setBrightness(60);
}
void updateBoard() {
#if BOOTSWITCH_BOARD_TQT
  const auto now = millis();
  buttonA.setRawState(now, digitalRead(0) == LOW);
  buttonB.setRawState(now, digitalRead(47) == LOW);
#else
  M5.update();
#endif
}
bool boardHasBatteryTelemetry() { return !BOOTSWITCH_BOARD_TQT; }
int boardBatteryLevel() {
#if BOOTSWITCH_BOARD_TQT
  return -1;  // T-QT has no battery; GPIO4 is not a battery ADC input.
#else
  return preparePowerAccess() ? M5.Power.getBatteryLevel() : -1;
#endif
}
int boardChargingEnabled() { return charging; }
bool boardPowerSleep() { return powerSleep; }
const char* boardUsbName() {
#if BOOTSWITCH_BOARD_TQT
  return "BootSwitch T-QT";
#else
  return "BootSwitch StickS3";
#endif
}
#if BOOTSWITCH_ENABLE_REMOTE_FLASH
bool boardCanEnterDownload() {
#if BOOTSWITCH_BOARD_TQT
  return true;
#else
  return M5.Power.M5pm1.isEnabled();
#endif
}
bool boardEnterDownload() {
#if BOOTSWITCH_BOARD_TQT
  usb_persist_restart(RESTART_BOOTLOADER);
  return false;
#else
  return preparePowerAccess() && M5.Power.M5pm1.writeRegister8(0x0C, 0xA3);
#endif
}
#endif
}
