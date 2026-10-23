#include "usb_wake.h"

#include <Arduino.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <atomic>
#include "tusb.h"

namespace bootswitch {
namespace {
USBHIDKeyboard wakeKeyboard;
std::atomic<bool> remoteWakeEnabled{false};

void usbEvent(void*, esp_event_base_t, int32_t eventId, void* data) {
  if (eventId == ARDUINO_USB_SUSPEND_EVENT && data) {
    remoteWakeEnabled.store(static_cast<arduino_usb_event_data_t*>(data)->suspend.remote_wakeup_en);
  } else if (eventId == ARDUINO_USB_STARTED_EVENT || eventId == ARDUINO_USB_STOPPED_EVENT ||
             eventId == ARDUINO_USB_RESUME_EVENT) {
    remoteWakeEnabled.store(false);
  }
}
}  // namespace

bool prepareUsbWake() {
  wakeKeyboard.begin();
  USB.onEvent(usbEvent);
  return USB.usbAttributes(USB.usbAttributes() | TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP);
}

UsbWakeStatus usbWakeStatus() {
  const bool configured = tud_mounted();
  const bool suspended = configured && tud_suspended();
  // The core reports host permission in its suspend event. While the bus is
  // active this flag does not claim to predict permission for a future suspend.
  return {configured, suspended, suspended && remoteWakeEnabled.load()};
}

UsbWakeResult requestUsbWake() {
  if (!tud_mounted()) return UsbWakeResult::NotConfigured;
  if (!tud_suspended()) return UsbWakeResult::NotSuspended;
  // The device must observe at least 5 ms of bus idle before remote resume.
  // Yield for 10 ms after seeing suspend, then recheck in case the host resumed.
  delay(10);
  if (!tud_mounted()) return UsbWakeResult::NotConfigured;
  if (!tud_suspended()) return UsbWakeResult::NotSuspended;
  // TinyUSB checks the host's actual remote-wakeup feature bit. An event-task
  // snapshot is only used for diagnostics, never to authorize signaling.
  if (tud_remote_wakeup()) return UsbWakeResult::Requested;
  if (!tud_mounted()) return UsbWakeResult::NotConfigured;
  if (!tud_suspended()) return UsbWakeResult::NotSuspended;
  return UsbWakeResult::NotEnabled;
}
}  // namespace bootswitch
