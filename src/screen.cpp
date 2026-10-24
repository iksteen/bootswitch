#include "screen.h"
#include "screen_layout.h"

#include <M5GFX.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace bootswitch {
namespace {
constexpr size_t kCharacterWidth = 6;
constexpr int32_t kLineHeight = 8;
}

bool ScreenRenderer::begin(lgfx::LGFX_Device& display) {
  display_ = &display;
  layout_ = screenLayoutFor(display.width(), display.height());
  if (!layout_) return false;
  display.setFont(&lgfx::fonts::Font0);
  display.setTextPadding(0);
  return true;
}

void ScreenRenderer::text(const char* value, int32_t x, int32_t y,
                          uint8_t scale, uint16_t color) {
  display_->setTextSize(scale);
  display_->setTextColor(color, TFT_BLACK);
  display_->drawString(value, x, y);
}

void ScreenRenderer::draw(const ScreenState& state) {
  if (!layout_) return;
  const auto& p = *layout_;
  display_->fillScreen(TFT_BLACK);
  text("NEXT BOOT", p.heading.x, p.heading.y, 1, TFT_LIGHTGREY);
  const bool windows = state.selection == Mode::Windows;
  text(windows ? "Windows" : "Linux", p.selection.x, p.selection.y,
       p.selectionScale, windows ? TFT_CYAN : TFT_GREEN);
  drawStatus(state);
  text(!state.usbReady ? "USB init failed" : state.configDisk ?
       p.configDiskLabel : "USB: boot selector", p.usb.x, p.usb.y);

  char network[32];
  const char* networkText = "Wi-Fi: off";
  if (state.network == ScreenNetworkState::Connecting) networkText = "Wi-Fi: connecting";
  else if (state.network == ScreenNetworkState::Connected) {
    std::snprintf(network, sizeof(network), "%.*s%s", int(sizeof(state.ipAddress)),
                  state.ipAddress, state.apiEnabled ? "" : p.apiOffSuffix);
    networkText = network;
  }
  text(networkText, p.network.x, p.network.y);
  text(state.bleEnabled ? p.bluetoothLabel : "BLE: off", p.bluetooth.x, p.bluetooth.y);
  drawTelemetry(state.telemetry);
}

void ScreenRenderer::drawStatus(const ScreenState& state) {
  if (!layout_) return;
  const auto& p = *layout_;
  const auto& area = p.status;
  display_->fillRect(area.x, area.y, area.width, area.height, TFT_BLACK);
  char progress[48];
  const char* message = state.status ? state.status : p.buttonHint;
  if (state.firmwarePercent >= 0) {
    const unsigned percent = std::min(state.firmwarePercent, 100);
    std::snprintf(progress, sizeof(progress), p.progressFormat, percent);
    message = progress;
  }
  char line[64];
  const size_t columns = std::min(size_t(area.width) / kCharacterWidth, sizeof(line) - 1);
  size_t remaining = std::strlen(message);
  for (unsigned row = 0; row < p.statusLines; ++row) {
    const size_t count = std::min(columns, remaining);
    std::memcpy(line, message, count);
    line[count] = 0;
    if (row + 1 == p.statusLines && remaining > columns && columns >= 3)
      std::memcpy(line + columns - 3, "...", 3);
    text(line, area.x, area.y + row * kLineHeight);
    message += count;
    remaining -= count;
  }
}

void ScreenRenderer::drawTelemetry(const DisplayTelemetryValues& values) {
  if (!layout_) return;
  const auto& p = *layout_;
  const auto& area = p.telemetry;
  char wifi[20];
  if (!values.wifiEnabled) std::snprintf(wifi, sizeof(wifi), "WiFi:off");
  else if (!values.wifiConnected || values.rssi >= 0) std::snprintf(wifi, sizeof(wifi), "WiFi:--");
  else std::snprintf(wifi, sizeof(wifi), p.rssiFormat, static_cast<long>(values.rssi));
  char battery[16];
  if (values.batteryLevel < 0 || values.batteryLevel > 100) std::snprintf(battery, sizeof(battery), "--");
  else std::snprintf(battery, sizeof(battery), "%ld%%", static_cast<long>(values.batteryLevel));
  char value[40];
  if (values.batterySupported)
    std::snprintf(value, sizeof(value), p.telemetryFormat, wifi, battery);
  else std::snprintf(value, sizeof(value), "%s", wifi);
  display_->fillRect(area.x, area.y, area.width, area.height, TFT_BLACK);
  display_->setTextSize(1);
  display_->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  display_->drawRightString(value, p.telemetryAnchor.x, p.telemetryAnchor.y);
}
}
