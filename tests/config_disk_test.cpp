#include "settings.h"
#include "virtual_disk.h"

#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using namespace bootswitch;

static const std::string valid =
    "wifi_ssid=Test network\n"
    "wifi_password=a-test-password\n"
    "api_key=0123456789abcdef0123456789abcdef\n"
    "hostname=bootswitch\n";

bool sameSettings(const Settings& a, const Settings& b) {
  return std::strcmp(a.ssid, b.ssid) == 0 &&
         std::strcmp(a.password, b.password) == 0 &&
         std::strcmp(a.apiKey, b.apiKey) == 0 &&
         std::strcmp(a.hostname, b.hostname) == 0 &&
         std::strcmp(a.blePasskey, b.blePasskey) == 0 &&
         a.screenTimeoutSeconds == b.screenTimeoutSeconds;
}

void parserTests() {
  Settings settings;
  assert(!parseSettings(valid.data(), valid.size(), settings));
  assert(std::strcmp(settings.ssid, "Test network") == 0);
  char serialized[1024];
  size_t length;
  assert(formatSettings(settings, serialized, sizeof(serialized), length));
  Settings roundtrip;
  assert(!parseSettings(serialized, length, roundtrip));
  assert(sameSettings(settings, roundtrip));
  assert(!formatSettings(settings, serialized, 8, length));

  auto rejects = [&](const std::string& text) {
    Settings unchanged = settings;
    assert(parseSettings(text.data(), text.size(), unchanged));
    assert(sameSettings(settings, unchanged));
  };
  rejects(valid + "api_key=0123456789abcdef\n");
  rejects(valid + "typo=value\n");
  rejects(valid + "not-an-assignment\n");
  rejects(valid + "ble_passkey=12345\n");
  rejects(valid + "ble_passkey=1234567\n");
  rejects(valid + "ble_passkey=12345x\n");
  rejects(valid + "ble_passkey= 12345\n");
  rejects(valid + "ble_passkey=123456\nble_passkey=654321\n");
  for (const char* invalid : {"", "-1", "+1", "1.5", " 60", "60 ", "4294968",
                              "4294967296", "99999999999999999999999"})
    rejects(valid + "screen_timeout=" + invalid + "\n");
  rejects(valid + "screen_timeout=0\nscreen_timeout=60\n");
  rejects("wifi_ssid=" + std::string(33, 'x') + "\nwifi_password=\napi_key=0123456789abcdef\nhostname=test\n");
  rejects("wifi_ssid=test\nwifi_password=short\napi_key=0123456789abcdef\nhostname=test\n");
  rejects("wifi_ssid=test\nwifi_password=\napi_key=short\nhostname=test\n");
  rejects("wifi_ssid=test\nwifi_password=\napi_key=01234567 9abcdef\nhostname=test\n");
  rejects("wifi_ssid=test\nwifi_password=\napi_key=0123456789abcdef\nhostname=-bad\n");
  rejects("wifi_ssid=test\nwifi_password=\napi_key=0123456789abcdef\nhostname=bad.local\n");
  rejects(valid + std::string("\0hidden", 7));
  rejects("wifi_ssid=test\nwifi_password=" + std::string(64, 'g') + "\napi_key=0123456789abcdef\nhostname=test\n");
  const std::string complex = "\xEF\xBB\xBF# UTF-8 BOM and CRLF\r\n"
      "wifi_ssid=  Leading and trailing  \r\n"
      "wifi_password=space # = password  \r\n"
      "api_key=0123456789abcdef#=\r\n"
      "hostname=my-stick\r\n";
  assert(!parseSettings(complex.data(), complex.size(), roundtrip));
  assert(std::strcmp(roundtrip.ssid, "  Leading and trailing  ") == 0);
  assert(std::strcmp(roundtrip.password, "space # = password  ") == 0);
  const std::string open = "wifi_ssid=\nwifi_password=\napi_key=0123456789abcdef\nhostname=test";
  assert(!parseSettings(open.data(), open.size(), roundtrip));
  const std::string hex = "wifi_ssid=test\nwifi_password=" + std::string(64, 'a') +
      "\napi_key=0123456789abcdef\nhostname=test\n";
  assert(!parseSettings(hex.data(), hex.size(), roundtrip));

  // Defaults and omitted/empty fields disable services instead of keeping
  // stale credentials or turning an empty key into anonymous authentication.
  Settings empty;
  assert(!empty.wifiEnabled() && !empty.apiEnabled());
  assert(std::strcmp(empty.hostname, "bootswitch") == 0);
  assert(!validateSettings(empty));
  assert(empty.screenTimeoutSeconds == 60);
  assert(!parseSettings("", 0, roundtrip));
  assert(!roundtrip.wifiEnabled() && !roundtrip.apiEnabled());
  assert(!parseSettings("# No credentials\n", 17, roundtrip));
  assert(!roundtrip.wifiEnabled() && !roundtrip.apiEnabled());
  const std::string noKey = "wifi_ssid=test\nwifi_password=test-password\n";
  assert(!parseSettings(noKey.data(), noKey.size(), roundtrip));
  assert(roundtrip.wifiEnabled() && !roundtrip.apiEnabled());
  const std::string keyOnly = "api_key=0123456789abcdef\n";
  assert(!parseSettings(keyOnly.data(), keyOnly.size(), roundtrip));
  assert(!roundtrip.wifiEnabled() && roundtrip.apiEnabled());
  const std::string noSSID = valid.substr(valid.find('\n') + 1);
  assert(!parseSettings(noSSID.data(), noSSID.size(), roundtrip));
  assert(!roundtrip.wifiEnabled() && roundtrip.apiEnabled());
  const std::string blankKey = "wifi_ssid=test\nwifi_password=\napi_key=\n";
  assert(!parseSettings(blankKey.data(), blankKey.size(), roundtrip));
  assert(roundtrip.wifiEnabled() && !roundtrip.apiEnabled());
  assert(formatSettings(empty, serialized, sizeof(serialized), length));
  assert(!parseSettings(serialized, length, roundtrip));
  assert(!roundtrip.wifiEnabled() && !roundtrip.apiEnabled());
  assert(!roundtrip.bleEnabled());
  assert(roundtrip.screenTimeoutSeconds == 60);
  for (const uint32_t seconds : {0u, 1u, 120u, Settings::kMaxScreenTimeoutSeconds}) {
    const std::string text = valid + "screen_timeout=" + std::to_string(seconds) + "\n";
    assert(!parseSettings(text.data(), text.size(), roundtrip));
    assert(roundtrip.screenTimeoutSeconds == seconds);
    assert(formatSettings(roundtrip, serialized, sizeof(serialized), length));
    Settings restored;
    assert(!parseSettings(serialized, length, restored));
    assert(restored.screenTimeoutSeconds == seconds);
  }
  Settings invalidTimeout;
  invalidTimeout.screenTimeoutSeconds = Settings::kMaxScreenTimeoutSeconds + 1;
  assert(validateSettings(invalidTimeout));
  assert(!formatSettings(invalidTimeout, serialized, sizeof(serialized), length));
  // BLE is opt-in, independent of Wi-Fi and HTTP, with exact six-digit PINs.
  const std::string bleOnly = "ble_passkey=012345\n";
  assert(!parseSettings(bleOnly.data(), bleOnly.size(), roundtrip));
  assert(roundtrip.bleEnabled() && !roundtrip.wifiEnabled() && !roundtrip.apiEnabled());
  assert(std::strcmp(roundtrip.blePasskey, "012345") == 0);
  assert(formatSettings(roundtrip, serialized, sizeof(serialized), length));
  Settings bleRoundtrip;
  assert(!parseSettings(serialized, length, bleRoundtrip));
  assert(std::strcmp(bleRoundtrip.blePasskey, "012345") == 0);
  assert(!parseSettings(valid.data(), valid.size(), roundtrip));
  assert(!roundtrip.bleEnabled());  // Existing saved configurations remain valid.
  const std::string blankBle = valid + "ble_passkey=\n";
  assert(!parseSettings(blankBle.data(), blankBle.size(), roundtrip));
  assert(!roundtrip.bleEnabled());
}

