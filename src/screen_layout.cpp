#include "screen_layout.h"

namespace bootswitch {
namespace {
constexpr ScreenLayout kWide = {
  240, 135, {8, 5}, {8, 26}, 3, {8, 62, 224, 8}, 1,
  {8, 80}, {8, 98}, {8, 116}, {80, 4, 152, 12}, {232, 5},
  "A: toggle  B: wake PC", "CONFIG.INI: eject to apply", "BLE: on (PIN pairing)",
  " API off", "Updating firmware: %u%%", "WiFi:%lddBm", "%s Bat:%s"
};
constexpr ScreenLayout kSquare = {
  128, 128, {8, 5}, {8, 26}, 2, {8, 50, 112, 16}, 2,
  {8, 70}, {8, 86}, {8, 102}, {0, 115, 128, 12}, {120, 116},
  "A:toggle B:wake", "USB: config disk", "BLE: on (PIN)",
  "", "Updating: %u%%", "%lddBm", "%s %s"
};
}

const ScreenLayout* screenLayoutFor(int32_t width, int32_t height) {
  const ScreenLayout* layouts[] = {&kWide, &kSquare};
  for (const auto* layout : layouts)
    if (layout->width == width && layout->height == height) return layout;
  return nullptr;
}
}
