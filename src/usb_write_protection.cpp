#include <cstdint>

// Define separately from TinyUSB's weak declaration so this is a strong symbol.
// Report write protection in MODE SENSE as well as rejecting WRITE10 requests.
extern "C" bool tud_msc_is_writable_cb(uint8_t) { return false; }
