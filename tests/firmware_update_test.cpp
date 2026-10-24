#include "firmware_update.h"
#include "firmware_target.h"
#include <esp_app_format.h>
#include <esp_image_format.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using namespace bootswitch;

namespace {
constexpr size_t root = (BootDisk::kPartitionStart + BootDisk::kRootSector) * 512;
constexpr size_t data = (BootDisk::kPartitionStart + BootDisk::kDataSector) * 512;
constexpr size_t fat = (BootDisk::kPartitionStart + 1) * 512;
void put16(uint8_t* p, uint16_t value) { p[0] = value; p[1] = value >> 8; }
void put32(uint8_t* p, uint32_t value) { put16(p, value); put16(p + 2, value >> 16); }

struct Fixture {
  std::vector<uint8_t> bytes = std::vector<uint8_t>(VirtualDisk::kCapacity);
  VirtualDisk disk{bytes.data(), bytes.size()};
  void reset() {
    constexpr char config[] = "wifi_ssid=\n";
    assert(disk.format(config, sizeof(config) - 1));
  }
  void link(uint16_t cluster, uint16_t next) {
    for (unsigned copy = 0; copy < 2; ++copy)
      put16(bytes.data() + fat + copy * BootDisk::kFatSectors * 512 + cluster * 2, next);
  }
  void install(const std::vector<uint8_t>& payload, std::vector<uint16_t> chain = {}) {
    reset();
    const size_t count = (payload.size() + 511) / 512;
    if (chain.empty()) {
      for (size_t i = 0; i < count; ++i) chain.push_back(6 + i);
    }
    assert(chain.size() == count);
    auto* entry = bytes.data() + root + 128;
    std::memcpy(entry, "UPDATE  BIN", 11);
    entry[11] = 0x20;
    put16(entry + 26, chain.empty() ? 0 : chain[0]);
    put32(entry + 28, payload.size());
    for (size_t i = 0; i < count; ++i) {
      link(chain[i], i + 1 == count ? 0xFFFF : chain[i + 1]);
      std::memcpy(bytes.data() + data + (chain[i] - 2) * 512,
                  payload.data() + i * 512, std::min(size_t(512), payload.size() - i * 512));
    }
  }
  VirtualDisk::File open() {
    VirtualDisk::File file;
    assert(!disk.firmware(file));
    assert(file);
    return file;
  }
  void rejects() {
    VirtualDisk::File file;
    assert(disk.firmware(file));
    assert(!file);
  }
};

std::vector<uint8_t> payload(size_t size) {
  std::vector<uint8_t> result(size);
  for (size_t i = 0; i < size; ++i) result[i] = (i * 73 + i / 512) & 255;
  return result;
}

esp_partition_t running{0x10000, 0x330000}, target{0x340000, 0x330000};
bool missingSlot = false, beginFails = false, writeFails = false, validationFails = false;
int imageLengthDelta = 0;
size_t verifiedImageLength = 0;
unsigned begins = 0, writes = 0, ends = 0, aborts = 0, activations = 0;
size_t expectedSize = 0, lastProgress = 0;
std::vector<uint8_t> flashed;
void resetOta() {
  target = {0x340000, 0x330000};
  missingSlot = beginFails = writeFails = validationFails = false;
  begins = writes = ends = aborts = activations = 0;
  expectedSize = lastProgress = 0;
  imageLengthDelta = 0;
  verifiedImageLength = 0;
  flashed.clear();
}
void progress(size_t written, size_t total) {
  assert(written > lastProgress && written <= total && total == expectedSize);
  lastProgress = written;
}

void diskTests(Fixture& fixture) {
  fixture.reset();
  VirtualDisk::File absent;
  assert(!fixture.disk.firmware(absent) && !absent);
  const auto content = payload(1201);
  fixture.install(content, {700, 10, 900});
  auto file = fixture.open();
  assert(file.size() == content.size());
  std::vector<uint8_t> result;
  for (size_t request : {0u, 1u, 510u, 513u, 2048u, 512u}) {
    uint8_t buffer[2048];
    const size_t count = file.read(buffer, request);
    result.insert(result.end(), buffer, buffer + count);
  }
  assert(result == content);
  uint8_t byte;
  assert(file.read(&byte, 1) == 0);
  fixture.install({}); fixture.rejects();
  fixture.install(content);
  std::memcpy(fixture.bytes.data() + root + 160, fixture.bytes.data() + root + 128, 32);
  fixture.rejects();
  fixture.install(content); fixture.bytes[root + 128 + 11] = 0x10; fixture.rejects();
  fixture.install(content); put32(fixture.bytes.data() + root + 128 + 28, UINT32_MAX); fixture.rejects();
  fixture.install(content); put16(fixture.bytes.data() + root + 128 + 20, 1); fixture.rejects();
  for (uint16_t invalid : {0, 1, 2, 3, 0xFFF7, 0xFFFF}) {
    fixture.install(content);
    put16(fixture.bytes.data() + root + 128 + 26, invalid);
    fixture.rejects();
  }
  fixture.install(content); fixture.link(6, 0xFFFF); fixture.rejects();
  fixture.install(content); fixture.link(7, 6); fixture.rejects();
  fixture.install(content); fixture.link(8, 9); fixture.rejects();
  fixture.install(content); fixture.bytes[fat + 6 * 2] ^= 1; fixture.rejects();
  fixture.install(content); fixture.bytes[510] = 0; fixture.rejects();
  fixture.install(content); fixture.bytes[root + 32] = 'X'; fixture.rejects();
  // Deleted/temporary files do not request an update.
  fixture.install(content); fixture.bytes[root + 128] = 0xE5;
  assert(!fixture.disk.firmware(absent) && !absent);
  fixture.install(content); std::memcpy(fixture.bytes.data() + root + 128, "EDITOR  TMP", 11);
  assert(!fixture.disk.firmware(absent) && !absent);
  // Only UPDATE.BIN requests an update; the previous disk filename is ordinary
  // session storage on new firmware, like other non-update files.
  fixture.install(content); std::memcpy(fixture.bytes.data() + root + 128, "FIRMWAREBIN", 11);
  assert(!fixture.disk.firmware(absent) && !absent);
}

void otaTests(Fixture& fixture) {
  auto content = payload(1300123);
  esp_image_header_t header{};
  header.magic = ESP_IMAGE_HEADER_MAGIC;
  header.chip_id = ESP_CHIP_ID_ESP32S3;
  std::memcpy(content.data(), &header, sizeof(header));
  put32(content.data() + sizeof(header) + sizeof(esp_image_segment_header_t), ESP_APP_DESC_MAGIC_WORD);
  // A large fragmented file crosses both FAT sectors and flash write chunks.
  std::vector<uint16_t> chain;
  for (size_t i = 0; i < (content.size() + 511) / 512; ++i)
    chain.push_back(i % 2 ? 4000 + i / 2 : 6 + i / 2);
  fixture.install(content, chain);
  const esp_partition_t* staged = &running;
  resetOta();
  assert(!stageFirmware(fixture.open(), staged, progress));
  assert(staged == &target && flashed == content && lastProgress == content.size());
  assert(begins == 1 && ends == 1 && !aborts && !activations);
  resetOta(); missingSlot = true;
  assert(stageFirmware(fixture.open(), staged) && !staged && !begins);
  resetOta(); target.address = running.address;
  assert(stageFirmware(fixture.open(), staged) && !staged && !begins);
  resetOta(); target.size = 1024;
  assert(stageFirmware(fixture.open(), staged) && !staged && !begins);
  resetOta(); beginFails = true;
  assert(stageFirmware(fixture.open(), staged) && !staged && !aborts);
  resetOta(); writeFails = true;
  assert(stageFirmware(fixture.open(), staged) && !staged && aborts == 1 && !ends);
  resetOta(); validationFails = true;
  assert(stageFirmware(fixture.open(), staged) && !staged && ends == 1 && !aborts);
  for (int delta : {-32, 32}) {
    resetOta(); imageLengthDelta = delta;
    assert(stageFirmware(fixture.open(), staged) && !staged && ends == 1);
  }
  for (size_t offset : {0u, 12u, 32u}) {
    resetOta();
    auto invalid = content;
    invalid[offset] ^= 0xFF;
    fixture.install(invalid);
    assert(stageFirmware(fixture.open(), staged) && !staged && !begins);
  }
  resetOta(); fixture.install(payload(12));
  assert(stageFirmware(fixture.open(), staged) && !staged && !begins);
  assert(!activations); // Staging never changes the selected boot partition.
}

std::vector<uint8_t> readFile(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {});
}

