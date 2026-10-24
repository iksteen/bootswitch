#include "firmware_target.h"
struct FirmwareTargetDescriptor {
  uint8_t magic[8];
  uint32_t target;
};
extern "C" {
extern const FirmwareTargetDescriptor bootswitch_board_descriptor
    __attribute__((section(".rodata_custom_desc"), used)) = {
      {'B','S','W','B','R','D','0','1'}, bootswitch::kFirmwareTargetId
    };
}
