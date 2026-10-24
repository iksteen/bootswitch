#include "screen.h"
#include "screen_layout.h"
#include <M5GFX.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

using namespace bootswitch;
using Display = lgfx::LGFX_Device;

const Display::Operation& findText(const Display& display, const char* value) {
  for (const auto& op : display.operations)
    if (op.isText && op.text == value) return op;
  assert(false && "Expected visible text missing");
  return display.operations.front();
}
void within(const Display& display, const ScreenRect& area) {
  assert(!display.operations.empty());
  for (const auto& op : display.operations) {
    assert(op.x >= area.x && op.y >= area.y);
    assert(op.x + op.width <= area.x + area.width);
    assert(op.y + op.height <= area.y + area.height);
  }
}
void checkLayout(int width, int height, unsigned selectionScale,
                 const char* telemetry, const char* progress) {
  const auto* layout = screenLayoutFor(width, height);
  assert(layout);
  Display display(width, height);
  ScreenRenderer screen;
  assert(screen.begin(display));
  const auto dimensionReads = display.dimensionReads;
  ScreenState state;
  state.usbReady = state.configDisk = state.apiEnabled = state.bleEnabled = true;
  state.network = ScreenNetworkState::Connected;
  std::strcpy(state.ipAddress, "192.168.0.106");
  state.telemetry.wifiEnabled = state.telemetry.wifiConnected = true;
  state.telemetry.rssi = -127;
  state.telemetry.batteryLevel = 100;
  screen.draw(state);
  assert(display.fullClears == 1);
  const auto& selection = findText(display, "Linux");
  assert(selection.x == 8 && selection.y == 26 && selection.scale == selectionScale);
  assert(selection.color == 0x00FF00);
  findText(display, "NEXT BOOT");
  findText(display, "192.168.0.106");
  findText(display, layout->buttonHint);
  findText(display, layout->configDiskLabel);
  findText(display, layout->bluetoothLabel);
  findText(display, telemetry);

  display.operations.clear();
  state.selection = Mode::Windows;
  state.usbReady = false;
  state.network = ScreenNetworkState::Connecting;
  state.bleEnabled = false;
  screen.draw(state);
  assert(findText(display, "Windows").color == 0x00FFFF);
  findText(display, "USB init failed");
  findText(display, "Wi-Fi: connecting");
  findText(display, "BLE: off");
  display.operations.clear();
  state.usbReady = true; state.configDisk = false;
  state.network = ScreenNetworkState::Disabled;
  screen.draw(state);
  findText(display, "USB: boot selector");
  findText(display, "Wi-Fi: off");
  display.operations.clear();
  state.network = ScreenNetworkState::Connected; state.apiEnabled = false;
  screen.draw(state);
  const std::string address = std::string(state.ipAddress) + layout->apiOffSuffix;
  findText(display, address.c_str());

  // Full-length errors stay in their reserved region and cannot overwrite USB
  // or telemetry. Replacing one with a short message clears the old contents.
  display.operations.clear();
  const auto fullClears = display.fullClears;
  state.status = "An unusually long error message that must never overwrite any other screen field";
  screen.drawStatus(state);
  within(display, layout->status);
  const auto& last = display.operations.back();
  assert(last.text.size() >= 3 && last.text.substr(last.text.size() - 3) == "...");
  display.operations.clear();
  state.status = "OK";
  screen.drawStatus(state);
  within(display, layout->status);
  for (int y = layout->status.y; y < layout->status.y + layout->status.height; ++y)
    for (int x = layout->status.x; x < layout->status.x + layout->status.width; ++x)
      if (x >= layout->status.x + 12 || y >= layout->status.y + 8)
        assert(display.pixels[y * width + x] == 0);

  display.operations.clear();
  state.firmwarePercent = 42;
  screen.drawStatus(state);
  findText(display, progress);
  within(display, layout->status);
  assert(display.fullClears == fullClears);  // No progress-induced flicker.

  display.operations.clear();
  state.telemetry.rssi = -32; state.telemetry.batteryLevel = 9;
  screen.drawTelemetry(state.telemetry);
  within(display, layout->telemetry);
  assert(display.fullClears == fullClears);
  display.operations.clear();
  state.telemetry.wifiEnabled = false; state.telemetry.batteryLevel = -1;
  screen.drawTelemetry(state.telemetry);
  const std::string unavailable = width == 128 ? "WiFi:off --" : "WiFi:off Bat:--";
  findText(display, unavailable.c_str());
  within(display, layout->telemetry);
  display.operations.clear();
  state.telemetry.wifiEnabled = true; state.telemetry.wifiConnected = false;
  screen.drawTelemetry(state.telemetry);
  findText(display, width == 128 ? "WiFi:-- --" : "WiFi:-- Bat:--");
  within(display, layout->telemetry);
  // Battery support is a hardware capability, independent of screen shape.
  // Unsupported batteries disappear; unavailable readings on supported boards
  // retain their placeholder, and shortening the field clears old pixels.
  display.operations.clear();
  state.telemetry.wifiConnected = true; state.telemetry.rssi = -32;
  state.telemetry.batterySupported = false;
  screen.drawTelemetry(state.telemetry);
  findText(display, width == 128 ? "-32dBm" : "WiFi:-32dBm");
  within(display, layout->telemetry);
  assert(display.dimensionReads == dimensionReads);  // Profile selected once.
}

int main() {
  checkLayout(128, 128, 2, "-127dBm 100%", "Updating: 42%");
  checkLayout(240, 135, 3, "WiFi:-127dBm Bat:100%", "Updating firmware: 42%");
  assert(!screenLayoutFor(128, 64) && !screenLayoutFor(240, 128));
  ScreenRenderer screen;
  ScreenState state;
  screen.draw(state); screen.drawStatus(state); screen.drawTelemetry(state.telemetry);
  Display unsupported(64, 64);
  assert(!screen.begin(unsupported));
  screen.draw(state); screen.drawStatus(state); screen.drawTelemetry(state.telemetry);
  assert(unsupported.operations.empty());
  std::cout << "Both screen profiles: labels, colors, bounded errors, partial clearing, progress and cached layout passed.\n";
}
