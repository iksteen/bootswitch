#include "firmware_download.h"
#if BOOTSWITCH_ENABLE_USB_UPDATE
#include <algorithm>
#include <memory>
#include <new>
namespace bootswitch {
const char* receiveFirmware(FirmwareDownloadStream& stream, VirtualDisk& disk,
                            size_t maxSize, void (*progress)(size_t, size_t)) {
  const int64_t length = stream.size();
  if (length <= 80) return "Invalid download size";
  if (uint64_t(length) > std::min(maxSize, disk.firmwareCapacity())) return "Firmware too large";
  std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[4096]);
  if (!buffer) return "Download buffer unavailable";
  if (!disk.prepareFirmware(length)) return "Download disk unavailable";
  const uint32_t started = stream.now();
  uint32_t lastData = started;
  size_t received = 0;
  if (progress) progress(0, length);
  while (received < size_t(length)) {
    if (uint32_t(stream.now() - started) >= 180000 ||
        uint32_t(stream.now() - lastData) >= 15000) return "Firmware download timed out";
    const size_t capacity = std::min(size_t(4096), size_t(length) - received);
    const int count = stream.read(buffer.get(), capacity);
    if (count < 0 || size_t(count) > capacity) return "Firmware download failed";
    if (count) {
      if (!disk.writeFirmware(received, buffer.get(), count)) return "Download storage full";
      received += count;
      lastData = stream.now();
      if (progress) progress(received, length);
    } else if (!stream.connected()) return "Incomplete firmware download";
    stream.idle();
  }
  return nullptr;
}
}  // namespace bootswitch
#endif
