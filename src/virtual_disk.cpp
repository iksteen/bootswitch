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

bool VirtualDisk::available() const {
  return bytes_ && (sectorMap_ ? capacity_ >= 512 : capacity_ >= kCapacity);
}

void VirtualDisk::readRaw(size_t start, void* output, size_t length) const {
  if (!sectorMap_) {
    if (length) std::memcpy(output, bytes_ + start, length);
    return;
  }
  auto* out = static_cast<uint8_t*>(output);
  while (length) {
    const size_t sector = start / 512, offset = start % 512;
    const size_t count = std::min(length, 512 - offset);
    if (sectorMap_[sector]) {
      std::memcpy(out, bytes_ + (sectorMap_[sector] - 1) * 512 + offset, count);
    } else {
      uint8_t base[512];
      BootDisk::sector(sector, base, Mode::Linux);
      std::memcpy(out, base + offset, count);
    }
    out += count;
    start += count;
    length -= count;
  }
}

uint16_t VirtualDisk::fatEntry(size_t offset) const {
  uint8_t value[2];
  readRaw(offset, value, sizeof(value));
  return get16(value);
}

bool VirtualDisk::format(const char* text, size_t length) {
  if (!available() || !text || !length || length > kMaxConfigSize) return false;
#if BOOTSWITCH_ENABLE_USB_UPDATE
  downloadSize_ = 0;
#endif
  if (sectorMap_) {
    std::memset(sectorMap_, 0, BootDisk::kSectorCount * sizeof(*sectorMap_));
    usedSectors_ = 0;
  } else {
    for (uint32_t lba = 0; lba < BootDisk::kSectorCount; ++lba)
      BootDisk::sector(lba, bytes_ + lba * 512, Mode::Linux);
  }
  const size_t clusters = (length + 511) / 512;
  for (size_t i = 0; i < clusters; ++i) {
    uint8_t next[2];
    put16(next, i + 1 == clusters ? 0xFFFF : 5 + i);
    for (unsigned copy = 0; copy < 2; ++copy)
      if (write(0, kFatOffset + copy * BootDisk::kFatSectors * 512 + (4 + i) * 2,
                next, 2) != 2) return false;
  }
  uint8_t entry[32] = {};
  std::memcpy(entry, "CONFIG  INI", 11);
  entry[11] = 0x20;
  put16(entry + 16, 0x5D56);
  put16(entry + 18, 0x5D56);
  put16(entry + 24, 0x5D56);
  put16(entry + 26, 4);
  put32(entry + 28, length);
  return write(0, kRootOffset + 96, entry, sizeof(entry)) == sizeof(entry) &&
         write(0, kDataOffset + 1024, text, length) == static_cast<int32_t>(length);
}

bool VirtualDisk::range(uint32_t lba, uint32_t offset, uint32_t length, uint64_t& start) const {
  start = uint64_t(lba) * 512 + offset;
  return available() && start <= kCapacity &&
         length <= kCapacity - start && length <= INT32_MAX;
}

int32_t VirtualDisk::write(uint32_t lba, uint32_t offset, const void* buffer, uint32_t length) {
  uint64_t start;
  if (!range(lba, offset, length, start) || (length && !buffer)) return -1;
  if (!sectorMap_) {
    if (length) std::memcpy(bytes_ + start, buffer, length);
  } else if (length) {
    // Reserve the entire request before changing anything. A full pool must
    // never leave a partially acknowledged USB write in the filesystem.
    const size_t first = start / 512, last = (start + length - 1) / 512;
    size_t needed = 0;
    for (size_t sector = first; sector <= last; ++sector)
      if (!sectorMap_[sector]) ++needed;
    if (needed > capacity_ / 512 - usedSectors_) return -1;
    auto* input = static_cast<const uint8_t*>(buffer);
    size_t remaining = length;
    while (remaining) {
      const size_t sector = start / 512, offset = start % 512;
      const size_t count = std::min(remaining, 512 - offset);
      if (!sectorMap_[sector]) {
        BootDisk::sector(sector, bytes_ + usedSectors_ * 512, Mode::Linux);
        sectorMap_[sector] = ++usedSectors_;
      }
      std::memcpy(bytes_ + (sectorMap_[sector] - 1) * 512 + offset, input, count);
      input += count;
      start += count;
      remaining -= count;
    }
  }
  return static_cast<int32_t>(length);
}

