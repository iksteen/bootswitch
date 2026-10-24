#include <Arduino.h>
#include "board.h"
#include <Preferences.h>
#include <USB.h>
#include <USBMSC.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include <esp32-hal-cpu.h>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <freertos/semphr.h>
#include "tusb.h"
#include "device/dcd.h"
#include "boot_disk.h"
#include "ble_control.h"
#include "ble_power.h"
#include "board_power.h"
#include "settings.h"
#include "virtual_disk.h"
#include "usb_wake.h"
#include "display_telemetry.h"
#include "screen.h"
#include "firmware_update.h"
#include "update_coordinator.h"

#if ARDUINO_USB_MODE != 0 || ARDUINO_USB_CDC_ON_BOOT != 0
#error "BootSwitch requires native USB OTG mode, with CDC on boot disabled"
#endif
using bootswitch::BootDisk;
using bootswitch::Mode;
using bootswitch::Settings;
using bootswitch::VirtualDisk;
using bootswitch::BleCommand;
using bootswitch::UsbWakeResult;

namespace {
USBMSC disk;
#if BOOTSWITCH_BOARD_TQT && BOOTSWITCH_ENABLE_REMOTE_FLASH
// Explicitly start CDC before USB.begin(), after all interfaces are registered.
// CDC's host-controlled ROM reset is a development feature only.
USBCDC recoverySerial;
std::atomic<bool> cdcDownloadRequested{false};
#endif
#if BOOTSWITCH_BOARD_TQT
constexpr size_t kDiskPoolSize = 1792 * 1024;
uint16_t diskSectorMap[BootDisk::kSectorCount];
#else
constexpr size_t kDiskPoolSize = VirtualDisk::kCapacity;
#endif
Preferences preferences;
WebServer server(80);
Settings settings;
uint8_t* diskBytes = nullptr;
VirtualDisk virtualDisk(nullptr, 0);
SemaphoreHandle_t diskMutex = nullptr;
bool ramDiskReady = false;
char configText[VirtualDisk::kMaxConfigSize + 1];
// The TinyUSB event hook can run in an ISR; only hand off a lock-free flag.
static_assert(ATOMIC_BOOL_LOCK_FREE == 2, "USB wake flag must be ISR safe");
std::atomic<bool> usbWakeRequested{false};
std::atomic<Mode> selected{Mode::Linux};
bool storageReady = false;
bool usbReady = false;
bool networkEnabled = false;
bool apiEnabled = false;
bool mdnsStarted = false;
bool lastConnected = false;
bool redraw = true;
const char* problem = nullptr;
uint32_t lastRetry = 0;
uint32_t restartAt = 0;
enum class RestartAction { None, Application
#if BOOTSWITCH_ENABLE_REMOTE_FLASH
  , Flash
#endif
};
RestartAction restartPending = RestartAction::None;
bool screenAwake = true;
bool powerControllerSleep = false;
// Confirmed at startup; -1 means the PM1 charge state could not be configured.
int8_t batteryChargingEnabled = -1;
uint32_t lastDisplayActivity = 0;
bootswitch::DisplayTelemetry displayTelemetry;
bootswitch::ScreenRenderer screen;
#if BOOTSWITCH_ENABLE_USB_UPDATE
int lastFirmwarePercent = -1;
uint32_t lastFirmwareDraw = 0;
void firmwareProgress(size_t written, size_t total);
#endif
void drawScreen();
bootswitch::ScreenState screenState();

void wakeDisplay() {
  lastDisplayActivity = millis();
  if (!screenAwake) {
    screenAwake = true;
    displayTelemetry.invalidate();
    bootswitch::boardDisplay().wakeup();
    redraw = true;
  }
}

UsbWakeResult wakeHost() {
  wakeDisplay();
  const auto result = bootswitch::requestUsbWake();
  switch (result) {
    case UsbWakeResult::Requested: problem = "USB wake requested"; break;
    case UsbWakeResult::NotConfigured: problem = "USB host not connected"; break;
    case UsbWakeResult::NotSuspended: problem = "USB host not suspended"; break;
    case UsbWakeResult::NotEnabled: problem = "Host has not armed USB wake"; break;
  }
  redraw = true;
  return result;
}

void loadSettings() {
  // USB/NVS configuration is the only settings source. Fresh devices and
  // unreadable saved configurations start with both network services disabled.
  settings = Settings{};
  if (storageReady && preferences.isKey("settings")) {
    const String saved = preferences.getString("settings");
    if (const char* error = bootswitch::parseSettings(saved.c_str(), saved.length(), settings))
      problem = error;
  }
  size_t length = 0;
  if (!bootswitch::formatSettings(settings, configText, sizeof(configText), length)) return;
  // Persist the initial empty configuration without inventing credentials.
  if (storageReady && !preferences.isKey("settings") &&
      preferences.putString("settings", configText) != length)
    problem = "Settings save failed";
  diskMutex = xSemaphoreCreateMutex();
  diskBytes = static_cast<uint8_t*>(heap_caps_malloc(kDiskPoolSize,
                                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (diskMutex && diskBytes) {
    virtualDisk = VirtualDisk(diskBytes, kDiskPoolSize
#if BOOTSWITCH_BOARD_TQT
                              , diskSectorMap
#endif
    );
    ramDiskReady = virtualDisk.format(configText, length);
  }
  if (!ramDiskReady) {
    if (diskBytes) heap_caps_free(diskBytes);
    diskBytes = nullptr;
    virtualDisk = VirtualDisk(nullptr, 0);
    if (diskMutex) vSemaphoreDelete(diskMutex);
    diskMutex = nullptr;
    problem = "Config disk unavailable";
  }
}

const char* modeName(Mode mode) {
  return mode == Mode::Windows ? "windows" : "linux";
}

bool setMode(Mode mode) {
  if (restartPending != RestartAction::None) return false;
  wakeDisplay();
  if (mode == selected.load()) return true;
  if (!storageReady || preferences.putUChar("mode", uint8_t(mode)) != 1) {
    problem = "Save failed";
    redraw = true;
    return false;
  }
  // Publish only after NVS confirms persistence; USB callbacks never touch NVS.
  selected.store(mode);
  bootswitch::publishBleMode(mode);
  problem = nullptr;
  redraw = true;
  return true;
}

class DeviceUpdateBackend : public bootswitch::UpdateBackend {
 public:
  void lockDisk() override { xSemaphoreTake(diskMutex, portMAX_DELAY); }
  void unlockDisk() override { xSemaphoreGive(diskMutex); }
  void mediaPresent(bool present) override { disk.mediaPresent(present); }
  bool settingsAvailable() const override { return storageReady; }
  bool loadSavedSettings(Settings& saved) override {
    saved = settings;
    if (storageReady && preferences.isKey("settings")) {
      const String text = preferences.getString("settings");
      return !bootswitch::parseSettings(text.c_str(), text.length(), saved);
    }
    return true;
  }
  bool saveSettings(const char* text, size_t length) override {
    const String current = preferences.getString("settings");
    return current == text || preferences.putString("settings", text) == length;
  }
  void beginRecovery() override {
    // Finish the eject response, then remove USB long enough for hosts to
    // discard the old FAT/cache. Writes remain gated through the rebuild.
    delay(250);
    if (usbReady) tud_disconnect();
    delay(250);
  }
  void endRecovery() override { if (usbReady) tud_connect(); }
#if BOOTSWITCH_ENABLE_USB_UPDATE
  const char* stage(VirtualDisk::File file, const esp_partition_t*& staged) override {
    lastFirmwarePercent = -1;
    problem = "Validating firmware";
    drawScreen();
    return bootswitch::stageFirmware(file, staged, firmwareProgress, [] { delay(1); });
  }
  bool activate(const esp_partition_t* staged) override {
    return esp_ota_set_boot_partition(staged) == ESP_OK;
  }
#endif
};
DeviceUpdateBackend updateBackend;
bootswitch::UpdateCoordinator updates(virtualDisk, updateBackend, configText, sizeof(configText));

int32_t readDisk(uint32_t lba, uint32_t offset, void* buffer, uint32_t size) {
  if (!ramDiskReady) return BootDisk::read(lba, offset, buffer, size, selected.load());
  xSemaphoreTake(diskMutex, portMAX_DELAY);
  const int32_t result = virtualDisk.read(lba, offset, buffer, size, selected.load());
  xSemaphoreGive(diskMutex);
  return result;
}

int32_t writeDisk(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t size) {
  if (!ramDiskReady || !updates.writable()) {
    tud_msc_set_sense(0, SCSI_SENSE_DATA_PROTECT, 0x27, 0);
    return -1;
  }
  return updates.write(lba, offset, buffer, size);
}

bool startStopDisk(uint8_t, bool start, bool loadEject) {
  if (loadEject && !start && ramDiskReady) updates.requestApply();
  return true;
}

#if BOOTSWITCH_ENABLE_USB_UPDATE
void firmwareProgress(size_t written, size_t total) {
  const unsigned percent = written * 100u / total;
  const uint32_t now = millis();
  // Keep intermediate updates at 10 Hz; always show the first and final value.
  if (int(percent) != lastFirmwarePercent &&
      (lastFirmwarePercent < 0 || percent == 100 || uint32_t(now - lastFirmwareDraw) >= 100)) {
    lastFirmwarePercent = percent;
    screen.drawStatus(screenState());
    lastFirmwareDraw = now;
  }
  delay(1);  // Yield while USB and the idle watchdog remain active.
}
#endif

void applyConfiguration() {
  if (!ramDiskReady || restartPending != RestartAction::None || !updates.applyPending()) return;
  wakeDisplay();
  bootswitch::ApplyResult result;
  if (!updates.processApply(result)) return;
#if BOOTSWITCH_ENABLE_USB_UPDATE
  lastFirmwarePercent = -1;
#endif
  problem = result.error ? result.error :
      (result.firmware ? "Firmware ready; restarting" : "Config saved; restarting");
  redraw = true;
  if (result.restart) {
    restartAt = millis() + 500;
    restartPending = RestartAction::Application;
  }
}

#if BOOTSWITCH_ENABLE_REMOTE_FLASH
bool requestFlash() {
  if (restartPending != RestartAction::None || !bootswitch::boardCanEnterDownload()) return false;
  wakeDisplay();
  problem = "Entering flash mode";
  redraw = true;
  // Let the HTTP/ATT response reach the client before disconnecting USB/radios.
  restartAt = millis() + 500;
  restartPending = RestartAction::Flash;
  return true;
}
#endif

const char kPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>BootSwitch</title>
<style>body{font:18px system-ui;max-width:32em;margin:12vh auto;padding:1em;background:#14202b;color:#f2f5f8}button{font:inherit;padding:.7em 1em;margin:.3em .3em .3em 0;cursor:pointer}output{display:block;margin:1em 0;font-size:2em}p{line-height:1.5}</style>
<h1>BootSwitch</h1><p>Selected for the next boot:</p><output id="mode">Loading…</output>
<button data-mode="linux">Linux</button><button data-mode="windows">Windows</button><button data-mode="toggle">Toggle</button>
<p>This changes the saved selection. Start or restart your PC to use it.</p>
<button data-mode="wake">Wake PC from sleep</button><p id="status" role="status"></p>
<script>
const mode=document.querySelector('#mode'),status=document.querySelector('#status');
async function refresh(){try{const r=await fetch('/api/mode',{cache:'no-store'});if(!r.ok)throw Error('Read failed');const d=await r.json();mode.textContent=d.mode==='windows'?'Windows':'Linux';status.textContent=d.saved?'Selection saved':'Settings storage unavailable';}catch(e){status.textContent=e.message;}}
document.querySelectorAll('button').forEach(b=>b.onclick=async()=>{
  document.querySelectorAll('button').forEach(x=>x.disabled=true);
  try{
    const action=b.dataset.mode,modeCommand=action!=='toggle'&&action!=='wake';
    const r=await fetch(modeCommand?'/api/mode':'/api/'+action,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','X-Bootswitch':'1'},body:modeCommand?'mode='+action:''});
    const d=await r.json().catch(()=>({}));
    if(!r.ok)throw Error(d.error||'Request failed ('+r.status+')');
    await refresh();
    if(action==='wake')status.textContent='USB wake requested; host resume is not confirmed';
  }catch(e){status.textContent=e.message;}
  finally{document.querySelectorAll('button').forEach(x=>x.disabled=false);}
});
refresh();setInterval(()=>{if(!document.hidden)refresh();},3000);
document.addEventListener('visibilitychange',()=>{if(!document.hidden)refresh();});
</script></html>)HTML";

bool authenticate() {
  if (!apiEnabled || !settings.apiEnabled()) {
    server.send(503, "text/plain", "API disabled");
    return false;
  }
  const String authorization = server.header("Authorization");
  if (authorization == String("Bearer ") + settings.apiKey ||
      server.authenticate("admin", settings.apiKey)) return true;
  server.requestAuthentication(DIGEST_AUTH, "BootSwitch");
  return false;
}

void sendState() {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", String("{\"mode\":\"") +
      modeName(selected.load()) + "\",\"saved\":" +
      (storageReady ? "true}" : "false}"));
}

bool authorizeChange() {
  if (!authenticate()) return false;
  // Require a non-simple header for browser requests. No CORS permission is
  // granted, so third-party pages cannot submit authenticated mutations.
  if (server.header("X-Bootswitch") != "1") {
    server.send(403, "text/plain", "X-Bootswitch: 1 required");
    return false;
  }
  if (restartPending != RestartAction::None) {
    server.send(409, "text/plain", "Device restarting");
    return false;
  }
  return true;
}

void changeMode(Mode mode) {
  if (!setMode(mode)) {
    server.send(503, "text/plain", "Could not save selection");
    return;
  }
  sendState();
}

void startNetwork() {
  networkEnabled = settings.wifiEnabled() && !bootswitch::validateSettings(settings);
  apiEnabled = networkEnabled && settings.apiEnabled();
  if (!networkEnabled) {
    WiFi.mode(WIFI_OFF);
    return;
  }
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(settings.hostname);
  WiFi.setAutoReconnect(true);
  // Install credentials without connecting: the listen interval must be set
  // before association, and begin(ssid, password) would overwrite it.
  uint8_t bssid[6];
  const uint8_t* pinnedAp = settings.wifiBssidBytes(bssid) ? bssid : nullptr;
  // Scan channels automatically even when the AP is pinned: mesh nodes can
  // change channel without changing their BSSID.
  WiFi.begin(settings.ssid, settings.password, 0, pinnedAp, false);
  if (settings.wifiListenInterval != 0) {
    wifi_config_t config{};
    bool configured = esp_wifi_get_config(WIFI_IF_STA, &config) == ESP_OK;
    if (configured) {
      config.sta.listen_interval = settings.wifiListenInterval;
      configured = esp_wifi_set_config(WIFI_IF_STA, &config) == ESP_OK;
    }
    if (!configured || !WiFi.setSleep(WIFI_PS_MAX_MODEM)) {
      // Keep remote control available using the normal DTIM-based sleep mode.
      WiFi.setSleep(WIFI_PS_MIN_MODEM);
      problem = "Wi-Fi sleep setup failed";
    }
  }
  WiFi.begin();
  lastRetry = millis();
  // Wi-Fi can connect without exposing any HTTP listener when no key is set.
  if (!apiEnabled) return;
  const char* headers[] = {"Authorization", "X-Bootswitch"};
  server.collectHeaders(headers, 2);
  server.on("/", HTTP_GET, [] {
    if (!authenticate()) return;
    server.sendHeader("Cache-Control", "no-store");
    server.sendHeader("X-Content-Type-Options", "nosniff");
    server.sendHeader("X-Frame-Options", "DENY");
    server.send_P(200, "text/html; charset=utf-8", kPage);
  });
  server.on("/api/mode", HTTP_GET, [] {
    if (authenticate()) sendState();
  });
  server.on("/api/mode", HTTP_POST, [] {
    if (!authorizeChange()) return;
    const String value = server.arg("mode");
#if BOOTSWITCH_ENABLE_REMOTE_FLASH
    if (value == "flash") {
      server.sendHeader("Cache-Control", "no-store");
      if (requestFlash()) server.send(202, "application/json", "{\"flash_requested\":true}");
      else server.send(503, "application/json", "{\"error\":\"Flash control unavailable\"}");
      return;
    }
#endif
    if (value != "linux" && value != "windows") {
#if BOOTSWITCH_ENABLE_REMOTE_FLASH
      server.send(400, "text/plain", "mode must be linux, windows or flash");
#else
      server.send(400, "text/plain", "mode must be linux or windows");
#endif
      return;
    }
    changeMode(value == "windows" ? Mode::Windows : Mode::Linux);
  });
  server.on("/api/toggle", HTTP_POST, [] {
    if (authorizeChange()) changeMode(selected.load() == Mode::Linux ?
                                    Mode::Windows : Mode::Linux);
  });
  server.on("/api/usb", HTTP_GET, [] {
    if (!authenticate()) return;
    const auto status = bootswitch::usbWakeStatus();
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", String("{\"configured\":") +
        (status.configured ? "true" : "false") + ",\"suspended\":" +
        (status.suspended ? "true" : "false") + ",\"remote_wakeup_enabled\":" +
        (status.remoteWakeEnabled ? "true}" : "false}"));
  });
  server.on("/api/power", HTTP_GET, [] {
    if (!authenticate()) return;
    wifi_ps_type_t wifiSleep = WIFI_PS_NONE;
    const bool haveWifiSleep = esp_wifi_get_ps(&wifiSleep) == ESP_OK;
    wifi_ap_record_t ap{};
    const bool haveAp = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
    char apAddress[18] = {};
    if (haveAp) std::snprintf(apAddress, sizeof(apAddress), "%02x:%02x:%02x:%02x:%02x:%02x",
        ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5]);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", String("{\"cpu_mhz\":") + getCpuFrequencyMhz() +
        ",\"apb_hz\":" + getApbFrequency() +
        ",\"display_awake\":" + (screenAwake ? "true" : "false") +
        ",\"display_idle_ms\":" + uint32_t(millis() - lastDisplayActivity) +
        ",\"wifi_connected\":" + (WiFi.status() == WL_CONNECTED ? "true" : "false") +
        ",\"wifi_bssid\":" + (haveAp ? String("\"") + apAddress + "\"" : "null") +
        ",\"wifi_channel\":" + (haveAp ? String(ap.primary) : "null") +
        ",\"wifi_rssi_dbm\":" + (haveAp ? String(ap.rssi) : "null") +
        ",\"wifi_sleep\":" + (haveWifiSleep ? int(wifiSleep) : -1) +
        ",\"ble_active\":" + (bootswitch::bleActive() ? "true" : "false") +
        ",\"pmic_idle_sleep\":" + (powerControllerSleep ? "true" : "false") +
        ",\"battery_charging_enabled\":" + (batteryChargingEnabled < 0 ? "null" :
                                            (batteryChargingEnabled ? "true" : "false")) +
        ",\"ble_modem_sleep\":" + (bootswitch::bleActive() && bootswitch::bleModemSleepConfigured() ? "true" : "false") +
        ",\"ble_sleeping\":" + (bootswitch::bleActive() && esp_bt_controller_is_sleeping() ? "true}" : "false}"));
  });
  server.on("/api/wake", HTTP_POST, [] {
    if (!authorizeChange()) return;
    const auto result = wakeHost();
    server.sendHeader("Cache-Control", "no-store");
    switch (result) {
      case UsbWakeResult::Requested:
        server.send(200, "application/json", "{\"wake_requested\":true}");
        break;
      case UsbWakeResult::NotConfigured:
        server.send(503, "application/json", "{\"wake_requested\":false,\"error\":\"USB host not connected\"}");
        break;
      case UsbWakeResult::NotSuspended:
        server.send(409, "application/json", "{\"wake_requested\":false,\"error\":\"USB host not suspended\"}");
        break;
      case UsbWakeResult::NotEnabled:
        server.send(409, "application/json", "{\"wake_requested\":false,\"error\":\"Host has not armed USB wake\"}");
        break;
    }
  });
  server.onNotFound([] { server.send(404, "text/plain", "Not found"); });
  server.begin();
}

