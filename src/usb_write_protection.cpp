#include <cstdint>

// Define separately from TinyUSB's weak declaration so this is a strong symbol.
// USB advertises the RAM configuration disk as writable. If PSRAM allocation
// fails, the application falls back to the original read-only boot selector.
extern bool bootSwitchDiskWritable();
extern "C" bool tud_msc_is_writable_cb(uint8_t) { return bootSwitchDiskWritable(); }
