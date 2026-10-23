#pragma once
#include <cstdint>
constexpr uint8_t ESP_IMAGE_HEADER_MAGIC = 0xE9;
constexpr uint16_t ESP_CHIP_ID_ESP32S3 = 9;
constexpr uint32_t ESP_APP_DESC_MAGIC_WORD = 0xABCD5432;
struct __attribute__((packed)) esp_image_header_t {
  uint8_t magic;
  uint8_t reserved[11];
  uint16_t chip_id;
  uint8_t rest[10];
};
struct esp_image_segment_header_t { uint32_t load_addr; uint32_t data_len; };
