#include "update_coordinator.h"
#include <esp_app_format.h>
#include <esp_image_format.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace bootswitch;

namespace {
// Conditions determine the interleaving; the timeout only detects deadlocks.
class Checkpoint {
 public:
  void signal() {
    std::lock_guard<std::mutex> lock(mutex_);
    reached_ = true;
    condition_.notify_all();
  }
  void wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    assert(condition_.wait_for(lock, std::chrono::seconds(10), [&] { return reached_; }));
  }
  void pause() {
    std::unique_lock<std::mutex> lock(mutex_);
    reached_ = true;
    condition_.notify_all();
    assert(condition_.wait_for(lock, std::chrono::seconds(10), [&] { return released_; }));
  }
  void release() {
    std::lock_guard<std::mutex> lock(mutex_);
    released_ = true;
    condition_.notify_all();
  }
 private:
  std::mutex mutex_;
  std::condition_variable condition_;
  bool reached_ = false, released_ = false;
};

enum class Fault { None, Begin, Write, End, Verify, Length, Activate, Save, Storage, Load, Rebuild };
struct Flash {
  esp_partition_t running{0x10000, 0x330000}, target{0x340000, 0x330000};
  const esp_partition_t* selected = &running;
  Fault fault = Fault::None;
  unsigned begins = 0, writes = 0, ends = 0, aborts = 0, activations = 0;
  size_t expectedSize = 0;
  bool verified = false;
  std::vector<uint8_t> image;
  Checkpoint* authenticated = nullptr;
};
Flash* flash = nullptr;

class Backend : public UpdateBackend {
 public:
  std::mutex diskMutex;
  Settings saved;
  Flash ota;
  std::atomic<bool> media{true}, connected{true};
  unsigned writeCalls = 0, stageCalls = 0, saveCalls = 0, rebuilds = 0, reconnects = 0;
  Checkpoint* lockAttempt = nullptr;
  Checkpoint* admittedWrite = nullptr;
  Checkpoint* hideMedia = nullptr;
  Checkpoint* formatting = nullptr;
  Checkpoint* publishMedia = nullptr;
  void lockDisk() override {
    if (lockAttempt) lockAttempt->signal();
    diskMutex.lock();
  }
  void unlockDisk() override { diskMutex.unlock(); }
  void mediaPresent(bool present) override {
    if (!present && hideMedia) hideMedia->pause();
    if (present && publishMedia) publishMedia->pause();
    media.store(present);
  }
  bool settingsAvailable() const override { return ota.fault != Fault::Storage; }
  bool loadSavedSettings(Settings& output) override {
    if (ota.fault == Fault::Load) return false;
    output = saved;
    return true;
  }
  bool saveSettings(const char* text, size_t length) override {
    ++saveCalls;
    if (ota.fault == Fault::Save) return false;
    assert(!parseSettings(text, length, saved));
    return true;
  }
  void beginRecovery() override { assert(!media.load()); connected.store(false); }
  void endRecovery() override { ++reconnects; connected.store(true); }
  int32_t writeDisk(VirtualDisk& disk, uint32_t lba, uint32_t offset,
                    const void* bytes, uint32_t length) override {
    ++writeCalls;
    if (admittedWrite) admittedWrite->pause();
    return disk.write(lba, offset, bytes, length);
  }
  bool rebuildDisk(VirtualDisk& disk, const char* text, size_t length) override {
    ++rebuilds;
    assert(!media.load() && !connected.load());
    if (formatting) formatting->pause();
    return ota.fault != Fault::Rebuild && disk.format(text, length);
  }
  const char* stage(VirtualDisk::File file, const esp_partition_t*& staged) override {
    ++stageCalls;
    assert(!media.load());
    return stageFirmware(file, staged);
  }
  bool activate(const esp_partition_t* staged) override {
    assert(saveCalls == 1 && ota.verified);
    return esp_ota_set_boot_partition(staged) == ESP_OK;
  }
};

constexpr size_t rootOffset = (BootDisk::kPartitionStart + BootDisk::kRootSector) * 512;
constexpr size_t dataOffset = (BootDisk::kPartitionStart + BootDisk::kDataSector) * 512;
constexpr size_t fatOffset = (BootDisk::kPartitionStart + 1) * 512;
void put16(uint8_t* p, uint16_t value) { p[0] = value; p[1] = value >> 8; }
void put32(uint8_t* p, uint32_t value) { put16(p, value); put16(p + 2, value >> 16); }
std::vector<uint8_t> load(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input), {}};
}

