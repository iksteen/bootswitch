#include "display_telemetry.h"

#include <cassert>
#include <cstring>
#include <iostream>

int main() {
  bootswitch::DisplayTelemetry telemetry;
  unsigned reads = 0;
  auto sample = [&](uint32_t now, bool awake) {
    if (telemetry.pollDue(now, awake)) {
      ++reads;
      telemetry.update(true, true, -72, 84);
    }
  };
  sample(0, true);
  for (uint32_t now = 5; now < 1000; now += 5) sample(now, true);
  assert(reads == 1);
  sample(1000, true);
  assert(reads == 2);
  // Ordinary screen redraws use cached data without bringing a sample forward.
  assert(std::strcmp(telemetry.text(), "WiFi:-72dBm Bat:84%") == 0);
  assert(!telemetry.update(true, true, -72, 84));
  sample(1005, true);
  assert(reads == 2);

  for (uint32_t now = 1010; now < 10000; now += 5) sample(now, false);
  assert(reads == 2);  // Neither radio nor battery reads while the screen is off.
  sample(10000, true);
  assert(reads == 3);  // Fresh values immediately on wake, then at 1 Hz again.
  sample(10999, true);
  assert(reads == 3);
  sample(11000, true);
  assert(reads == 4);

  telemetry.invalidate();
  assert(telemetry.pollDue(UINT32_MAX - 499u, true));
  assert(!telemetry.pollDue(499, true));
  assert(telemetry.pollDue(500, true));  // millis() rollover.

  assert(telemetry.update(false, false, 0, -1));
  assert(std::strcmp(telemetry.text(), "WiFi:off Bat:--") == 0);
  assert(telemetry.update(true, false, -72, 100));
  assert(std::strcmp(telemetry.text(), "WiFi:-- Bat:100%") == 0);
  assert(telemetry.update(true, true, 0, 0));
  assert(std::strcmp(telemetry.text(), "WiFi:-- Bat:0%") == 0);
  assert(telemetry.update(true, true, -100, 101));
  assert(std::strcmp(telemetry.text(), "WiFi:-100dBm Bat:--") == 0);
  std::cout << "Display telemetry: 1 Hz while awake, no reads asleep, immediate wake, rollover, and unavailable readings passed.\n";
}