int32_t VirtualDisk::read(uint32_t lba, uint32_t offset, void* buffer, uint32_t length, Mode mode) const {
  uint64_t start;
  if (!range(lba, offset, length, start) || (length && !buffer)) return -1;
  readRaw(start, buffer, length);
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

const char* VirtualDisk::layout() const {
  if (!available()) return "Config disk unavailable";
  uint8_t mbrSector[512], boot[512], root[96];
  readRaw(0, mbrSector, sizeof(mbrSector));
  readRaw(BootDisk::kPartitionStart * 512, boot, sizeof(boot));
  const uint8_t* mbr = mbrSector + 446;
  if (mbrSector[510] != 0x55 || mbrSector[511] != 0xAA || mbr[4] != 0x0E ||
      get32(mbr + 8) != BootDisk::kPartitionStart || get32(mbr + 12) != BootDisk::kVolumeSectors ||
      get16(boot + 11) != 512 || boot[13] != 1 || get16(boot + 14) != 1 ||
      boot[16] != 2 || get16(boot + 17) != 64 || get16(boot + 19) != BootDisk::kVolumeSectors ||
      get16(boot + 22) != BootDisk::kFatSectors || get32(boot + 39) != BootDisk::kSerial ||
      std::memcmp(boot + 43, "BOOTSWITCH ", 11) != 0)
    return "Disk layout changed";
  readRaw(kRootOffset, root, sizeof(root));
  if (std::memcmp(root, "BOOTSWITCH ", 11) || root[11] != 0x08 ||
      std::memcmp(root + 32, "MODE    TXT", 11) || !(root[32 + 11] & 1) ||
      get16(root + 32 + 26) != 2 ||
      get32(root + 32 + 28) != 8 || std::memcmp(root + 64, "SWITCH  CFG", 11) ||
      !(root[64 + 11] & 1) || get16(root + 64 + 26) != 3 || get32(root + 64 + 28) != 28)
    return "Boot files changed";
  for (unsigned copy = 0; copy < 2; ++copy) {
    const size_t fat = kFatOffset + copy * BootDisk::kFatSectors * 512;
    if (fatEntry(fat + 4) < 0xFFF8 || fatEntry(fat + 6) < 0xFFF8)
      return "Boot file chains changed";
  }
  return nullptr;
}

const char* VirtualDisk::configuration(char* output, size_t capacity, size_t& length) const {
  if (!output) return "Config disk unavailable";
  if (const char* error = layout()) return error;
  uint8_t root[64 * 32];
  readRaw(kRootOffset, root, sizeof(root));
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
    readRaw(kDataOffset + (cluster - 2) * 512, output + size - remaining, bytes);
    remaining -= bytes;
    const uint16_t next = fatEntry(kFatOffset + cluster * 2);
    if (next != fatEntry(kFatOffset + BootDisk::kFatSectors * 512 + cluster * 2))
      return "FAT copies differ";
    if (remaining && next >= 0xFFF8) return "Config file incomplete";
    if (!remaining && next < 0xFFF8) return "Invalid config chain";
    cluster = next;
  }
  output[size] = 0;
  length = size;
  return nullptr;
}

#if BOOTSWITCH_ENABLE_USB_UPDATE
size_t VirtualDisk::firmwareCapacity() const {
  // Leave room for configuration, both FATs, root entries and partial sectors.
  const size_t pool = sectorMap_ ? capacity_ : kCapacity;
  const size_t volume = (BootDisk::kVolumeSectors - BootDisk::kDataSector - 10) * 512;
  return std::min(pool > 32768 ? pool - 32768 : 0, volume);
}