struct Fixture {
  std::vector<uint8_t> bytes = std::vector<uint8_t>(VirtualDisk::kCapacity);
  VirtualDisk disk{bytes.data(), bytes.size()};
  Backend backend;
  char text[VirtualDisk::kMaxConfigSize + 1];
  UpdateCoordinator updates{disk, backend, text, sizeof(text)};
  ApplyResult result;
  Fixture(const std::vector<uint8_t>& image, bool firmware = true) {
    flash = &backend.ota;
    std::strcpy(backend.saved.hostname, "original");
    Settings pending = backend.saved;
    std::strcpy(pending.hostname, "pending");
    size_t length = 0;
    assert(formatSettings(pending, text, sizeof(text), length));
    assert(disk.format(text, length));
    if (firmware) {
      const size_t count = (image.size() + 511) / 512;
      auto* entry = bytes.data() + rootOffset + 128;
      std::memcpy(entry, "UPDATE  BIN", 11);
      entry[11] = 0x20;
      put16(entry + 26, 6);
      put32(entry + 28, image.size());
      for (size_t i = 0; i < count; ++i) {
        const uint16_t cluster = 6 + i;
        const uint16_t next = i + 1 == count ? 0xFFFF : cluster + 1;
        for (unsigned copy = 0; copy < 2; ++copy)
          put16(bytes.data() + fatOffset + copy * BootDisk::kFatSectors * 512 + cluster * 2, next);
        std::memcpy(bytes.data() + dataOffset + (cluster - 2) * 512,
                    image.data() + i * 512, std::min(size_t(512), image.size() - i * 512));
      }
    }
    // Arbitrary host data must disappear from the entire recovered RAM disk.
    bytes.back() = 0x7B;
  }
  void apply() {
    assert(updates.requestApply());
    assert(updates.processApply(result));
  }
  void assertFreshDisk() {
    char canonical[VirtualDisk::kMaxConfigSize + 1];
    size_t length = 0;
    assert(formatSettings(backend.saved, canonical, sizeof(canonical), length));
    std::vector<uint8_t> fresh(VirtualDisk::kCapacity);
    VirtualDisk expected(fresh.data(), fresh.size());
    assert(expected.format(canonical, length));
    assert(bytes == fresh);
    assert(updates.writable() && backend.media.load() && backend.connected.load());
    assert(!result.restart && result.error);
  }
};

void admittedWriteDrains(const std::vector<uint8_t>& image) {
  Fixture f(image);
  Checkpoint admitted, attempted;
  f.backend.admittedWrite = &admitted;
  // Change a real CONFIG.INI value after write admission, before its copy.
  const std::string config(f.text);
  const size_t address = dataOffset + 1024 + config.find("pending");
  assert(config.find("pending") != std::string::npos);
  int32_t written = -1;
  std::thread writer([&] { written = f.updates.write(address / 512, address % 512, "drained", 7); });
  admitted.wait();
  assert(f.updates.requestApply());
  f.backend.lockAttempt = &attempted;
  std::thread processor([&] { assert(f.updates.processApply(f.result)); });
  attempted.wait();  // Processor is blocked behind the admitted writer's mutex.
  assert(!f.updates.writable() && !f.backend.media.load());
  admitted.release();
  writer.join(); processor.join();
  assert(written == 7 && f.result.restart && !f.result.error);
  assert(std::strcmp(f.backend.saved.hostname, "drained") == 0);
  assert(f.backend.ota.selected == &f.backend.ota.target);
}

void queuedWriteIsRejected(const std::vector<uint8_t>& image) {
  Fixture f(image);
  const auto snapshot = f.bytes;
  Checkpoint attempted;
  f.backend.lockAttempt = &attempted;
  f.backend.diskMutex.lock();
  int32_t written = 0;
  std::thread writer([&] { written = f.updates.write(100, 0, "bad", 3); });
  attempted.wait();  // Passed first gate, but has not acquired the disk mutex.
  assert(f.updates.requestApply());
  f.backend.diskMutex.unlock();
  writer.join();
  assert(written == -1 && f.backend.writeCalls == 0 && f.bytes == snapshot);
  assert(f.updates.processApply(f.result) && f.result.restart);
}

void incompleteEjectIsNotProcessed(const std::vector<uint8_t>& image) {
  Fixture f(image);
  Checkpoint hidden;
  f.backend.hideMedia = &hidden;
  std::thread ejector([&] { assert(f.updates.requestApply()); });
  hidden.wait();
  assert(!f.updates.writable() && !f.updates.applyPending());
  assert(!f.updates.processApply(f.result) && !f.updates.requestApply());
  assert(f.updates.write(0, 0, "bad", 3) == -1);
  hidden.release(); ejector.join();
  assert(!f.backend.media.load() && f.updates.applyPending());
  assert(f.updates.processApply(f.result) && f.result.restart);
  assert(!f.updates.processApply(f.result) && !f.updates.requestApply());
}

