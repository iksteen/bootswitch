#include "ble_power.h"

#include <atomic>
#include <esp_bt.h>
#include <esp_idf_version.h>

#if !defined(CONFIG_IDF_TARGET_ESP32S3) || ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(4, 4, 7)
#error "Review the Bluetooth power adapter when changing the target or ESP-IDF version"
#endif

namespace {
std::atomic<bool> modemSleepConfigured{false};
}

namespace bootswitch {
bool bleModemSleepConfigured() { return modemSleepConfigured.load(); }
}

extern "C" esp_err_t __real_esp_bt_controller_init(esp_bt_controller_config_t* config);

// NimBLE uses the Arduino SDK's default controller configuration, which disables
// modem sleep. Adapt the public initialization parameters without patching the
// library or entering CPU light sleep (native USB must remain active).
extern "C" esp_err_t __wrap_esp_bt_controller_init(esp_bt_controller_config_t* config) {
  modemSleepConfigured.store(false);
  if (!config) return __real_esp_bt_controller_init(config);
  auto powerConfig = *config;
  if (powerConfig.sleep_mode == ESP_BT_SLEEP_MODE_NONE) {
    powerConfig.sleep_mode = ESP_BT_SLEEP_MODE_1;
    powerConfig.sleep_clock = ESP_BT_SLEEP_CLOCK_MAIN_XTAL;
  }
  const esp_err_t result = __real_esp_bt_controller_init(&powerConfig);
  modemSleepConfigured.store(result == ESP_OK && powerConfig.sleep_mode == ESP_BT_SLEEP_MODE_1);
  return result;
}
