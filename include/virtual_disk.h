#pragma once

#include "boot_disk.h"
#include "build_features.h"

namespace bootswitch {
// RAM-backed FAT volume: normal editor writes, including temporary files and
// rename replacement, are preserved for this USB session. Only validated
// CONFIG.INI settings and optional FIRMWARE.BIN are applied after safe eject.
class VirtualDisk {
 public:
  static constexpr size_t kCapacity = BootDisk::kSectorCount * BootDisk::kSectorSize;
  static constexpr size_t kMaxConfigSize = 4096;

#if BOOTSWITCH_ENABLE_USB_UPDATE
  // A validated, sequential view of a possibly fragmented firmware file.
  // Its backing disk must remain unchanged for the lifetime of the reader.
  class File {
   public:
    explicit operator bool() const { return bytes_ != nullptr; }
    size_t size() const { return size_; }
    size_t read(void* output, size_t capacity);

   private:
    friend class VirtualDisk;
    const uint8_t* bytes_ = nullptr;
    uint32_t size_ = 0;
    uint32_t remaining_ = 0;
    uint16_t cluster_ = 0;
    uint16_t offset_ = 0;
  };
#endif

  VirtualDisk(uint8_t* bytes, size_t capacity) : bytes_(bytes), capacity_(capacity) {}
  bool format(const char* configuration, size_t length);
  int32_t read(uint32_t lba, uint32_t offset, void* buffer, uint32_t length, Mode mode) const;
  int32_t write(uint32_t lba, uint32_t offset, const void* buffer, uint32_t length);
  // Errors contain no file contents. The application must hold its disk mutex.
  const char* configuration(char* output, size_t capacity, size_t& length) const;
#if BOOTSWITCH_ENABLE_USB_UPDATE
  // Missing firmware is normal (an empty File); malformed files are errors.
  const char* firmware(File& output) const;
#endif

 private:
  bool range(uint32_t lba, uint32_t offset, uint32_t length, uint64_t& start) const;
  const char* layout() const;
  uint8_t* bytes_;
  size_t capacity_;
};
}  // namespace bootswitch
