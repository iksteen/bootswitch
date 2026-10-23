#pragma once

#include <cstddef>
#include <cstdint>

namespace bootswitch {
struct Settings {
  // The millisecond timeout must fit in the unsigned millis() counter.
  static constexpr uint32_t kMaxScreenTimeoutSeconds = UINT32_MAX / 1000u;
  static constexpr uint32_t kMaxWifiListenInterval = 10;
  char ssid[33] = {};
  char password[65] = {};
  char wifiBssid[18] = {};
  char apiKey[97] = {};
  char hostname[64] = "bootswitch";
  char blePasskey[7] = {};
  uint32_t screenTimeoutSeconds = 60;
  // 0 follows the AP's DTIM; positive values use maximum modem sleep.
  uint32_t wifiListenInterval = 0;

  bool wifiEnabled() const { return ssid[0] != 0; }
  bool apiEnabled() const { return apiKey[0] != 0; }
  bool bleEnabled() const { return blePasskey[0] != 0; }
  // Empty selects any AP for the SSID. Nonempty pins one unicast AP address.
  bool wifiBssidBytes(uint8_t output[6]) const;
};

// Values are literal UTF-8 bytes after '='; only keys are whitespace-trimmed.
// Success is nullptr. Errors contain no secret values and leave output unchanged.
const char* parseSettings(const char* text, size_t length, Settings& output);
const char* validateSettings(const Settings& settings);
bool formatSettings(const Settings& settings, char* output, size_t capacity,
                    size_t& length);
}  // namespace bootswitch