void startBluetooth() {
  if (!settings.bleEnabled()) return;
  if (!storageReady) { problem = "BLE settings unavailable"; return; }
  const bool newPasskey = preferences.getString("ble-passkey") != settings.blePasskey;
  const char* error = bootswitch::startBle(settings, selected.load(), newPasskey);
  if (!error && newPasskey && preferences.putString("ble-passkey", settings.blePasskey) != 6) {
    bootswitch::stopBle();
    error = "BLE settings save failed";
  }
  if (error) problem = error;
}

bool pollDisplayTelemetry(uint32_t now) {
  if (!displayTelemetry.pollDue(now, screenAwake)) return false;
  const bool connected = networkEnabled && WiFi.status() == WL_CONNECTED;
  return displayTelemetry.update(networkEnabled, connected,
      connected ? WiFi.RSSI() : 0,
      bootswitch::boardBatteryLevel(), bootswitch::boardHasBatteryTelemetry());
}

bootswitch::ScreenState screenState() {
  bootswitch::ScreenState state;
  state.selection = selected.load();
  state.status = problem;
#if BOOTSWITCH_ENABLE_USB_UPDATE
  state.firmwarePercent = lastFirmwarePercent;
#endif
  state.usbReady = usbReady;
  state.configDisk = ramDiskReady;
  state.apiEnabled = apiEnabled;
  state.bleEnabled = bootswitch::bleActive();
  state.telemetry = displayTelemetry.values();
  if (networkEnabled) {
    state.network = WiFi.status() == WL_CONNECTED ?
        bootswitch::ScreenNetworkState::Connected : bootswitch::ScreenNetworkState::Connecting;
    if (state.network == bootswitch::ScreenNetworkState::Connected)
      WiFi.localIP().toString().toCharArray(state.ipAddress, sizeof(state.ipAddress));
  }
  return state;
}

