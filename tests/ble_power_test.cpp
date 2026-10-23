#include "ble_power.h"
#include <esp_bt.h>
#include <cassert>
#include <cstring>
#include <iostream>

extern "C" esp_err_t __wrap_esp_bt_controller_init(esp_bt_controller_config_t*);
static esp_bt_controller_config_t observed{};
static esp_err_t result = ESP_OK;
static unsigned calls = 0;

extern "C" esp_err_t __real_esp_bt_controller_init(esp_bt_controller_config_t* config) {
  ++calls;
  if (!config) return -2;
  observed = *config;
  return result;
}

int main() {
  assert(!bootswitch::bleModemSleepConfigured());
  esp_bt_controller_config_t original{0x5A5A, 4096, 3, ESP_BT_SLEEP_MODE_NONE, 0};
  const auto saved = original;
  assert(__wrap_esp_bt_controller_init(&original) == ESP_OK);
  assert(bootswitch::bleModemSleepConfigured());
  assert(std::memcmp(&original, &saved, sizeof(original)) == 0);
  assert(observed.magic == original.magic && observed.controller_task_stack_size == 4096 && observed.ble_max_act == 3);
  assert(observed.sleep_mode == ESP_BT_SLEEP_MODE_1 && observed.sleep_clock == ESP_BT_SLEEP_CLOCK_MAIN_XTAL);
  // Respect an explicit caller-provided low-power clock, and propagate failures.
  original.sleep_mode = ESP_BT_SLEEP_MODE_1;
  original.sleep_clock = ESP_BT_SLEEP_CLOCK_EXT_32K_XTAL;
  assert(__wrap_esp_bt_controller_init(&original) == ESP_OK);
  assert(observed.sleep_clock == ESP_BT_SLEEP_CLOCK_EXT_32K_XTAL);
  result = -7;
  assert(__wrap_esp_bt_controller_init(&original) == -7);
  assert(!bootswitch::bleModemSleepConfigured());
  assert(__wrap_esp_bt_controller_init(nullptr) == -2);
  assert(calls == 4);
  std::cout << "BLE modem sleep: enable defaults, preserve caller config, propagate failures and null input passed.\n";
}
