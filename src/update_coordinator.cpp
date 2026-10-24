#include "update_coordinator.h"

namespace bootswitch {
namespace {
class DiskLock {
 public:
  explicit DiskLock(UpdateBackend& backend) : backend_(backend) { backend_.lockDisk(); }
  ~DiskLock() { backend_.unlockDisk(); }
  DiskLock(const DiskLock&) = delete;
  DiskLock& operator=(const DiskLock&) = delete;
 private:
  UpdateBackend& backend_;
};
}

int32_t UpdateCoordinator::read(uint32_t lba, uint32_t offset, void* bytes, uint32_t length, Mode mode) {
  if (!writable()) return -1;
  DiskLock lock(backend_);
  if (!writable()) return -1;
  return disk_.read(lba, offset, bytes, length, mode);
}

int32_t UpdateCoordinator::write(uint32_t lba, uint32_t offset, const void* bytes, uint32_t length) {
  if (!writable()) return -1;
  DiskLock lock(backend_);
  if (!writable()) return -1;
  return backend_.writeDisk(disk_, lba, offset, bytes, length);
}

bool UpdateCoordinator::requestApply() {
  State expected = State::Writable;
  if (!state_.compare_exchange_strong(expected, State::Ejecting)) return false;
  backend_.mediaPresent(false);
  // A main-loop/button request must not overtake an incomplete USB callback.
  state_.store(State::Pending);
  return true;
}

#if BOOTSWITCH_ENABLE_USB_UPDATE
bool UpdateCoordinator::requestDownload() {
  State expected = State::Writable;
  if (!state_.compare_exchange_strong(expected, State::Ejecting)) return false;
  backend_.mediaPresent(false);
  state_.store(State::DownloadPending);
  return true;
}
#endif

bool UpdateCoordinator::recover() {
  state_.store(State::Recovering);
  backend_.beginRecovery();
  Settings saved;
  size_t length = 0;
  bool rebuilt = false;
  if (backend_.loadSavedSettings(saved) && formatSettings(saved, text_, capacity_, length)) {
    DiskLock lock(backend_);
    rebuilt = backend_.rebuildDisk(disk_, text_, length);
  }
  state_.store(rebuilt ? State::Writable : State::Unavailable);
  if (rebuilt) backend_.mediaPresent(true);
  backend_.endRecovery();
  return rebuilt;
}

bool UpdateCoordinator::processApply(ApplyResult& result) {
  State expected = State::Pending;
#if BOOTSWITCH_ENABLE_USB_UPDATE
  bool download = false;
#endif
  if (!state_.compare_exchange_strong(expected, State::Applying)) {
#if BOOTSWITCH_ENABLE_USB_UPDATE
    expected = State::DownloadPending;
    if (!state_.compare_exchange_strong(expected, State::Applying)) return false;
    download = true;
#else
    return false;
#endif
  }
  result = ApplyResult{};
  size_t length = 0;
#if BOOTSWITCH_ENABLE_USB_UPDATE
  VirtualDisk::File firmware;
  if (download) {
    // Network updates use persisted settings, never host edits awaiting eject.
    // Drain admitted I/O before recycling the disk's storage for the download.
    Settings saved;
    if (!backend_.settingsAvailable() || !backend_.loadSavedSettings(saved) ||
        !formatSettings(saved, text_, capacity_, length)) result.error = "Settings unavailable";
    if (!result.error) {
      DiskLock lock(backend_);
      if (!disk_.format(text_, length)) result.error = "Download disk unavailable";
    }
    if (!result.error) result.error = backend_.downloadFirmware(disk_);
  }
#endif
  {
    // Drain any write admitted before eject, including one already copying.
    DiskLock lock(backend_);
    if (!result.error) result.error = disk_.configuration(text_, capacity_, length);
#if BOOTSWITCH_ENABLE_USB_UPDATE
    if (!result.error) result.error = disk_.firmware(firmware);
#endif
  }
  Settings candidate;
  if (!result.error) result.error = parseSettings(text_, length, candidate);
  if (!result.error && !backend_.settingsAvailable()) result.error = "Settings unavailable";
#if BOOTSWITCH_ENABLE_USB_UPDATE
  const esp_partition_t* staged = nullptr;
  if (!result.error && download && !firmware) result.error = "Downloaded firmware missing";
  if (!result.error && firmware) result.error = backend_.stage(firmware, staged, download);
  result.firmware = staged != nullptr;
#endif
  if (!result.error) {
    if (!formatSettings(candidate, text_, capacity_, length) ||
        !backend_.saveSettings(text_, length)) result.error = "Settings save failed";
  }
#if BOOTSWITCH_ENABLE_USB_UPDATE
  if (!result.error && staged && !backend_.activate(staged)) result.error = "Firmware activation failed";
#endif
  if (result.error) {
    if (!recover()) result.error = "USB disk recovery failed";
  } else {
    state_.store(State::Restarting);
    result.restart = true;
  }
  return true;
}
}  // namespace bootswitch
