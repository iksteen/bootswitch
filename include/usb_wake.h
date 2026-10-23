#pragma once

namespace bootswitch {
struct UsbWakeStatus {
  bool configured;
  bool suspended;
  bool remoteWakeEnabled;
};

enum class UsbWakeResult { Requested, NotConfigured, NotSuspended, NotEnabled };

// Call before USB.begin(). Adds a passive HID keyboard interface so host OSes
// can arm wake permission. Wake requests use USB signaling, with no key reports.
bool prepareUsbWake();
UsbWakeStatus usbWakeStatus();
UsbWakeResult requestUsbWake();
}  // namespace bootswitch
