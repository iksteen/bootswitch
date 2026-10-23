#include "firmware_update.h"

#if BOOTSWITCH_ENABLE_USB_UPDATE
#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <esp_app_format.h>
#include <esp_image_format.h>

namespace bootswitch {
const char* stageFirmware(VirtualDisk::File file, const esp_partition_t*& staged,
                          void (*progress)(size_t, size_t)) {
  staged = nullptr;
  if (!file) return nullptr;
  const auto* running = esp_ota_get_running_partition();
  const auto* target = esp_ota_get_next_update_partition(nullptr);
  if (!running || !target || running->address == target->address)
    return "No inactive firmware slot";
  if (file.size() > target->size) return "Firmware too large";
  std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[4096]);
  if (!buffer) return "Firmware buffer unavailable";
  constexpr size_t descriptorOffset = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
  auto probe = file;
  if (probe.read(buffer.get(), descriptorOffset + sizeof(uint32_t)) != descriptorOffset + sizeof(uint32_t))
    return "Invalid application image";
  esp_image_header_t header;
  uint32_t descriptorMagic;
  std::memcpy(&header, buffer.get(), sizeof(header));
  std::memcpy(&descriptorMagic, buffer.get() + descriptorOffset, sizeof(descriptorMagic));
  if (header.magic != ESP_IMAGE_HEADER_MAGIC || header.chip_id != ESP_CHIP_ID_ESP32S3 ||
      descriptorMagic != ESP_APP_DESC_MAGIC_WORD) return "Invalid application image";
  esp_ota_handle_t handle;
  if (esp_ota_begin(target, file.size(), &handle) != ESP_OK)
    return "Firmware update begin failed";
  size_t written = 0;
  while (written < file.size()) {
    const size_t count = std::min(size_t(4096), file.size() - written);
    if (file.read(buffer.get(), count) != count) {
      esp_ota_abort(handle);
      return "Firmware read failed";
    }
    if (esp_ota_write(handle, buffer.get(), count) != ESP_OK) {
      esp_ota_abort(handle);
      return "Firmware write failed";
    }
    written += count;
    if (progress) progress(written, file.size());
  }
  // ESP-IDF verifies the image format, chip compatibility, checksum and any
  // embedded SHA-256 digest. This consumes the handle on both success/failure.
  if (esp_ota_end(handle) != ESP_OK) return "Invalid firmware image";
  // ota_end validates against the entire partition. Bound a second check to
  // this file so old bytes beyond a truncated upload cannot complete the image.
  const esp_partition_pos_t position{target->address, static_cast<uint32_t>(file.size())};
  esp_image_metadata_t metadata{};
  if (esp_image_verify(ESP_IMAGE_VERIFY, &position, &metadata) != ESP_OK ||
      metadata.image_len != file.size()) return "Firmware file size mismatch";
  staged = target;
  return nullptr;
}
}  // namespace bootswitch
#endif
