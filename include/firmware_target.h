#pragma once
#include <cstddef>
#include <cstdint>
#include "build_features.h"
namespace bootswitch {
// ESP application header (24), first segment header (8), app descriptor (256).
constexpr size_t kFirmwareTargetOffset = 288;
constexpr uint32_t kFirmwareTargetId = BOOTSWITCH_BOARD_TQT ? 1 : 0;
// Versioned custom descriptor. Absent descriptors identify legacy StickS3 images.
constexpr uint8_t kFirmwareTargetMagic[8] = {'B','S','W','B','R','D','0','1'};
}
