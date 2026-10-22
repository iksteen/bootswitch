#pragma once

#include <cstddef>
#include <cstdint>

namespace bootswitch {
enum class Mode : uint8_t { Linux = 0, Windows = 1 };

// Generated base image of a 4 MiB MBR disk. The writable version lives in PSRAM;
// boot selection is overlaid on reads, independent of host filesystem caching.
class BootDisk {
 public:
  static constexpr uint32_t kSectorSize = 512;
  static constexpr uint32_t kSectorCount = 8192;
  static constexpr uint32_t kPartitionStart = 2048;
  static constexpr uint32_t kVolumeSectors = kSectorCount - kPartitionStart;
  static constexpr uint32_t kFatSectors = 24;
  static constexpr uint32_t kRootSector = 1 + 2 * kFatSectors;
  static constexpr uint32_t kDataSector = kRootSector + 4;
  static constexpr uint32_t kSerial = 0xB0075E1E;

  // Supports partial sectors and transfers spanning sectors. The caller takes
  // one mode snapshot per callback so a concurrent toggle cannot tear a read.
  static int32_t read(uint32_t lba, uint32_t offset, void* buffer,
                      uint32_t length, Mode mode);
  static void sector(uint32_t lba, uint8_t* buffer, Mode mode);
};
}  // namespace bootswitch
