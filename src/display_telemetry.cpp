#include "display_telemetry.h"

namespace bootswitch {
bool DisplayTelemetry::pollDue(uint32_t now, bool screenAwake) {
  if (!screenAwake) {
    invalidate();
    return false;
  }
  if (havePoll_ && uint32_t(now - lastPoll_) < 1000u) return false;
  havePoll_ = true;
  lastPoll_ = now;
  return true;
}

bool DisplayTelemetry::update(bool wifiEnabled, bool wifiConnected,
                              int32_t rssi, int32_t batteryLevel, bool batterySupported) {
  wifiConnected = wifiEnabled && wifiConnected;
  if (!wifiConnected || rssi >= 0) rssi = 0;
  if (!batterySupported || batteryLevel < 0 || batteryLevel > 100) batteryLevel = -1;
  const bool changed = !haveValues_ || values_.wifiEnabled != wifiEnabled ||
      values_.wifiConnected != wifiConnected || values_.rssi != rssi ||
      values_.batteryLevel != batteryLevel || values_.batterySupported != batterySupported;
  values_.wifiEnabled = wifiEnabled;
  values_.wifiConnected = wifiConnected;
  values_.rssi = rssi;
  values_.batteryLevel = batteryLevel;
  values_.batterySupported = batterySupported;
  haveValues_ = true;
  return changed;
}
}  // namespace bootswitch