void drawScreen() {
  screen.draw(screenState());
  redraw = false;
}
}  // namespace

bool bootSwitchDiskWritable() { return ramDiskReady && updates.writable(); }

// A bus reset normally accompanies reboot/re-enumeration; resume also covers
// waking a suspended host. Neither event proves that the host actually rebooted.
// Do not wake on transfers: OS storage probes would keep the screen on forever.
extern "C" void IRAM_ATTR tud_event_hook_cb(uint8_t, uint32_t eventId, bool) {
  if (eventId == DCD_EVENT_BUS_RESET || eventId == DCD_EVENT_RESUME)
    usbWakeRequested.store(true, std::memory_order_relaxed);
}

void setup() {
  storageReady = preferences.begin("bootswitch", false);
  if (storageReady) {
    selected.store(preferences.getUChar("mode", 0) == 1 ?
                   Mode::Windows : Mode::Linux);
  } else problem = "Settings unavailable";
  loadSettings();

  // Enumerate USB before starting the display or network. Do not wait for Wi-Fi.
  disk.vendorID("BootSw");
  disk.productID("Boot selector");
  disk.productRevision("2.0");
  USB.productName(bootswitch::boardUsbName());
#if BOOTSWITCH_BOARD_TQT && BOOTSWITCH_ENABLE_REMOTE_FLASH
  // The SDK's built-in 1200-baud reset executes inside the TinyUSB control
  // callback. Defer it to loop(), just like API/BLE requests, so the transfer
  // completes before switching the shared PHY to USB Serial/JTAG.
  recoverySerial.enableReboot(false);
  recoverySerial.onEvent(ARDUINO_USB_CDC_LINE_CODING_EVENT,
      [](void*, esp_event_base_t, int32_t, void* eventData) {
        const auto* event = static_cast<arduino_usb_cdc_event_data_t*>(eventData);
        if (event && event->line_coding.bit_rate == 1200)
          cdcDownloadRequested.store(true, std::memory_order_relaxed);
      });
  recoverySerial.begin();
#endif
  disk.onRead(readDisk);
  disk.onWrite(writeDisk);
  disk.onStartStop(startStopDisk);
  disk.mediaPresent(true);
  usbReady = bootswitch::prepareUsbWake() && disk.begin(BootDisk::kSectorCount, BootDisk::kSectorSize);
  if (usbReady) usbReady = USB.begin();

  bootswitch::beginBoard();
  if (!screen.begin(bootswitch::boardDisplay())) problem = "Unsupported screen size";
  batteryChargingEnabled = bootswitch::boardChargingEnabled();
#if !BOOTSWITCH_BOARD_TQT
  if (batteryChargingEnabled < 0) problem = "Battery charge setup failed";
#endif
  powerControllerSleep = bootswitch::boardPowerSleep();
  startNetwork();
  startBluetooth();
  lastDisplayActivity = millis();
  pollDisplayTelemetry(lastDisplayActivity);
  drawScreen();
}

