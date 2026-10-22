#include "ble_control.h"

#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>
#include <cstdlib>
#include <string>

namespace bootswitch {
namespace {
constexpr char kService[] = "7e6e0001-bb6e-4b08-8e8a-fc9f1b6a4450";
constexpr char kState[] = "7e6e0002-bb6e-4b08-8e8a-fc9f1b6a4450";
constexpr char kCommand[] = "7e6e0003-bb6e-4b08-8e8a-fc9f1b6a4450";
QueueHandle_t commands = nullptr;
NimBLECharacteristic* state = nullptr;
bool initialized = false;
bool active = false;
std::atomic<uint16_t> subscribers[CONFIG_BT_NIMBLE_MAX_CONNECTIONS];

void unsubscribe(uint16_t handle) {
  for (auto& slot : subscribers) {
    if (slot.load() == handle) slot.store(BLE_HS_CONN_HANDLE_NONE);
  }
}

class StateCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo& peer, uint16_t value) override {
    const uint16_t handle = peer.getConnHandle();
    unsubscribe(handle);
    if (!(value & 1)) return;
    for (auto& slot : subscribers) {
      if (slot.load() == BLE_HS_CONN_HANDLE_NONE) { slot.store(handle); break; }
    }
  }
} stateCallbacks;

class CommandCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& peer) override {
    // Enforce authenticated encryption both in ATT permissions and here.
    if (!peer.isEncrypted() || !peer.isAuthenticated()) return;
    const auto value = characteristic->getValue();
    const BleCommand command = parseBleCommand(value.data(), value.size());
    if (command != BleCommand::None && commands)
      xQueueSend(commands, &command, 0);
  }
} commandCallbacks;

class ServerCallbacks : public NimBLEServerCallbacks {
  void onDisconnect(NimBLEServer*, NimBLEConnInfo& peer, int) override {
    unsubscribe(peer.getConnHandle());
  }

  void onAuthenticationComplete(NimBLEConnInfo& peer) override {
    if (!peer.isEncrypted() || !peer.isAuthenticated())
      NimBLEDevice::getServer()->disconnect(peer.getConnHandle());
  }
} serverCallbacks;

const char* fail(const char* error) {
  stopBle();
  return error;
}
}  // namespace

const char* startBle(const Settings& settings, Mode mode, bool forgetBonds) {
  if (!settings.bleEnabled()) return nullptr;
  if (validateSettings(settings)) return "Invalid BLE settings";
  commands = xQueueCreate(8, sizeof(BleCommand));
  if (!commands) return "BLE queue unavailable";
  for (auto& slot : subscribers) slot.store(BLE_HS_CONN_HANDLE_NONE);
  initialized = NimBLEDevice::init(settings.hostname);
  if (!initialized) return fail("BLE init failed");
  // A changed PIN must revoke existing bonded keys, which otherwise bypass
  // passkey entry. Keep bonds on ordinary reboots with the same configured PIN.
  if (forgetBonds && !NimBLEDevice::deleteAllBonds()) return fail("BLE bond reset failed");
  NimBLEDevice::setSecurityAuth(true, true, true);  // bonding, MITM, Secure Connections
  NimBLEDevice::setSecurityPasskey(std::strtoul(settings.blePasskey, nullptr, 10));
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  auto* server = NimBLEDevice::createServer();
  if (!server) return fail("BLE server unavailable");
  server->setCallbacks(&serverCallbacks, false);
  server->advertiseOnDisconnect(true);
  auto* service = server->createService(kService);
  if (!service) return fail("BLE service unavailable");
  state = service->createCharacteristic(kState,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC |
      NIMBLE_PROPERTY::READ_AUTHEN | NIMBLE_PROPERTY::NOTIFY, 7);
  auto* command = service->createCharacteristic(kCommand,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC |
      NIMBLE_PROPERTY::WRITE_AUTHEN, 7);
  if (!state || !command) return fail("BLE characteristic unavailable");
  state->setValue(mode == Mode::Windows ? "windows" : "linux");
  state->setCallbacks(&stateCallbacks);
  command->setCallbacks(&commandCallbacks);
  if (!server->start()) return fail("BLE server start failed");
  auto* advertising = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData scan;
  if (!scan.setName(std::string(settings.hostname).substr(0, 29)) ||
      !advertising->setScanResponseData(scan) || !advertising->addServiceUUID(kService))
    return fail("BLE advertising failed");
  advertising->enableScanResponse(true);
  if (!advertising->start()) return fail("BLE advertising failed");
  active = true;
  return nullptr;
}

void stopBle() {
  active = false;
  if (initialized) NimBLEDevice::deinit(true);
  initialized = false;
  state = nullptr;
  if (commands) vQueueDelete(commands);
  commands = nullptr;
}

bool bleActive() { return active; }

BleCommand nextBleCommand() {
  BleCommand command = BleCommand::None;
  if (active) xQueueReceive(commands, &command, 0);
  return command;
}

void publishBleMode(Mode mode) {
  if (!active) return;
  state->setValue(mode == Mode::Windows ? "windows" : "linux");
  // Notifications also need an authenticated link, independently of whether
  // the phone has attempted a protected read or write yet.
  auto* server = NimBLEDevice::getServer();
  for (const auto& slot : subscribers) {
    const uint16_t handle = slot.load();
    if (handle == BLE_HS_CONN_HANDLE_NONE) continue;
    const auto peer = server->getPeerInfoByHandle(handle);
    if (peer.isEncrypted() && peer.isAuthenticated()) state->notify(handle);
  }
}
}  // namespace bootswitch
