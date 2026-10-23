#pragma once

// Development-only control; cannot be enabled through CONFIG.INI.
#ifndef BOOTSWITCH_ENABLE_REMOTE_FLASH
#define BOOTSWITCH_ENABLE_REMOTE_FLASH 0
#endif

#if BOOTSWITCH_ENABLE_REMOTE_FLASH != 0 && BOOTSWITCH_ENABLE_REMOTE_FLASH != 1
#error "BootSwitch development feature flags must be 0 or 1"
#endif
