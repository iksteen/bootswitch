#pragma once

#include <cstddef>
#include <cstdint>

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
using esp_ota_handle_t = unsigned;
struct esp_partition_t { uint32_t address; size_t size; };
const esp_partition_t* esp_ota_get_running_partition();
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*);
esp_err_t esp_ota_begin(const esp_partition_t*, size_t, esp_ota_handle_t*);
esp_err_t esp_ota_write(esp_ota_handle_t, const void*, size_t);
esp_err_t esp_ota_end(esp_ota_handle_t);
esp_err_t esp_ota_abort(esp_ota_handle_t);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t*);
