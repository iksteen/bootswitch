#pragma once

#include <cstdint>

namespace bootswitch {
// Sampling is independent of user activity: updating these values must never
// keep the screen awake. Values are cached so redraws do not read the hardware.
class DisplayTelemetry {
 public:
  bool pollDue(uint32_t now, bool screenAwake);
  void invalidate() { havePoll_ = false; }
  bool update(bool wifiEnabled, bool wifiConnected, int32_t rssi, int32_t batteryLevel);
  const char* text() const { return text_; }

 private:
  uint32_t lastPoll_ = 0;
  bool havePoll_ = false;
  char text_[40] = "WiFi:-- Bat:--";
};
}  // namespace bootswitch
