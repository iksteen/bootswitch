#pragma once

// Development-only firmware controls. Neither can be enabled through CONFIG.INI.
#ifndef BOOTSWITCH_ENABLE_REMOTE_FLASH
#define BOOTSWITCH_ENABLE_REMOTE_FLASH 0
#endif
#ifndef BOOTSWITCH_ENABLE_USB_UPDATE
#define BOOTSWITCH_ENABLE_USB_UPDATE 0
#endif

#if (BOOTSWITCH_ENABLE_REMOTE_FLASH != 0 && BOOTSWITCH_ENABLE_REMOTE_FLASH != 1) || \
    (BOOTSWITCH_ENABLE_USB_UPDATE != 0 && BOOTSWITCH_ENABLE_USB_UPDATE != 1)
#error "BootSwitch development feature flags must be 0 or 1"
#endif
