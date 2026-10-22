#include "settings.h"

#include <cstdio>
#include <cinttypes>
#include <cstring>

namespace bootswitch {
namespace {
bool textValue(const char* value) {
  for (; *value; ++value) {
    const unsigned char c = *value;
    if (c < 32 || c == 127) return false;
  }
  return true;
}
bool hexKey(const char* value) {
  for (; *value; ++value) {
    if (!((*value >= '0' && *value <= '9') ||
          (*value >= 'a' && *value <= 'f') ||
          (*value >= 'A' && *value <= 'F'))) return false;
  }
  return true;
}

const char* screenTimeout(const char* value, uint32_t& seconds) {
  if (!*value) return "Screen timeout: seconds";
  uint32_t parsed = 0;
  for (; *value; ++value) {
    if (*value < '0' || *value > '9') return "Screen timeout: seconds";
    const uint32_t digit = *value - '0';
    if (parsed > (Settings::kMaxScreenTimeoutSeconds - digit) / 10u)
      return "Screen timeout out of range";
    parsed = parsed * 10u + digit;
  }
  seconds = parsed;
  return nullptr;
}
}  // namespace

const char* validateSettings(const Settings& s) {
  if (s.screenTimeoutSeconds > Settings::kMaxScreenTimeoutSeconds)
    return "Screen timeout out of range";
  // Arrays must be terminated even when loading untrusted settings.
  if (!std::memchr(s.ssid, 0, sizeof(s.ssid)) ||
      !std::memchr(s.password, 0, sizeof(s.password)) ||
      !std::memchr(s.apiKey, 0, sizeof(s.apiKey)) ||
      !std::memchr(s.hostname, 0, sizeof(s.hostname)) ||
      !std::memchr(s.blePasskey, 0, sizeof(s.blePasskey))) return "Setting too long";
  if (!textValue(s.ssid) || !textValue(s.password)) return "Invalid Wi-Fi text";
  const size_t passwordLength = std::strlen(s.password);
  if (s.wifiEnabled() && passwordLength && (passwordLength < 8 ||
      (passwordLength == 64 && !hexKey(s.password)))) return "Invalid Wi-Fi password";
  const size_t keyLength = std::strlen(s.apiKey);
  if (keyLength && (keyLength < 16 || keyLength > 96)) return "API key: 16-96 chars";
  for (const unsigned char* c = reinterpret_cast<const unsigned char*>(s.apiKey); *c; ++c) {
    if (*c < 33 || *c > 126) return "API key: no whitespace";
  }
  const size_t hostLength = std::strlen(s.hostname);
  if (!hostLength || s.hostname[0] == '-' || s.hostname[hostLength - 1] == '-')
    return "Invalid hostname";
  for (const char* c = s.hostname; *c; ++c) {
    if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-'))
      return "Invalid hostname";
  }
  if (s.bleEnabled()) {
    if (std::strlen(s.blePasskey) != 6) return "BLE passkey: 6 digits";
    for (const char* c = s.blePasskey; *c; ++c) {
      if (*c < '0' || *c > '9') return "BLE passkey: 6 digits";
    }
  }
  return nullptr;
}

const char* parseSettings(const char* text, size_t length, Settings& output) {
  if (!text || std::memchr(text, 0, length)) return "Invalid config text";
  Settings candidate;
  char timeoutText[11] = {};
  unsigned seen = 0;
  size_t position = 0;
  // UTF-8 BOM from Windows text editors is allowed.
  if (length >= 3 && std::memcmp(text, "\xEF\xBB\xBF", 3) == 0) position = 3;
  while (position < length) {
    const size_t start = position;
    while (position < length && text[position] != '\n') ++position;
    size_t end = position;
    if (position < length) ++position;
    if (end > start && text[end - 1] == '\r') --end;
    size_t keyStart = start;
    while (keyStart < end && (text[keyStart] == ' ' || text[keyStart] == '\t')) ++keyStart;
    if (keyStart == end || text[keyStart] == '#' || text[keyStart] == ';') continue;
    size_t equals = keyStart;
    while (equals < end && text[equals] != '=') ++equals;
    if (equals == end) return "Expected key=value";
    size_t keyEnd = equals;
    while (keyEnd > keyStart && (text[keyEnd - 1] == ' ' || text[keyEnd - 1] == '\t')) --keyEnd;
    char* destination = nullptr;
    size_t capacity = 0;
    unsigned bit = 0;
    struct Field { const char* name; char* value; size_t capacity; unsigned bit; };
    Field fields[] = {
      {"wifi_ssid", candidate.ssid, sizeof(candidate.ssid), 1},
      {"wifi_password", candidate.password, sizeof(candidate.password), 2},
      {"api_key", candidate.apiKey, sizeof(candidate.apiKey), 4},
      {"hostname", candidate.hostname, sizeof(candidate.hostname), 8},
      {"ble_passkey", candidate.blePasskey, sizeof(candidate.blePasskey), 16},
      {"screen_timeout", timeoutText, sizeof(timeoutText), 32},
    };
    for (const auto& field : fields) {
      if (std::strlen(field.name) == keyEnd - keyStart &&
          std::memcmp(text + keyStart, field.name, keyEnd - keyStart) == 0) {
        destination = field.value;
        capacity = field.capacity;
        bit = field.bit;
        break;
      }
    }
    if (!destination) return "Unknown config key";
    if (seen & bit) return "Duplicate config key";
    seen |= bit;
    const size_t valueLength = end - equals - 1;
    if (valueLength >= capacity) return "Setting too long";
    std::memcpy(destination, text + equals + 1, valueLength);
    destination[valueLength] = 0;
  }
  if (seen & 32) {
    if (const char* error = screenTimeout(timeoutText, candidate.screenTimeoutSeconds))
      return error;
  }
  // Omitted fields use defaults. In particular, removing an
  // API key must never preserve old credentials or enable anonymous access.
  if (const char* error = validateSettings(candidate)) return error;
  output = candidate;
  return nullptr;
}

bool formatSettings(const Settings& s, char* output, size_t capacity, size_t& length) {
  if (validateSettings(s)) return false;
  const int written = std::snprintf(output, capacity,
      "# BootSwitch settings: edit, save, then safely eject to apply.\r\n"
      "# Values are literal: no quotes or spaces added around '='.\r\n"
      "# Credentials are readable by anyone with access to this USB disk.\r\n"
      "wifi_ssid=%s\r\nwifi_password=%s\r\napi_key=%s\r\nhostname=%s\r\n"
      "ble_passkey=%s\r\n"
      "# Screen timeout in seconds; 0 keeps the display on.\r\n"
      "screen_timeout=%" PRIu32 "\r\n",
      s.ssid, s.password, s.apiKey, s.hostname, s.blePasskey, s.screenTimeoutSeconds);
  if (written < 0 || static_cast<size_t>(written) >= capacity) return false;
  length = static_cast<size_t>(written);
  return true;
}
}  // namespace bootswitch
