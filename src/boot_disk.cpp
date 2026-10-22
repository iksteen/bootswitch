#include "boot_disk.h"

#include <algorithm>
#include <cstring>

namespace bootswitch {
namespace {
void put16(uint8_t* p, uint16_t value) {
  p[0] = value;
  p[1] = value >> 8;
}
void put32(uint8_t* p, uint32_t value) {
  put16(p, value);
  put16(p + 2, value >> 16);
}
constexpr char kLinuxMode[] = "linux  \n";
constexpr char kWindowsMode[] = "windows\n";
constexpr char kLinuxConfig[] = "set bootswitch_mode=linux  \n";
constexpr char kWindowsConfig[] = "set bootswitch_mode=windows\n";
static_assert(sizeof(kLinuxMode) == sizeof(kWindowsMode), "fixed file size");
static_assert(sizeof(kLinuxConfig) == sizeof(kWindowsConfig), "fixed file size");

void entry(uint8_t* p, const char* name, uint16_t cluster, uint32_t size) {
  std::memcpy(p, name, 11);
  p[11] = 0x21;  // read-only + archive
  put16(p + 16, 0x5D51);  // 2026-10-17, valid FAT date
  put16(p + 18, 0x5D51);
  put16(p + 24, 0x5D51);
  put16(p + 26, cluster);
  put32(p + 28, size);
}
}  // namespace

void BootDisk::sector(uint32_t lba, uint8_t* b, Mode mode) {
  std::memset(b, 0, kSectorSize);
  if (lba == 0) {
    uint8_t* p = b + 446;
    p[1] = p[5] = 0xFE;  // CHS is unused; LBA fields are authoritative.
    p[2] = p[6] = 0xFF;
    p[3] = p[7] = 0xFF;
    p[4] = 0x0E;  // FAT16 LBA, deliberately not bootable
    put32(p + 8, kPartitionStart);
    put32(p + 12, kVolumeSectors);
    b[510] = 0x55;
    b[511] = 0xAA;
    return;
  }
  if (lba < kPartitionStart || lba >= kSectorCount) return;
  const uint32_t relative = lba - kPartitionStart;
  if (relative == 0) {
    b[0] = 0xEB; b[1] = 0x3C; b[2] = 0x90;
    std::memcpy(b + 3, "BOOTSWCH", 8);
    put16(b + 11, kSectorSize);
    b[13] = 1;  // sectors per cluster
    put16(b + 14, 1);  // reserved sectors
    b[16] = 2;  // FAT copies
    put16(b + 17, 64);  // root entries
    put16(b + 19, kVolumeSectors);
    b[21] = 0xF8;
    put16(b + 22, kFatSectors);
    put16(b + 24, 63);
    put16(b + 26, 255);
    put32(b + 28, kPartitionStart);
    b[36] = 0x80;
    b[38] = 0x29;
    put32(b + 39, kSerial);
    std::memcpy(b + 43, "BOOTSWITCH ", 11);
    std::memcpy(b + 54, "FAT16   ", 8);
    b[510] = 0x55;
    b[511] = 0xAA;
  } else if (relative == 1 || relative == 1 + kFatSectors) {
    put16(b, 0xFFF8);
    put16(b + 2, 0xFFFF);
    put16(b + 4, 0xFFFF);  // cluster 2: MODE.TXT
    put16(b + 6, 0xFFFF);  // cluster 3: SWITCH.CFG
  } else if (relative == kRootSector) {
    std::memcpy(b, "BOOTSWITCH ", 11);
    b[11] = 0x08;
    entry(b + 32, "MODE    TXT", 2, sizeof(kLinuxMode) - 1);
    entry(b + 64, "SWITCH  CFG", 3, sizeof(kLinuxConfig) - 1);
  } else if (relative == kDataSector) {
    std::memcpy(b, mode == Mode::Windows ? kWindowsMode : kLinuxMode,
                sizeof(kLinuxMode) - 1);
  } else if (relative == kDataSector + 1) {
    std::memcpy(b, mode == Mode::Windows ? kWindowsConfig : kLinuxConfig,
                sizeof(kLinuxConfig) - 1);
  }
}

int32_t BootDisk::read(uint32_t lba, uint32_t offset, void* buffer,
                       uint32_t length, Mode mode) {
  const uint64_t start = uint64_t(lba) * kSectorSize + offset;
  const uint64_t capacity = uint64_t(kSectorCount) * kSectorSize;
  if (start > capacity || length > capacity - start ||
      length > INT32_MAX || (length && !buffer)) return -1;
  auto* out = static_cast<uint8_t*>(buffer);
  uint8_t block[kSectorSize];
  uint64_t position = start;
  uint32_t remaining = length;
  while (remaining) {
    sector(position / kSectorSize, block, mode);
    const uint32_t within = position % kSectorSize;
    const uint32_t count = std::min(remaining, kSectorSize - within);
    std::memcpy(out, block + within, count);
    position += count;
    out += count;
    remaining -= count;
  }
  return static_cast<int32_t>(length);
}
}  // namespace bootswitch
