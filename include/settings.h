#pragma once

#include <cstddef>
#include <cstdint>

namespace bootswitch {
struct Settings {
  // The millisecond timeout must fit in the unsigned millis() counter.
  static constexpr uint32_t kMaxScreenTimeoutSeconds = UINT32_MAX / 1000u;
  char ssid[33] = {};
  char password[65] = {};
  char apiKey[97] = {};
  char hostname[64] = "bootswitch";
  uint32_t screenTimeoutSeconds = 60;

  bool wifiEnabled() const { return ssid[0] != 0; }
  bool apiEnabled() const { return apiKey[0] != 0; }
};

// Values are literal UTF-8 bytes after '='; only keys are whitespace-trimmed.
// Success is nullptr. Errors contain no secret values and leave output unchanged.
const char* parseSettings(const char* text, size_t length, Settings& output);
const char* validateSettings(const Settings& settings);
bool formatSettings(const Settings& settings, char* output, size_t capacity,
                    size_t& length);
}  // namespace bootswitch
