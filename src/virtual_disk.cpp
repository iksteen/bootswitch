#include "virtual_disk.h"

#include <algorithm>
#include <cstring>

namespace bootswitch {
namespace {
constexpr uint32_t kRootOffset = (BootDisk::kPartitionStart + BootDisk::kRootSector) * 512;
constexpr uint32_t kDataOffset = (BootDisk::kPartitionStart + BootDisk::kDataSector) * 512;
constexpr uint32_t kClusterCount = BootDisk::kVolumeSectors - BootDisk::kDataSector;
constexpr uint32_t kFatOffset = (BootDisk::kPartitionStart + 1) * 512;
uint16_t get16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
uint32_t get32(const uint8_t* p) { return uint32_t(get16(p)) | uint32_t(get16(p + 2)) << 16; }
void put16(uint8_t* p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
void put32(uint8_t* p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }
}  // namespace

bool VirtualDisk::format(const char* text, size_t length) {
  if (!bytes_ || capacity_ < kCapacity || !text || !length || length > kMaxConfigSize)
    return false;
  for (uint32_t lba = 0; lba < BootDisk::kSectorCount; ++lba)
    BootDisk::sector(lba, bytes_ + lba * 512, Mode::Linux);
  const size_t clusters = (length + 511) / 512;
  for (size_t i = 0; i < clusters; ++i) {
    const uint16_t next = i + 1 == clusters ? 0xFFFF : 5 + i;
    for (unsigned copy = 0; copy < 2; ++copy)
      put16(bytes_ + kFatOffset + copy * BootDisk::kFatSectors * 512 + (4 + i) * 2, next);
  }
  uint8_t* entry = bytes_ + kRootOffset + 96;
  std::memcpy(entry, "CONFIG  INI", 11);
  entry[11] = 0x20;  // writable, archive
  put16(entry + 16, 0x5D56);  // 2026-10-22
  put16(entry + 18, 0x5D56);
  put16(entry + 24, 0x5D56);
  put16(entry + 26, 4);
  put32(entry + 28, length);
  std::memcpy(bytes_ + kDataOffset + 1024, text, length);
  return true;
}

bool VirtualDisk::range(uint32_t lba, uint32_t offset, uint32_t length, uint64_t& start) const {
  start = uint64_t(lba) * 512 + offset;
  return bytes_ && capacity_ >= kCapacity && start <= kCapacity &&
         length <= kCapacity - start && length <= INT32_MAX;
}

int32_t VirtualDisk::write(uint32_t lba, uint32_t offset, const void* buffer, uint32_t length) {
  uint64_t start;
  if (!range(lba, offset, length, start) || (length && !buffer)) return -1;
  if (length) std::memcpy(bytes_ + start, buffer, length);
  return static_cast<int32_t>(length);
}

int32_t VirtualDisk::read(uint32_t lba, uint32_t offset, void* buffer, uint32_t length, Mode mode) const {
  uint64_t start;
  if (!range(lba, offset, length, start) || (length && !buffer)) return -1;
  if (length) std::memcpy(buffer, bytes_ + start, length);
  // Never let host writes replace the boot-selection payloads. Metadata for
  // these read-only files is checked before accepting a configuration save.
  const uint64_t end = start + length;
  for (unsigned index = 0; index < 2; ++index) {
    const uint64_t sectorStart = kDataOffset + index * 512;
    const uint64_t first = std::max(start, sectorStart);
    const uint64_t last = std::min(end, sectorStart + 512);
    if (first < last) {
      uint8_t sector[512];
      BootDisk::sector(sectorStart / 512, sector, mode);
      std::memcpy(static_cast<uint8_t*>(buffer) + first - start,
                  sector + first - sectorStart, last - first);
    }
  }
  return static_cast<int32_t>(length);
}

const char* VirtualDisk::configuration(char* output, size_t capacity, size_t& length) const {
  if (!bytes_ || capacity_ < kCapacity || !output) return "Config disk unavailable";
  const uint8_t* mbr = bytes_ + 446;
  const uint8_t* boot = bytes_ + BootDisk::kPartitionStart * 512;
  if (bytes_[510] != 0x55 || bytes_[511] != 0xAA || mbr[4] != 0x0E ||
      get32(mbr + 8) != BootDisk::kPartitionStart || get32(mbr + 12) != BootDisk::kVolumeSectors ||
      get16(boot + 11) != 512 || boot[13] != 1 || get16(boot + 14) != 1 ||
      boot[16] != 2 || get16(boot + 17) != 64 || get16(boot + 19) != BootDisk::kVolumeSectors ||
      get16(boot + 22) != BootDisk::kFatSectors || get32(boot + 39) != BootDisk::kSerial ||
      std::memcmp(boot + 43, "BOOTSWITCH ", 11) != 0)
    return "Disk layout changed";
  const uint8_t* root = bytes_ + kRootOffset;
  if (std::memcmp(root, "BOOTSWITCH ", 11) || root[11] != 0x08 ||
      std::memcmp(root + 32, "MODE    TXT", 11) || !(root[32 + 11] & 1) ||
      get16(root + 32 + 26) != 2 ||
      get32(root + 32 + 28) != 8 || std::memcmp(root + 64, "SWITCH  CFG", 11) ||
      !(root[64 + 11] & 1) || get16(root + 64 + 26) != 3 || get32(root + 64 + 28) != 28)
    return "Boot files changed";
  for (unsigned copy = 0; copy < 2; ++copy) {
    const uint8_t* fat = bytes_ + kFatOffset + copy * BootDisk::kFatSectors * 512;
    if (get16(fat + 4) < 0xFFF8 || get16(fat + 6) < 0xFFF8)
      return "Boot file chains changed";
  }
  const uint8_t* file = nullptr;
  for (unsigned index = 0; index < 64; ++index) {
    const uint8_t* entry = root + index * 32;
    if (entry[0] == 0) break;
    if (entry[0] == 0xE5 || (entry[11] & 0x18) || entry[11] == 0x0F) continue;
    if (std::memcmp(entry, "CONFIG  INI", 11) == 0) {
      if (file) return "Duplicate CONFIG.INI";
      file = entry;
    }
  }
  if (!file) return "CONFIG.INI missing";
  const uint32_t size = get32(file + 28);
  if (size > kMaxConfigSize || capacity <= size) return "CONFIG.INI too large";
  if (!size) {
    output[0] = 0;
    length = 0;
    return nullptr;
  }
  uint16_t cluster = get16(file + 26);
  uint16_t visited[kMaxConfigSize / 512] = {};
  size_t count = 0, remaining = size;
  while (remaining) {
    if (cluster < 4 || cluster >= kClusterCount + 2) return "Invalid config cluster";
    for (size_t i = 0; i < count; ++i)
      if (visited[i] == cluster) return "Config cluster loop";
    visited[count++] = cluster;
    const size_t bytes = std::min(remaining, size_t(512));
    std::memcpy(output + size - remaining, bytes_ + kDataOffset + (cluster - 2) * 512, bytes);
    remaining -= bytes;
    const uint16_t next = get16(bytes_ + kFatOffset + cluster * 2);
    if (next != get16(bytes_ + kFatOffset + BootDisk::kFatSectors * 512 + cluster * 2))
      return "FAT copies differ";
    if (remaining && next >= 0xFFF8) return "Config file incomplete";
    if (!remaining && next < 0xFFF8) return "Invalid config chain";
    cluster = next;
  }
  output[size] = 0;
  length = size;
  return nullptr;
}
}  // namespace bootswitch
