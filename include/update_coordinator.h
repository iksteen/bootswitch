#pragma once

#include <atomic>
#include "settings.h"
#include "virtual_disk.h"
#include "firmware_update.h"

namespace bootswitch {
// Device I/O only. The coordinator owns the write gate and apply/recovery order.
class UpdateBackend {
 public:
  virtual ~UpdateBackend() = default;
  virtual void lockDisk() = 0;
  virtual void unlockDisk() = 0;
  virtual void mediaPresent(bool present) = 0;
  virtual bool settingsAvailable() const = 0;
  virtual bool loadSavedSettings(Settings& saved) = 0;
  virtual bool saveSettings(const char* text, size_t length) = 0;
  virtual void beginRecovery() = 0;
  virtual void endRecovery() = 0;
  virtual int32_t writeDisk(VirtualDisk& disk, uint32_t lba, uint32_t offset,
                            const void* bytes, uint32_t length) {
    return disk.write(lba, offset, bytes, length);
  }
  virtual bool rebuildDisk(VirtualDisk& disk, const char* text, size_t length) {
    return disk.format(text, length);
  }
#if BOOTSWITCH_ENABLE_USB_UPDATE
  virtual const char* downloadFirmware(VirtualDisk&) { return "Network updates unavailable"; }
  virtual const char* stage(VirtualDisk::File file, const esp_partition_t*& staged,
                            bool requireSignature = false) = 0;
  virtual bool activate(const esp_partition_t* staged) = 0;
#endif
};

struct ApplyResult {
  const char* error = nullptr;
  bool firmware = false;
  bool restart = false;
};

class UpdateCoordinator {
 public:
  UpdateCoordinator(VirtualDisk& disk, UpdateBackend& backend, char* text, size_t capacity)
      : disk_(disk), backend_(backend), text_(text), capacity_(capacity) {}
  bool writable() const { return state_.load() == State::Writable; }
  bool applyPending() const {
    const auto state = state_.load();
    return state == State::Pending || state == State::DownloadPending;
  }
  int32_t read(uint32_t lba, uint32_t offset, void* bytes, uint32_t length, Mode mode);
  int32_t write(uint32_t lba, uint32_t offset, const void* bytes, uint32_t length);
  // USB callback/button: freeze writes and finish hiding the medium before
  // publishing work to the main loop. Duplicate requests are coalesced.
  bool requestApply();
#if BOOTSWITCH_ENABLE_USB_UPDATE
  bool requestDownload();
#endif
  // Main loop only. Returns false when no completed request is pending.
  bool processApply(ApplyResult& result);

 private:
  enum class State { Writable, Ejecting, Pending, DownloadPending, Applying, Recovering, Restarting, Unavailable };
  bool recover();
  VirtualDisk& disk_;
  UpdateBackend& backend_;
  char* text_;
  size_t capacity_;
  std::atomic<State> state_{State::Writable};
};
}  // namespace bootswitch
