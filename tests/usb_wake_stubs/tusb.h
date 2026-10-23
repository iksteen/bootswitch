#pragma once
constexpr unsigned TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP = 0x20;
bool tud_mounted();
bool tud_suspended();
bool tud_remote_wakeup();