int main(int argc, char** argv) {
  if (argc == 4 && std::string(argv[1]) == "--extract") {
    std::ifstream input(argv[2], std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
    VirtualDisk disk(bytes.data(), bytes.size());
    char config[VirtualDisk::kMaxConfigSize + 1];
    size_t length;
    const char* error = disk.configuration(config, sizeof(config), length);
    Settings settings;
    if (!error) error = parseSettings(config, length, settings);
    if (error) { std::cerr << error << '\n'; return 1; }
    std::ofstream output(argv[3], std::ios::binary);
    output.write(config, length);
    assert(output.good());
    std::cout << "Edited FAT configuration extracted and validated.\n";
    return 0;
  }
  parserTests();
  std::vector<uint8_t> bytes(VirtualDisk::kCapacity);
  VirtualDisk disk(bytes.data(), bytes.size());
  assert(disk.format(valid.data(), valid.size()));
  char config[VirtualDisk::kMaxConfigSize + 1];
  size_t length = 0;
  assert(!disk.configuration(config, sizeof(config), length));
  assert(std::string(config, length) == valid);
  // Partial and cross-sector host writes round-trip without corrupting neighbors.
  uint8_t changed[1201];
  std::memset(changed, 0x6a, sizeof(changed));
  assert(disk.write(1000, 507, changed, sizeof(changed)) == sizeof(changed));
  uint8_t readback[1201];
  assert(disk.read(1000, 507, readback, sizeof(readback), Mode::Linux) == sizeof(readback));
  assert(std::memcmp(changed, readback, sizeof(changed)) == 0);
  assert(bytes[1000 * 512 + 506] == 0 && bytes[1000 * 512 + 507 + sizeof(changed)] == 0);
  assert(disk.write(BootDisk::kSectorCount - 1, 511, changed, 2) == -1);
  assert(disk.read(UINT32_MAX, UINT32_MAX, readback, 1, Mode::Linux) == -1);
  assert(disk.write(0, 0, nullptr, 1) == -1);
  assert(disk.read(0, 0, nullptr, 1, Mode::Linux) == -1);
  assert(disk.write(BootDisk::kSectorCount, 0, nullptr, 0) == 0);
  const uint32_t data = BootDisk::kPartitionStart + BootDisk::kDataSector;
  assert(disk.write(data, 0, changed, 512) == 512);
  assert(disk.read(data, 0, readback, 512, Mode::Windows) == 512);
  assert(std::memcmp(readback, "windows\n", 8) == 0);
  assert(disk.read(data, 0, readback, 512, Mode::Linux) == 512);
  assert(std::memcmp(readback, "linux  \n", 8) == 0);
  assert(disk.read(data + 1, 0, readback, 512, Mode::Windows) == 512);
  assert(std::memcmp(readback, "set bootswitch_mode=windows\n", 28) == 0);

  // A corrupt/looped chain and oversized files fail without reading outside RAM.
  const uint32_t root = (BootDisk::kPartitionStart + BootDisk::kRootSector) * 512;
  auto original = bytes;
  // Emptying CONFIG.INI intentionally disables both services.
  std::memset(bytes.data() + root + 96 + 28, 0, 4);
  assert(!disk.configuration(config, sizeof(config), length));
  assert(length == 0);
  Settings emptyConfig;
  assert(!parseSettings(config, length, emptyConfig));
  assert(!emptyConfig.wifiEnabled() && !emptyConfig.apiEnabled());
  bytes = original;
  bytes[root + 96 + 28] = 0;
  bytes[root + 96 + 29] = 0x20;
  assert(disk.configuration(config, sizeof(config), length));
  bytes = original;
  bytes[root + 96 + 28] = 1;
  bytes[root + 96 + 29] = 4;  // 1025 bytes
  for (unsigned copy = 0; copy < 2; ++copy) {
    const uint32_t fat = (BootDisk::kPartitionStart + 1 + copy * BootDisk::kFatSectors) * 512;
    bytes[fat + 8] = 4;
    bytes[fat + 9] = 0;
  }
  assert(disk.configuration(config, sizeof(config), length));
  // Reset reconstructs only the committed config, discarding temporary files.
  assert(disk.format(valid.data(), valid.size()));
  assert(!disk.configuration(config, sizeof(config), length));
  assert(std::string(config, length) == valid);
  if (argc == 2) {
    std::ofstream output(std::string(argv[1]) + "/config-disk.img", std::ios::binary);
    output.write(reinterpret_cast<char*>(bytes.data()), bytes.size());
    assert(output.good());
  }
  std::cout << "Configuration parser, writable disk, bounds, and corrupt-chain checks passed.\n";
}