void verifiedBytesStayFrozen(const std::vector<uint8_t>& image) {
  Fixture f(image);
  const auto snapshot = f.bytes;
  Checkpoint authenticated;
  f.backend.ota.authenticated = &authenticated;
  assert(f.updates.requestApply() && !f.updates.requestApply());
  std::thread processor([&] { assert(f.updates.processApply(f.result)); });
  authenticated.wait();  // Real signature verification passed; OTA begin reached.
  assert(!f.updates.requestApply() && !f.updates.applyPending());
  assert(f.updates.write((dataOffset + 2048) / 512, 0, "bad", 3) == -1);
  authenticated.release(); processor.join();
  assert(f.result.restart && f.bytes == snapshot);
  assert(f.backend.ota.image == std::vector<uint8_t>(image.begin(), image.end() - 80));
  assert(f.backend.stageCalls == 1 && f.backend.ota.activations == 1);
}

void recoveryIsHiddenAndAtomic(const std::vector<uint8_t>& image) {
  Fixture f(image);
  f.backend.ota.fault = Fault::Activate;
  Checkpoint formatting, publish;
  f.backend.formatting = &formatting;
  f.backend.publishMedia = &publish;
  assert(f.updates.requestApply());
  std::thread processor([&] { assert(f.updates.processApply(f.result)); });
  formatting.wait();
  assert(!f.updates.writable() && !f.backend.media.load() && !f.backend.connected.load());
  assert(!f.updates.requestApply() && f.updates.write(0, 0, "bad", 3) == -1);
  formatting.release();
  publish.wait();  // Fresh disk is complete, but not yet exposed to the host.
  assert(!f.backend.media.load() && !f.backend.connected.load());
  // Same bytes a reboot would generate from the now-committed saved settings.
  assert(std::strcmp(f.backend.saved.hostname, "pending") == 0);
  char canonical[VirtualDisk::kMaxConfigSize + 1]; size_t length = 0;
  assert(formatSettings(f.backend.saved, canonical, sizeof(canonical), length));
  std::vector<uint8_t> expected(VirtualDisk::kCapacity);
  VirtualDisk clean(expected.data(), expected.size());
  assert(clean.format(canonical, length) && f.bytes == expected);
  publish.release(); processor.join();
  f.assertFreshDisk();
  assert(!f.updates.processApply(f.result)); // Duplicate eject did not queue work.
  assert(f.backend.rebuilds == 1 && f.backend.reconnects == 1);
}

void injectedFailures(const std::vector<uint8_t>& image) {
  const struct { Fault fault; const char* error; } cases[] = {
    {Fault::Begin, "Firmware update begin failed"}, {Fault::Write, "Firmware write failed"},
    {Fault::End, "Invalid firmware image"}, {Fault::Verify, "Firmware file size mismatch"},
    {Fault::Length, "Firmware file size mismatch"}, {Fault::Activate, "Firmware activation failed"},
    {Fault::Save, "Settings save failed"}, {Fault::Storage, "Settings unavailable"},
  };
  for (const auto& test : cases) {
    Fixture f(image); f.backend.ota.fault = test.fault;
    f.apply();
    assert(f.result.error && std::strcmp(f.result.error, test.error) == 0);
    assert(f.backend.ota.selected == &f.backend.ota.running);
    assert(f.backend.ota.activations == (test.fault == Fault::Activate ? 1u : 0u));
    // Valid settings are committed before activation, and retained on its failure.
    assert(std::strcmp(f.backend.saved.hostname, test.fault == Fault::Activate ? "pending" : "original") == 0);
    f.assertFreshDisk();
    // Recovery permits a host to upload a corrected signed image and retry.
    Fixture corrected(image);
    flash = &f.backend.ota;
    f.backend.ota.fault = Fault::None;
    f.backend.ota.image.clear();
    f.backend.ota.verified = false;
    f.backend.saveCalls = 0;
    for (size_t offset = 0; offset < corrected.bytes.size(); offset += 4096)
      assert(f.updates.write(offset / 512, 0, corrected.bytes.data() + offset, 4096) == 4096);
    assert(f.updates.requestApply() && f.updates.processApply(f.result));
    assert(!f.result.error && f.result.restart && f.result.firmware);
    assert(f.backend.ota.selected == &f.backend.ota.target);
    assert(!f.updates.writable() && !f.backend.media.load());
  }
}

void authenticationFailure(const std::vector<uint8_t>& image) {
  auto bad = image; bad.back() ^= 1;
  Fixture f(bad); f.apply();
  assert(f.result.error && std::strcmp(f.result.error, "Invalid firmware signature") == 0);
  assert(!f.backend.ota.begins && !f.backend.saveCalls && !f.backend.ota.activations);
  f.assertFreshDisk();
}