void signatureTests(Fixture& fixture, const std::string& directory) {
  const auto content = readFile(directory + "/raw.bin");
  const auto signedImage = readFile(directory + "/signed.bin");
  const esp_partition_t* staged = &running;
  assert(signedImage.size() == content.size() + 80);
  std::vector<uint16_t> chain;
  for (size_t i = 0; i < (signedImage.size() + 511) / 512; ++i)
    chain.push_back(i % 2 ? 4000 + i / 2 : 6 + i / 2);
  fixture.install(signedImage, chain);
  resetOta(); verifiedImageLength = content.size();
  assert(!stageFirmware(fixture.open(), staged, progress));
  assert(staged == &target && flashed == content && lastProgress == content.size());
  assert(begins == 1 && ends == 1 && !aborts && !activations);
  // The trailer does not consume OTA capacity, even if the application fills it.
  resetOta(); target.size = content.size(); verifiedImageLength = content.size();
  assert(!stageFirmware(fixture.open(), staged) && staged == &target);
  resetOta(); target.size = content.size() - 1;
  assert(stageFirmware(fixture.open(), staged) && !staged && !begins);
  for (size_t offset : {size_t(0), size_t(4096), content.size() - 1,
                        content.size(), content.size() + 8, content.size() + 12,
                        content.size() + 16, signedImage.size() - 1}) {
    auto invalid = signedImage;
    invalid[offset] ^= 1;
    fixture.install(invalid);
    resetOta(); verifiedImageLength = content.size();
    assert(stageFirmware(fixture.open(), staged) && !staged && !activations);
    if (!BOOTSWITCH_ALLOW_UNSIGNED_FIRMWARE || offset != content.size()) assert(!begins);
  }
  fixture.install(readFile(directory + "/wrong-key.bin"));
  resetOta();
  assert(stageFirmware(fixture.open(), staged) && !staged && !begins);
  // Invalid scalar encodings are rejected by the real mbedTLS verifier.
  auto invalid = signedImage;
  std::fill(invalid.end() - 64, invalid.end(), 0);
  fixture.install(invalid); resetOta();
  assert(stageFirmware(fixture.open(), staged) && !staged && !begins);
  std::fill(invalid.end() - 64, invalid.end(), 255);
  fixture.install(invalid); resetOta();
  assert(stageFirmware(fixture.open(), staged) && !staged && !begins);
  for (auto invalid : {signedImage, content, std::vector<uint8_t>(12)}) {
    invalid.pop_back();
    fixture.install(invalid); resetOta(); verifiedImageLength = content.size();
    assert(stageFirmware(fixture.open(), staged) && !staged && !activations);
    if (!BOOTSWITCH_ALLOW_UNSIGNED_FIRMWARE) assert(!begins);
  }
  invalid = signedImage;
  invalid.push_back(0);
  fixture.install(invalid); resetOta(); verifiedImageLength = content.size();
  assert(stageFirmware(fixture.open(), staged) && !staged && !activations);
  if (!BOOTSWITCH_ALLOW_UNSIGNED_FIRMWARE) assert(!begins);
  fixture.install(content); resetOta();
  const auto* error = stageFirmware(fixture.open(), staged);
  if (BOOTSWITCH_ALLOW_UNSIGNED_FIRMWARE) {
    assert(!error && staged == &target && flashed == content);
  } else assert(error && !staged && !begins);
  // Authentication does not bypass ESP-IDF validation or activate an image.
  fixture.install(signedImage); resetOta(); validationFails = true;
  assert(stageFirmware(fixture.open(), staged) && !staged && ends == 1 && !activations);
  std::cout << "Real ECDSA verification, tampering, wrong keys, unsigned policy and trailer stripping passed.\n";
}
void targetTests(Fixture& fixture, const std::string& directory) {
  for (const auto& name : {"raw.bin", "signed.bin", "target-0-raw.bin", "target-0-signed.bin",
                           "target-1-raw.bin", "target-1-signed.bin", "target-2-raw.bin"}) {
    const std::string filename(name);
    const bool signedImage = filename.find("signed") != std::string::npos;
    const unsigned board = filename.find("target-1") == 0 ? 1 :
                           filename.find("target-2") == 0 ? 2 : 0;
    const auto content = readFile(directory + "/" + filename);
    fixture.install(content);
    // Exercise the identical file chain through the small-board sparse pool.
    std::vector<uint8_t> pool(1792 * 1024);
    std::vector<uint16_t> map(BootDisk::kSectorCount);
    VirtualDisk sparse(pool.data(), pool.size(), map.data());
    const char config[] = "wifi_ssid=\n";
    assert(sparse.format(config, sizeof(config) - 1));
    uint8_t base[512];
    for (uint32_t lba = 0; lba < BootDisk::kSectorCount; ++lba) {
      BootDisk::sector(lba, base, Mode::Linux);
      if (std::memcmp(base, fixture.bytes.data() + lba * 512, 512))
        assert(sparse.write(lba, 0, fixture.bytes.data() + lba * 512, 512) == 512);
    }
    for (auto* disk : {&fixture.disk, &sparse}) {
      resetOta();
      VirtualDisk::File file;
      assert(!disk->firmware(file) && file);
      const esp_partition_t* staged = &running;
      const auto* error = stageFirmware(file, staged);
      const bool accepted = board == kFirmwareTargetId &&
                            (signedImage || BOOTSWITCH_ALLOW_UNSIGNED_FIRMWARE);
      assert(bool(error) != accepted);
      assert((staged == &target) == accepted);
      assert(begins == (accepted ? 1u : 0u));
      if (accepted) {
        const auto length = content.size() - (signedImage ? 80 : 0);
        assert(flashed == std::vector<uint8_t>(content.begin(), content.begin() + length));
      }
    }
  }
  std::cout << "Board identity and legacy compatibility enforced before OTA erase, with full and sparse disks.\n";
}
}  // namespace

