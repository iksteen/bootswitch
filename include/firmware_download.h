#pragma once
#include "build_features.h"
#if BOOTSWITCH_ENABLE_USB_UPDATE
#include "virtual_disk.h"
namespace bootswitch {
// A successfully opened HTTP body. Reads are nonblocking: zero means wait,
// negative means failure. The transport owns TLS and HTTP validation.
class FirmwareDownloadStream {
 public:
  virtual ~FirmwareDownloadStream() = default;
  virtual int64_t size() const = 0;
  virtual int read(uint8_t* bytes, size_t capacity) = 0;
  virtual bool connected() = 0;
  virtual uint32_t now() = 0;
  virtual void idle() = 0;
};
const char* receiveFirmware(FirmwareDownloadStream& stream, VirtualDisk& disk,
                            size_t maxSize, void (*progress)(size_t, size_t) = nullptr);
const char* downloadLatestFirmware(VirtualDisk& disk,
                                   void (*progress)(size_t, size_t), void (*yield)());
}  // namespace bootswitch
#endif
