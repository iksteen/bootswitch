#pragma once
// The wake transport has no reason to send keyboard input reports.
class USBHIDKeyboard {
 public:
  void begin();
};
