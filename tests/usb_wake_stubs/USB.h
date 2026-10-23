#pragma once
#include <cstdint>
using esp_event_base_t = const char*;
using esp_event_handler_t = void (*)(void*, esp_event_base_t, int32_t, void*);
enum { ARDUINO_USB_STARTED_EVENT, ARDUINO_USB_STOPPED_EVENT,
       ARDUINO_USB_SUSPEND_EVENT, ARDUINO_USB_RESUME_EVENT };
union arduino_usb_event_data_t {
  struct { bool remote_wakeup_en; } suspend;
};
class ESPUSB {
 public:
  void onEvent(esp_event_handler_t callback);
  bool usbAttributes(uint8_t attributes);
  uint8_t usbAttributes();
};
extern ESPUSB USB;
