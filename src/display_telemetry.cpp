#include "display_telemetry.h"

#include <cstdio>
#include <cstring>

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
                              int32_t rssi, int32_t batteryLevel) {
  char wifi[20];
  if (!wifiEnabled) std::snprintf(wifi, sizeof(wifi), "WiFi:off");
  else if (!wifiConnected || rssi >= 0) std::snprintf(wifi, sizeof(wifi), "WiFi:--");
  else std::snprintf(wifi, sizeof(wifi), "WiFi:%lddBm", static_cast<long>(rssi));

  char battery[8];
  if (batteryLevel < 0 || batteryLevel > 100) std::snprintf(battery, sizeof(battery), "--");
  else std::snprintf(battery, sizeof(battery), "%ld%%", static_cast<long>(batteryLevel));

  char next[sizeof(text_)];
  std::snprintf(next, sizeof(next), "%s Bat:%s", wifi, battery);
  if (std::strcmp(next, text_) == 0) return false;
  std::memcpy(text_, next, std::strlen(next) + 1);
  return true;
}
}  // namespace bootswitch