const esp_partition_t* esp_ota_get_running_partition() { return &running; }
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) {
  return missingSlot ? nullptr : &target;
}
esp_err_t esp_ota_begin(const esp_partition_t* partition, size_t size, esp_ota_handle_t* handle) {
  assert(partition->address != running.address);
  ++begins; expectedSize = size; *handle = 42;
  return beginFails ? -1 : ESP_OK;
}
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void* bytes, size_t size) {
  assert(handle == 42); ++writes;
  if (writeFails && writes == 2) return -1;
  const auto* source = static_cast<const uint8_t*>(bytes);
  flashed.insert(flashed.end(), source, source + size);
  return ESP_OK;
}
esp_err_t esp_ota_end(esp_ota_handle_t handle) {
  assert(handle == 42 && flashed.size() == expectedSize); ++ends;
  return validationFails ? -1 : ESP_OK;
}
esp_err_t esp_ota_abort(esp_ota_handle_t handle) { assert(handle == 42); ++aborts; return ESP_OK; }
esp_err_t esp_ota_set_boot_partition(const esp_partition_t*) { ++activations; return ESP_OK; }
esp_err_t esp_image_verify(esp_image_load_mode_t, const esp_partition_pos_t* position,
                           esp_image_metadata_t* metadata) {
  assert(position->offset == target.address && position->size == expectedSize);
  metadata->image_len = (verifiedImageLength ? verifiedImageLength : expectedSize) + imageLengthDelta;
  return ESP_OK;
}

