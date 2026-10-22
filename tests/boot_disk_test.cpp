#include "boot_disk.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

using bootswitch::BootDisk;
using bootswitch::Mode;

int main(int argc, char** argv) {
  std::vector<uint8_t> linuxDisk(BootDisk::kSectorCount * BootDisk::kSectorSize);
  std::vector<uint8_t> windowsDisk(linuxDisk.size());
  assert(BootDisk::read(0, 0, linuxDisk.data(), linuxDisk.size(), Mode::Linux) ==
         int32_t(linuxDisk.size()));
  assert(BootDisk::read(0, 0, windowsDisk.data(), windowsDisk.size(), Mode::Windows) ==
         int32_t(windowsDisk.size()));
  assert(linuxDisk[510] == 0x55 && linuxDisk[511] == 0xAA);
  const uint32_t firstFat = BootDisk::kPartitionStart + 1;
  assert(std::memcmp(linuxDisk.data() + firstFat * 512,
                     linuxDisk.data() + (firstFat + BootDisk::kFatSectors) * 512,
                     BootDisk::kFatSectors * 512) == 0);
  const uint32_t modeOffset = (BootDisk::kPartitionStart + BootDisk::kDataSector) * 512;
  assert(std::memcmp(linuxDisk.data() + modeOffset, "linux  \n", 8) == 0);
  assert(std::memcmp(windowsDisk.data() + modeOffset, "windows\n", 8) == 0);
  // Mode changes may only affect payload sectors, not filesystem metadata.
  for (uint32_t i = 0; i < linuxDisk.size(); ++i) {
    if (i < modeOffset || i >= modeOffset + 1024)
      assert(linuxDisk[i] == windowsDisk[i]);
  }
  uint8_t partial[1301];
  for (uint32_t start : {0u, 509u, modeOffset - 7, modeOffset + 3}) {
    assert(BootDisk::read(start / 512, start % 512, partial, sizeof(partial),
                          Mode::Windows) == sizeof(partial));
    assert(std::memcmp(partial, windowsDisk.data() + start, sizeof(partial)) == 0);
  }
  assert(BootDisk::read(BootDisk::kSectorCount - 1, 511, partial, 1, Mode::Linux) == 1);
  assert(BootDisk::read(BootDisk::kSectorCount - 1, 511, partial, 2, Mode::Linux) == -1);
  assert(BootDisk::read(UINT32_MAX, UINT32_MAX, partial, 1, Mode::Linux) == -1);
  assert(BootDisk::read(0, 0, nullptr, 1, Mode::Linux) == -1);
  assert(BootDisk::read(BootDisk::kSectorCount, 0, nullptr, 0, Mode::Linux) == 0);
  if (argc == 2) {
    std::ofstream linuxFile(std::string(argv[1]) + "/linux.img", std::ios::binary);
    linuxFile.write(reinterpret_cast<char*>(linuxDisk.data()), linuxDisk.size());
    std::ofstream windowsFile(std::string(argv[1]) + "/windows.img", std::ios::binary);
    windowsFile.write(reinterpret_cast<char*>(windowsDisk.data()), windowsDisk.size());
    assert(linuxFile.good() && windowsFile.good());
  }
  std::cout << "Boot disk read, boundary, mode, and metadata checks passed.\n";
}