void loop() {
#if BOOTSWITCH_BOARD_TQT && BOOTSWITCH_ENABLE_REMOTE_FLASH
  if (cdcDownloadRequested.exchange(false, std::memory_order_relaxed)) requestFlash();
#endif
  bootswitch::updateBoard();
  if (usbWakeRequested.exchange(false, std::memory_order_relaxed)) wakeDisplay();
  if (restartPending != RestartAction::None) {
    if (int32_t(millis() - restartAt) >= 0) {
      if (restartPending == RestartAction::Application) ESP.restart();
#if BOOTSWITCH_ENABLE_REMOTE_FLASH
      else {
        // No NVS write: "flash" is an action, never a saved OS selection.
        const bool sent = bootswitch::boardEnterDownload();
        if (sent) delay(500);
        restartPending = RestartAction::None;
        problem = "Could not enter flash mode";
        redraw = true;
      }
#endif
    }
    if (restartPending != RestartAction::None) {
      if (apiEnabled) server.handleClient();
      if (redraw && screenAwake) drawScreen();
      delay(20);
      return;
    }
  }
  if (bootswitch::boardButtonB().wasPressed()) wakeDisplay();
  if (bootswitch::boardButtonB().wasClicked()) wakeHost();
  if (ramDiskReady && bootswitch::boardButtonB().wasHold()) updates.requestApply();
  applyConfiguration();
  if (bootswitch::boardButtonA().wasPressed()) {
    // A dark screen requires a separate wake press, avoiding a blind OS change.
    if (!screenAwake) wakeDisplay();
    else setMode(selected.load() == Mode::Linux ? Mode::Windows : Mode::Linux);
  }
  const BleCommand command = bootswitch::nextBleCommand();
#if BOOTSWITCH_ENABLE_REMOTE_FLASH
  if (command == BleCommand::Flash) {
    if (!requestFlash()) { problem = "Flash control unavailable"; redraw = true; }
  }
  else
#endif
  if (command == BleCommand::Wake) wakeHost();
  else if (command != BleCommand::None) {
    const Mode mode = command == BleCommand::Toggle ?
        (selected.load() == Mode::Linux ? Mode::Windows : Mode::Linux) :
        (command == BleCommand::Windows ? Mode::Windows : Mode::Linux);
    setMode(mode);
  }
  if (networkEnabled) {
    if (apiEnabled) server.handleClient();
    const bool connected = WiFi.status() == WL_CONNECTED;
    if (connected != lastConnected) {
      lastConnected = connected;
      if (mdnsStarted) MDNS.end();
      mdnsStarted = false;
      if (connected) {
        mdnsStarted = MDNS.begin(settings.hostname);
        if (mdnsStarted && apiEnabled) MDNS.addService("http", "tcp", 80);
      }
      redraw = true;
    }
    if (!connected && uint32_t(millis() - lastRetry) >= 30000) {
      lastRetry = millis();
      WiFi.reconnect();
    }
  }
  if (screenAwake && settings.screenTimeoutSeconds != 0 &&
      uint32_t(millis() - lastDisplayActivity) >= settings.screenTimeoutSeconds * 1000u) {
    bootswitch::boardDisplay().sleep();
    screenAwake = false;
  }
  const bool telemetryChanged = pollDisplayTelemetry(millis());
  if (redraw && screenAwake) drawScreen();
  else if (telemetryChanged && screenAwake) screen.drawTelemetry(displayTelemetry.values());
  // Button/UI/network polling at 50 Hz; TinyUSB has its own event-driven task.
  delay(20);
}