bool VirtualDisk::prepareFirmware(size_t length) {
  if (!length || length > firmwareCapacity() || layout()) return false;
  downloadSize_ = 0;
  // CONFIG.INI occupies clusters 4..11 at most; boot files use 2 and 3.
  const size_t count = (length + 511) / 512;
  for (size_t i = 0; i < count; ++i) {
    uint8_t next[2];
    put16(next, i + 1 == count ? 0xFFFF : 13 + i);
    for (unsigned copy = 0; copy < 2; ++copy)
      if (write(0, kFatOffset + copy * BootDisk::kFatSectors * 512 + (12 + i) * 2,
                next, sizeof(next)) != sizeof(next)) return false;
  }
  uint8_t entry[32] = {};
  std::memcpy(entry, "UPDATE  BIN", 11);
  entry[11] = 0x20;
  put16(entry + 26, 12);
  put32(entry + 28, length);
  if (write(0, kRootOffset + 128, entry, sizeof(entry)) != sizeof(entry)) return false;
  downloadSize_ = length;
  return true;
}

bool VirtualDisk::writeFirmware(size_t offset, const void* bytes, size_t length) {
  if (!downloadSize_ || offset > downloadSize_ || length > downloadSize_ - offset) return false;
  return write(0, kDataOffset + 10 * 512 + offset, bytes, length) == int32_t(length);
}

const char* VirtualDisk::firmware(File& output) const {
  output = File{};
  if (const char* error = layout()) return error;
  const uint8_t* file = nullptr;
  uint8_t root[64 * 32];
  readRaw(kRootOffset, root, sizeof(root));
  for (unsigned index = 0; index < 64; ++index) {
    const uint8_t* entry = root + index * 32;
    if (entry[0] == 0) break;
    if (entry[0] == 0xE5 || entry[11] == 0x0F) continue;
    if (std::memcmp(entry, "UPDATE  BIN", 11) != 0) continue;
    if (entry[11] & 0x18) return "Firmware is not a file";
    if (file) return "Duplicate UPDATE.BIN";
    file = entry;
  }
  if (!file) return nullptr;
  const uint32_t size = get32(file + 28);
  if (!size || size > kClusterCount * 512u) return "Invalid firmware size";
  if (get16(file + 20) != 0) return "Invalid firmware cluster";
  uint16_t cluster = get16(file + 26);
  // Validate the complete chain before exposing any bytes to the flash writer.
  uint8_t visited[(kClusterCount + 7) / 8] = {};
  uint32_t remaining = size;
  while (remaining) {
    if (cluster < 4 || cluster >= kClusterCount + 2) return "Invalid firmware cluster";
    const unsigned index = cluster - 2;
    if (visited[index / 8] & (1u << (index % 8))) return "Firmware cluster loop";
    visited[index / 8] |= 1u << (index % 8);
    remaining -= std::min(remaining, 512u);
    const uint16_t next = fatEntry(kFatOffset + cluster * 2);
    if (next != fatEntry(kFatOffset + BootDisk::kFatSectors * 512 + cluster * 2))
      return "FAT copies differ";
    if (remaining && next >= 0xFFF8) return "Firmware file incomplete";
    if (!remaining && next < 0xFFF8) return "Invalid firmware chain";
    cluster = next;
  }
  output.disk_ = this;
  output.size_ = output.remaining_ = size;
  output.cluster_ = get16(file + 26);
  return nullptr;
}

size_t VirtualDisk::File::read(void* output, size_t capacity) {
  if (!disk_ || !output) return 0;
  const size_t length = std::min(capacity, size_t(remaining_));
  size_t copied = 0;
  while (copied < length) {
    const size_t count = std::min(length - copied, size_t(512 - offset_));
    disk_->readRaw(kDataOffset + (cluster_ - 2) * 512 + offset_,
                   static_cast<uint8_t*>(output) + copied, count);
    copied += count;
    remaining_ -= count;
    offset_ += count;
    if (offset_ == 512 && remaining_) {
      cluster_ = disk_->fatEntry(kFatOffset + cluster_ * 2);
      offset_ = 0;
    }
  }
  return copied;
}
#endif
}  // namespace bootswitch
