#include "firmware_update.h"

#if BOOTSWITCH_ENABLE_USB_UPDATE
#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <esp_app_format.h>
#include <esp_image_format.h>
#include <mbedtls/asn1write.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include "firmware_public_key.h"

namespace bootswitch {
namespace {
constexpr size_t kTrailerSize = 80;
constexpr uint8_t kSignatureMagic[] = {'B', 'S', 'W', 'U', 'P', 'D', '0', '1'};

uint32_t read32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

bool verifySignature(const uint8_t digest[32], const uint8_t signature[64]) {
  mbedtls_pk_context key;
  mbedtls_pk_init(&key);
  mbedtls_mpi r, s;
  mbedtls_mpi_init(&r);
  mbedtls_mpi_init(&s);
  // Convert the fixed-width r || s trailer to DER using the library's ASN.1
  // writer; signature arithmetic and validity checks stay inside mbedTLS.
  uint8_t der[72];
  uint8_t* cursor = der + sizeof(der);
  bool valid = false;
  if (mbedtls_pk_parse_public_key(&key, kFirmwarePublicKey, sizeof(kFirmwarePublicKey)) == 0 &&
      mbedtls_pk_can_do(&key, MBEDTLS_PK_ECDSA) &&
      mbedtls_mpi_read_binary(&r, signature, 32) == 0 &&
      mbedtls_mpi_read_binary(&s, signature + 32, 32) == 0 &&
      mbedtls_asn1_write_mpi(&cursor, der, &s) >= 0 &&
      mbedtls_asn1_write_mpi(&cursor, der, &r) >= 0) {
    const size_t integers = der + sizeof(der) - cursor;
    if (mbedtls_asn1_write_len(&cursor, der, integers) >= 0 &&
        mbedtls_asn1_write_tag(&cursor, der, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE) >= 0) {
      valid = mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, digest, 32,
                               cursor, der + sizeof(der) - cursor) == 0;
    }
  }
  mbedtls_mpi_free(&s);
  mbedtls_mpi_free(&r);
  mbedtls_pk_free(&key);
  return valid;
}

const char* authenticateFirmware(VirtualDisk::File file, size_t& imageSize,
                                 uint8_t* buffer, void (*yield)()) {
  imageSize = file.size();
  if (file.size() <= kTrailerSize)
    return BOOTSWITCH_ALLOW_UNSIGNED_FIRMWARE ? nullptr : "Firmware signature required";
  const size_t candidateSize = file.size() - kTrailerSize;
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  bool hashed = mbedtls_sha256_starts_ret(&hash, 0) == 0;
  size_t remaining = candidateSize;
  while (hashed && remaining) {
    const size_t count = std::min(size_t(4096), remaining);
    hashed = file.read(buffer, count) == count &&
             mbedtls_sha256_update_ret(&hash, buffer, count) == 0;
    remaining -= count;
    if (yield) yield();
  }
  uint8_t trailer[kTrailerSize];
  if (!hashed || file.read(trailer, sizeof(trailer)) != sizeof(trailer)) {
    mbedtls_sha256_free(&hash);
    return "Firmware signature read failed";
  }
  if (std::memcmp(trailer, kSignatureMagic, sizeof(kSignatureMagic)) != 0) {
    mbedtls_sha256_free(&hash);
    return BOOTSWITCH_ALLOW_UNSIGNED_FIRMWARE ? nullptr : "Firmware signature required";
  }
  if (read32(trailer + 8) != candidateSize || read32(trailer + 12) != 0) {
    mbedtls_sha256_free(&hash);
    return "Invalid signature metadata";
  }
  uint8_t digest[32];
  hashed = mbedtls_sha256_update_ret(&hash, trailer, 16) == 0 &&
           mbedtls_sha256_finish_ret(&hash, digest) == 0;
  mbedtls_sha256_free(&hash);
  if (!hashed || !verifySignature(digest, trailer + 16)) return "Invalid firmware signature";
  imageSize = candidateSize;
  return nullptr;
}
}  // namespace

const char* stageFirmware(VirtualDisk::File file, const esp_partition_t*& staged,
                          void (*progress)(size_t, size_t), void (*yield)()) {
  staged = nullptr;
  if (!file) return nullptr;
  const auto* running = esp_ota_get_running_partition();
  const auto* target = esp_ota_get_next_update_partition(nullptr);
  if (!running || !target || running->address == target->address)
    return "No inactive firmware slot";
  if (file.size() > target->size + kTrailerSize) return "Firmware too large";
  std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[4096]);
  if (!buffer) return "Firmware buffer unavailable";
  size_t imageSize = 0;
  if (const char* error = authenticateFirmware(file, imageSize, buffer.get(), yield)) return error;
  if (imageSize > target->size) return "Firmware too large";
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
  if (esp_ota_begin(target, imageSize, &handle) != ESP_OK)
    return "Firmware update begin failed";
  size_t written = 0;
  while (written < imageSize) {
    const size_t count = std::min(size_t(4096), imageSize - written);
    if (file.read(buffer.get(), count) != count) {
      esp_ota_abort(handle);
      return "Firmware read failed";
    }
    if (esp_ota_write(handle, buffer.get(), count) != ESP_OK) {
      esp_ota_abort(handle);
      return "Firmware write failed";
    }
    written += count;
    if (progress) progress(written, imageSize);
  }
  // ESP-IDF verifies the image format, chip compatibility, checksum and any
  // embedded SHA-256 digest. This consumes the handle on both success/failure.
  if (esp_ota_end(handle) != ESP_OK) return "Invalid firmware image";
  // ota_end validates against the entire partition. Bound a second check to
  // this file so old bytes beyond a truncated upload cannot complete the image.
  const esp_partition_pos_t position{target->address, static_cast<uint32_t>(imageSize)};
  esp_image_metadata_t metadata{};
  if (esp_image_verify(ESP_IMAGE_VERIFY, &position, &metadata) != ESP_OK ||
      metadata.image_len != imageSize) return "Firmware file size mismatch";
  staged = target;
  return nullptr;
}
}  // namespace bootswitch
#endif
