#include "firmware_download.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
using namespace bootswitch;
namespace {
class Stream : public FirmwareDownloadStream {
 public:
  std::vector<uint8_t> bytes;
  size_t position = 0, chunk = 731;
  int64_t declared = -1;
  uint32_t clock = UINT32_MAX - 1000, tick = 1;
  bool stalled = false, dropped = false, failed = false, oversizedRead = false;
  int64_t size() const override { return declared; }
  int read(uint8_t* output, size_t capacity) override {
    if (failed) return -1;
    if (oversizedRead) return capacity + 1;
    if (stalled || dropped) return 0;
    const size_t count = std::min({capacity, bytes.size() - position, chunk});
    std::memcpy(output, bytes.data() + position, count);
    position += count;
    return count;
  }
  bool connected() override { return !dropped && position < bytes.size(); }
  uint32_t now() override { return clock; }
  void idle() override { clock += tick; }
};

void tests(bool sparse) {
  std::vector<uint8_t> storage(sparse ? 1792 * 1024 : VirtualDisk::kCapacity);
  std::vector<uint16_t> map(BootDisk::kSectorCount);
  VirtualDisk disk(storage.data(), storage.size(), sparse ? map.data() : nullptr);
  const char config[] = "hostname=downloads\n";
  auto reset = [&] { assert(disk.format(config, sizeof(config) - 1)); };
  Stream good;
  good.bytes.resize(1550000);
  for (size_t i = 0; i < good.bytes.size(); ++i) good.bytes[i] = uint8_t(i * 13);
  good.declared = good.bytes.size();
  reset();
  assert(!receiveFirmware(good, disk, 0x180000 + 80));
  VirtualDisk::File file;
  assert(!disk.firmware(file) && file && file.size() == good.bytes.size());
  std::vector<uint8_t> received(file.size());
  assert(file.read(received.data(), received.size()) == received.size());
  assert(received == good.bytes);
  char text[256]; size_t length;
  assert(!disk.configuration(text, sizeof(text), length) && !std::strcmp(text, config));
  assert(!disk.writeFirmware(good.bytes.size(), "x", 1));
  assert(!disk.writeFirmware(SIZE_MAX, "x", 1));
  reset();
  assert(!disk.writeFirmware(0, "x", 1));
  const int64_t invalidSizes[] = {-1, 0, 80, 0x180000 + 81, INT64_MAX};
  for (const int64_t size : invalidSizes) {
    Stream invalid; invalid.declared = size;
    const char* error = receiveFirmware(invalid, disk, 0x180000 + 80);
    assert(error && std::strstr(error, size <= 80 ? "size" : "large"));
    assert(!disk.firmware(file) && !file);
  }
  for (unsigned fault = 0; fault < 6; ++fault) {
    reset();
    Stream stream; stream.bytes.resize(200); stream.declared = 400;
    const char* expected;
    switch (fault) {
      case 0: expected = "Incomplete firmware download"; break;
      case 1: stream.dropped = true; expected = "Incomplete firmware download"; break;
      case 2: stream.stalled = true; stream.tick = 1000; expected = "Firmware download timed out"; break;
      case 3: stream.failed = true; expected = "Firmware download failed"; break;
      case 4: stream.oversizedRead = true; expected = "Firmware download failed"; break;
      default:
        stream.bytes.resize(400); stream.chunk = 1; stream.tick = 1000;
        expected = "Firmware download timed out"; break;
    }
    const char* error = receiveFirmware(stream, disk, 0x180000 + 80);
    assert(error && !std::strcmp(error, expected));
  }
  // The 4096-byte maximum CONFIG.INI ends immediately before the download.
  std::vector<char> largeConfig(4096, 'c');
  assert(disk.format(largeConfig.data(), largeConfig.size()));
  Stream small; small.bytes.resize(513, 0x55); small.declared = small.bytes.size();
  assert(!receiveFirmware(small, disk, 1000));
  char configCopy[4097];
  assert(!disk.configuration(configCopy, sizeof(configCopy), length));
  assert(length == largeConfig.size() && !std::memcmp(configCopy, largeConfig.data(), length));
}
}
int main() {
  tests(false); tests(true);
  std::cout << "Downloads: flat/sparse storage, bounded size, exact bytes, interrupted transfers, timeouts and clock wrap passed.\n";
}