void recoveryFailuresStayClosed(const std::vector<uint8_t>& image) {
  for (const auto fault : {Fault::Load, Fault::Rebuild}) {
    auto bad = image; bad.back() ^= 1;
    Fixture f(bad); f.backend.ota.fault = fault; f.apply();
    assert(f.result.error && std::strcmp(f.result.error, "USB disk recovery failed") == 0);
    assert(!f.result.restart && !f.updates.writable() && !f.backend.media.load());
    assert(f.backend.connected.load() && !f.updates.requestApply());
    assert(f.updates.write(0, 0, "bad", 3) == -1);
    assert(!f.backend.ota.begins && !f.backend.ota.activations);
  }
}

void settingsOnly(const std::vector<uint8_t>& image) {
  Fixture f(image, false); f.apply();
  assert(!f.result.error && !f.result.firmware && f.result.restart);
  assert(f.backend.stageCalls == 0 && f.backend.saveCalls == 1 && !f.backend.ota.activations);
  assert(std::strcmp(f.backend.saved.hostname, "pending") == 0);
}

void invalidSettingsNeverStage(const std::vector<uint8_t>& image) {
  Fixture f(image);
  const size_t offset = std::string(f.text).find("pending");
  assert(offset != std::string::npos);
  f.bytes[dataOffset + 1024 + offset] = '-';
  f.apply();
  assert(f.result.error && !f.backend.stageCalls && !f.backend.saveCalls && !f.backend.ota.activations);
  f.assertFreshDisk();
}

void unsignedPolicy(const std::vector<uint8_t>& image) {
  Fixture f(image); f.apply();
  if (BOOTSWITCH_ALLOW_UNSIGNED_FIRMWARE) {
    assert(!f.result.error && f.result.restart && f.backend.ota.selected == &f.backend.ota.target);
  } else {
    assert(f.result.error && std::strcmp(f.result.error, "Firmware signature required") == 0);
    assert(!f.backend.ota.begins && !f.backend.saveCalls && !f.backend.ota.activations);
    f.assertFreshDisk();
  }
}
}  // namespace

const esp_partition_t* esp_ota_get_running_partition() { return &flash->running; }
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) { return &flash->target; }
esp_err_t esp_ota_begin(const esp_partition_t* partition, size_t size, esp_ota_handle_t* handle) {
  assert(partition == &flash->target);
  ++flash->begins; flash->expectedSize = size; *handle = 42;
  if (flash->authenticated) flash->authenticated->pause();
  return flash->fault == Fault::Begin ? -1 : ESP_OK;
}
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void* bytes, size_t length) {
  assert(handle == 42); ++flash->writes;
  if (flash->fault == Fault::Write) return -1;
  const auto* data = static_cast<const uint8_t*>(bytes);
  flash->image.insert(flash->image.end(), data, data + length);
  return ESP_OK;
}
esp_err_t esp_ota_end(esp_ota_handle_t handle) {
  assert(handle == 42 && flash->image.size() == flash->expectedSize); ++flash->ends;
  return flash->fault == Fault::End ? -1 : ESP_OK;
}
esp_err_t esp_ota_abort(esp_ota_handle_t handle) { assert(handle == 42); ++flash->aborts; return ESP_OK; }
esp_err_t esp_image_verify(esp_image_load_mode_t, const esp_partition_pos_t* position,
                           esp_image_metadata_t* metadata) {
  assert(position->offset == flash->target.address && position->size == flash->expectedSize);
  metadata->image_len = flash->expectedSize + (flash->fault == Fault::Length ? 1 : 0);
  flash->verified = flash->fault != Fault::Verify && flash->fault != Fault::Length;
  return flash->fault == Fault::Verify ? -1 : ESP_OK;
}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t* partition) {
  assert(flash->verified && partition == &flash->target); ++flash->activations;
  if (flash->fault == Fault::Activate) return -1;
  flash->selected = partition;
  return ESP_OK;
}

int main(int argc, char** argv) {
  assert(argc == 2);
  const auto image = load(std::string(argv[1]) + "/signed.bin");
  admittedWriteDrains(image);
  queuedWriteIsRejected(image);
  incompleteEjectIsNotProcessed(image);
  verifiedBytesStayFrozen(image);
  recoveryIsHiddenAndAtomic(image);
  injectedFailures(image);
  authenticationFailure(image);
  recoveryFailuresStayClosed(image);
  settingsOnly(image);
  invalidSettingsNeverStage(image);
  unsignedPolicy(load(std::string(argv[1]) + "/raw.bin"));
  std::cout << "Update coordinator: forced write/eject/recovery races, real signatures, activation/NVS failures and retries passed.\n";
}
