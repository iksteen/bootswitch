#include "virtual_disk.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <random>
#include <vector>
using namespace bootswitch;
int main() {
  std::vector<uint8_t> full(VirtualDisk::kCapacity), pool(1792 * 1024);
  std::vector<uint16_t> map(BootDisk::kSectorCount);
  VirtualDisk flat(full.data(), full.size()), sparse(pool.data(), pool.size(), map.data());
  const char config[] = "wifi_ssid=\nhostname=tqt\n";
  for (auto* disk : {&flat, &sparse}) assert(disk->format(config, sizeof(config) - 1));
  auto compare = [&](size_t start, size_t length, Mode mode) {
    std::vector<uint8_t> a(length), b(length);
    assert(flat.read(start / 512, start % 512, a.data(), length, mode) == int(length));
    assert(sparse.read(start / 512, start % 512, b.data(), length, mode) == int(length));
    assert(a == b);
  };
  compare(0, VirtualDisk::kCapacity, Mode::Linux);
  compare(0, VirtualDisk::kCapacity, Mode::Windows);
  char text[4097]; size_t length = 0;
  assert(!sparse.configuration(text, sizeof(text), length));
  assert(std::strcmp(text, config) == 0);
  // A firmware-sized contiguous upload, fragmented writes, partial sectors,
  // rewritten metadata and reads crossing the generated/stored boundary.
  std::mt19937 random(0x745154);
  std::vector<uint8_t> image(1550000);
  for (auto& b : image) b = random();
  const size_t data = (BootDisk::kPartitionStart + BootDisk::kDataSector + 16) * 512;
  assert(flat.write(0, data, image.data(), image.size()) == int(image.size()));
  assert(sparse.write(0, data, image.data(), image.size()) == int(image.size()));
  for (size_t i = 0; i < 1000; ++i) {
    size_t start = data + random() % (image.size() - 2048);
    size_t count = 1 + random() % 2048;
    for (size_t n = 0; n < count; ++n) image[n] = random();
    assert(flat.write(start / 512, start % 512, image.data(), count) == int(count));
    assert(sparse.write(start / 512, start % 512, image.data(), count) == int(count));
    compare(start - 17, count + 34, Mode::Linux);
  }
  compare(0, VirtualDisk::kCapacity, Mode::Linux);
  // An over-capacity USB write fails atomically, including its existing sector.
  std::vector<uint8_t> before(VirtualDisk::kCapacity), after(before.size());
  assert(sparse.read(0, 0, before.data(), before.size(), Mode::Linux) == int(before.size()));
  assert(sparse.write(0, 0, before.data(), before.size()) == -1);
  assert(sparse.read(0, 0, after.data(), after.size(), Mode::Linux) == int(after.size()));
  assert(before == after);
  assert(sparse.write(BootDisk::kSectorCount, 0, image.data(), 1) == -1);
  assert(sparse.write(0, UINT32_MAX, image.data(), 1) == -1);
  // Rebuilding releases every slot and removes old uploaded data.
  assert(sparse.format(config, sizeof(config) - 1));
  assert(flat.format(config, sizeof(config) - 1));
  compare(0, VirtualDisk::kCapacity, Mode::Linux);
  // Repeated small-file editing can exhaust the pool but cannot corrupt it.
  uint8_t sector[512] = {};
  size_t accepted = 0;
  for (uint32_t lba = 0; lba < BootDisk::kSectorCount; ++lba) {
    if (sparse.write(lba, 0, sector, 512) == 512) ++accepted;
  }
  assert(accepted == pool.size() / 512);
  assert(sparse.format(config, sizeof(config) - 1));
  assert(!sparse.configuration(text, sizeof(text), length));
  std::cout << "Sparse disk matches full storage, bounds memory and rejects full-pool writes atomically.\n";
}
