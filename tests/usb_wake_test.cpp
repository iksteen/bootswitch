#include "usb_wake.h"
#include <Arduino.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include "tusb.h"

#include <cassert>
#include <iostream>

using namespace bootswitch;
namespace {
bool mounted = false, suspended = false, allowed = false, keyboardStarted = false;
bool attributesWritable = true;
uint8_t attributes = 0x40;
unsigned signals = 0, waited = 0;
enum class DuringWait { Nothing, Resume, Disconnect };
DuringWait duringWait = DuringWait::Nothing;
esp_event_handler_t handler = nullptr;

void event(int32_t id, bool permission = false) {
  arduino_usb_event_data_t data{};
  data.suspend.remote_wakeup_en = permission;
  handler(nullptr, nullptr, id, &data);
}

void reset() {
  mounted = suspended = allowed = false;
  signals = waited = 0;
  duringWait = DuringWait::Nothing;
  event(ARDUINO_USB_RESUME_EVENT);
}
}  // namespace

ESPUSB USB;
void ESPUSB::onEvent(esp_event_handler_t callback) { handler = callback; }
bool ESPUSB::usbAttributes(uint8_t value) {
  if (!attributesWritable) return false;
  attributes = value;
  return true;
}
uint8_t ESPUSB::usbAttributes() { return attributes; }
void USBHIDKeyboard::begin() { keyboardStarted = true; }
bool tud_mounted() { return mounted; }
bool tud_suspended() { return suspended; }
bool tud_remote_wakeup() {
  ++signals;
  return mounted && suspended && allowed;
}
void delay(uint32_t milliseconds) {
  waited += milliseconds;
  if (duringWait == DuringWait::Resume) suspended = false;
  if (duringWait == DuringWait::Disconnect) mounted = false;
}

int main() {
  assert(prepareUsbWake());
  assert(keyboardStarted && handler);
  assert(attributes == 0x60);  // Preserve self-powered and add remote wake.
  reset();
  suspended = true;
  auto status = usbWakeStatus();
  assert(!status.configured && !status.suspended && !status.remoteWakeEnabled);
  assert(requestUsbWake() == UsbWakeResult::NotConfigured);
  assert(signals == 0 && waited == 0);

  reset();
  mounted = true;
  assert(requestUsbWake() == UsbWakeResult::NotSuspended);
  assert(signals == 0 && waited == 0);

  reset();
  mounted = suspended = true;
  event(ARDUINO_USB_SUSPEND_EVENT, true);
  status = usbWakeStatus();
  assert(status.configured && status.suspended && status.remoteWakeEnabled);
  // Stale diagnostic permission must not bypass the core's live feature bit.
  assert(requestUsbWake() == UsbWakeResult::NotEnabled);
  assert(signals == 1 && waited >= 5);

  reset();
  mounted = suspended = allowed = true;
  // A delayed diagnostic event must not prevent an actually armed request.
  assert(!usbWakeStatus().remoteWakeEnabled);
  assert(requestUsbWake() == UsbWakeResult::Requested);
  assert(signals == 1 && waited >= 5);

  for (const auto change : {DuringWait::Resume, DuringWait::Disconnect}) {
    reset();
    mounted = suspended = allowed = true;
    duringWait = change;
    assert(requestUsbWake() == (change == DuringWait::Resume ?
        UsbWakeResult::NotSuspended : UsbWakeResult::NotConfigured));
    assert(signals == 0 && waited >= 5);
  }
  reset();
  mounted = suspended = true;
  for (const int32_t id : {ARDUINO_USB_STARTED_EVENT, ARDUINO_USB_STOPPED_EVENT,
                           ARDUINO_USB_RESUME_EVENT}) {
    event(ARDUINO_USB_SUSPEND_EVENT, true);
    assert(usbWakeStatus().remoteWakeEnabled);
    event(id);
    assert(!usbWakeStatus().remoteWakeEnabled);
  }
  attributesWritable = false;
  assert(!prepareUsbWake());
  std::cout << "USB wake gating, idle timing, host permission, and resume/disconnect races passed.\n";
}
