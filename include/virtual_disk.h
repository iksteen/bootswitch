#pragma once

#include "boot_disk.h"

namespace bootswitch {
// RAM-backed FAT volume: normal editor writes, including temporary files and
// rename replacement, are preserved for this USB session. Only validated
// CONFIG.INI settings are committed to NVS by the application after safe eject.
class VirtualDisk {
 public:
  static constexpr size_t kCapacity = BootDisk::kSectorCount * BootDisk::kSectorSize;
  static constexpr size_t kMaxConfigSize = 4096;

  VirtualDisk(uint8_t* bytes, size_t capacity) : bytes_(bytes), capacity_(capacity) {}
  bool format(const char* configuration, size_t length);
  int32_t read(uint32_t lba, uint32_t offset, void* buffer, uint32_t length, Mode mode) const;
  int32_t write(uint32_t lba, uint32_t offset, const void* buffer, uint32_t length);
  // Errors contain no file contents. The application must hold its disk mutex.
  const char* configuration(char* output, size_t capacity, size_t& length) const;

 private:
  bool range(uint32_t lba, uint32_t offset, uint32_t length, uint64_t& start) const;
  uint8_t* bytes_;
  size_t capacity_;
};
}  // namespace bootswitch