int main(int argc, char** argv) {
  Fixture fixture;
  if (argc == 3 && std::string(argv[1]) == "--target-tests") {
    targetTests(fixture, argv[2]);
    return 0;
  }
  if (argc == 3 && std::string(argv[1]) == "--signed-tests") {
    signatureTests(fixture, argv[2]);
    return 0;
  }
  if (argc == 4 && std::string(argv[1]) == "--extract") {
    std::ifstream input(argv[2], std::ios::binary);
    fixture.bytes.assign(std::istreambuf_iterator<char>(input), {});
    VirtualDisk disk(fixture.bytes.data(), fixture.bytes.size());
    VirtualDisk::File file;
    assert(!disk.firmware(file) && file);
    std::ofstream output(argv[3], std::ios::binary);
    uint8_t buffer[4096];
    while (const size_t count = file.read(buffer, sizeof(buffer)))
      output.write(reinterpret_cast<const char*>(buffer), count);
    assert(output.good());
    return 0;
  }
  diskTests(fixture);
  if (BOOTSWITCH_ALLOW_UNSIGNED_FIRMWARE) otaTests(fixture);
  if (argc == 2) {
    fixture.reset();
    std::ofstream output(std::string(argv[1]) + "/firmware-disk.img", std::ios::binary);
    output.write(reinterpret_cast<const char*>(fixture.bytes.data()), fixture.bytes.size());
    assert(output.good());
  }
  std::cout << "Firmware FAT streaming, corrupt chains, inactive-slot staging and failed OTA writes passed.\n";
}
