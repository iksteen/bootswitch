#include "display_telemetry.h"

#include <cassert>
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
  assert(telemetry.values().wifiEnabled && telemetry.values().wifiConnected);
  assert(telemetry.values().rssi == -72 && telemetry.values().batteryLevel == 84);
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
  assert(!telemetry.values().wifiEnabled && !telemetry.values().wifiConnected);
  assert(telemetry.values().rssi == 0 && telemetry.values().batteryLevel == -1);
  assert(telemetry.update(true, false, -72, 100));
  assert(telemetry.values().wifiEnabled && !telemetry.values().wifiConnected);
  assert(telemetry.values().rssi == 0 && telemetry.values().batteryLevel == 100);
  assert(telemetry.update(true, true, 0, 0));
  assert(telemetry.values().rssi == 0 && telemetry.values().batteryLevel == 0);
  assert(telemetry.update(true, true, -100, 101));
  assert(telemetry.values().rssi == -100 && telemetry.values().batteryLevel == -1);
  // Unavailable readings normalize to one value rather than causing redraws
  // when only the hardware's invalid sentinel changes.
  assert(!telemetry.update(true, true, -100, -2));
  assert(telemetry.update(false, false, -40, 101));
  assert(!telemetry.update(false, true, -99, -1));
  assert(telemetry.update(true, true, -52, 100, false));
  assert(!telemetry.values().batterySupported && telemetry.values().batteryLevel == -1);
  assert(!telemetry.update(true, true, -52, 73, false));
  assert(telemetry.update(true, true, -52, -1, true));
  assert(telemetry.values().batterySupported && telemetry.values().batteryLevel == -1);
  std::cout << "Display telemetry: 1 Hz while awake, no reads asleep, immediate wake, rollover, and unavailable readings passed.\n";
}
