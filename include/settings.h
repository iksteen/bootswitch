#pragma once

#include <cstddef>

namespace bootswitch {
struct Settings {
  char ssid[33] = {};
  char password[65] = {};
  char apiKey[97] = {};
  char hostname[64] = "bootswitch";

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
