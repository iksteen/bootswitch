#pragma once
#include <cstdint>
#define CONFIG_IDF_TARGET_ESP32S3 1
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr uint8_t ESP_BT_SLEEP_MODE_NONE = 0;
constexpr uint8_t ESP_BT_SLEEP_MODE_1 = 1;
constexpr uint8_t ESP_BT_SLEEP_CLOCK_MAIN_XTAL = 1;
constexpr uint8_t ESP_BT_SLEEP_CLOCK_EXT_32K_XTAL = 2;
struct esp_bt_controller_config_t {
  uint32_t magic;
  uint16_t controller_task_stack_size;
  uint8_t ble_max_act;
  uint8_t sleep_mode;
  uint8_t sleep_clock;
};
