#pragma once

#include <cstdint>

namespace bootswitch {
struct DisplayTelemetryValues {
  bool wifiEnabled = false;
  bool wifiConnected = false;
  int32_t rssi = 0;
  int32_t batteryLevel = -1;
  bool batterySupported = true;
};

// Sampling is independent of user activity: updating these values must never
// keep the screen awake. Values are cached so redraws do not read the hardware.
class DisplayTelemetry {
 public:
  bool pollDue(uint32_t now, bool screenAwake);
  void invalidate() { havePoll_ = false; }
  bool update(bool wifiEnabled, bool wifiConnected, int32_t rssi, int32_t batteryLevel,
              bool batterySupported = true);
  const DisplayTelemetryValues& values() const { return values_; }

 private:
  uint32_t lastPoll_ = 0;
  bool havePoll_ = false;
  bool haveValues_ = false;
  DisplayTelemetryValues values_;
};
}  // namespace bootswitch
