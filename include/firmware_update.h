#pragma once

#include "build_features.h"

#if BOOTSWITCH_ENABLE_USB_UPDATE
#include <esp_ota_ops.h>
#include "virtual_disk.h"

namespace bootswitch {
// Authenticate, then stage and validate the image in the inactive slot.
// Release builds require the embedded key's signature before any flash write.
// The disk must remain unchanged. Progress is called after every write chunk.
const char* stageFirmware(VirtualDisk::File file, const esp_partition_t*& staged,
                          void (*progress)(size_t, size_t) = nullptr,
                          void (*yield)() = nullptr);
}  // namespace bootswitch
#endif
