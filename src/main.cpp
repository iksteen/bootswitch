#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <USB.h>
#include <USBMSC.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <freertos/semphr.h>
#include "tusb.h"
#include "device/dcd.h"
#include "boot_disk.h"
#include "ble_control.h"
#include "board_power.h"
#include "settings.h"
#include "virtual_disk.h"
#include "usb_wake.h"
#include "display_telemetry.h"
#include "firmware_update.h"

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
Preferences preferences;
WebServer server(80);
Settings settings;
uint8_t* diskBytes = nullptr;
VirtualDisk virtualDisk(nullptr, 0);
SemaphoreHandle_t diskMutex = nullptr;
bool ramDiskReady = false;
char configText[VirtualDisk::kMaxConfigSize + 1];
std::atomic<bool> applyRequested{false};
std::atomic<bool> diskEjected{false};
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
#if BOOTSWITCH_ENABLE_USB_UPDATE
char firmwareStatus[40];
#endif
void drawScreen();

void wakeDisplay() {
  lastDisplayActivity = millis();
  if (!screenAwake) {
    screenAwake = true;
    displayTelemetry.invalidate();
    M5.Display.wakeup();
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
  diskBytes = static_cast<uint8_t*>(heap_caps_malloc(VirtualDisk::kCapacity,
                                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (diskMutex && diskBytes) {
    virtualDisk = VirtualDisk(diskBytes, VirtualDisk::kCapacity);
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

int32_t readDisk(uint32_t lba, uint32_t offset, void* buffer, uint32_t size) {
  if (!ramDiskReady) return BootDisk::read(lba, offset, buffer, size, selected.load());
  xSemaphoreTake(diskMutex, portMAX_DELAY);
  const int32_t result = virtualDisk.read(lba, offset, buffer, size, selected.load());
  xSemaphoreGive(diskMutex);
  return result;
}

int32_t writeDisk(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t size) {
  if (!ramDiskReady || diskEjected.load()) {
    tud_msc_set_sense(0, SCSI_SENSE_DATA_PROTECT, 0x27, 0);
    return -1;
  }
  xSemaphoreTake(diskMutex, portMAX_DELAY);
  const int32_t result = diskEjected.load() ? -1 : virtualDisk.write(lba, offset, buffer, size);
  xSemaphoreGive(diskMutex);
  return result;
}

bool startStopDisk(uint8_t, bool start, bool loadEject) {
  if (loadEject && !start && ramDiskReady) {
    // Host has flushed its filesystem before issuing the eject command. Keep
    // parsing, NVS writes, and restart work out of the USB callback/task.
    diskEjected.store(true);
    disk.mediaPresent(false);
    applyRequested.store(true);
  }
  return true;
}

#if BOOTSWITCH_ENABLE_USB_UPDATE
void firmwareProgress(size_t written, size_t total) {
  const unsigned percent = written * 100u / total;
  char status[sizeof(firmwareStatus)];
  std::snprintf(status, sizeof(status), "Updating firmware: %u%%", percent);
  if (std::strcmp(status, firmwareStatus)) {
    std::strcpy(firmwareStatus, status);
    problem = firmwareStatus;
    drawScreen();
  }
  delay(1);  // Yield while USB and the idle watchdog remain active.
}
#endif

void applyConfiguration() {
  if (!ramDiskReady || restartPending != RestartAction::None) return;
  wakeDisplay();
  diskEjected.store(true);
  disk.mediaPresent(false);
  size_t length = 0;
#if BOOTSWITCH_ENABLE_USB_UPDATE
  VirtualDisk::File firmware;
#endif
  xSemaphoreTake(diskMutex, portMAX_DELAY);
  const char* error = virtualDisk.configuration(configText, sizeof(configText), length);
#if BOOTSWITCH_ENABLE_USB_UPDATE
  if (!error) error = virtualDisk.firmware(firmware);
#endif
  xSemaphoreGive(diskMutex);
  Settings candidate;
  if (!error) error = bootswitch::parseSettings(configText, length, candidate);
  if (!error && !storageReady) error = "Settings unavailable";
#if BOOTSWITCH_ENABLE_USB_UPDATE
  const esp_partition_t* staged = nullptr;
  if (!error && firmware) {
    problem = "Validating firmware";
    drawScreen();
    error = bootswitch::stageFirmware(firmware, staged, firmwareProgress);
  }
#endif
  if (!error) {
    // Canonicalize before storing; comments, temp files and other host data
    // never reach flash. One NVS value replaces all fields atomically.
    bootswitch::formatSettings(candidate, configText, sizeof(configText), length);
    const String current = preferences.getString("settings");
    if (current != configText && preferences.putString("settings", configText) != length)
      error = "Settings save failed";
  }
#if BOOTSWITCH_ENABLE_USB_UPDATE
  if (!error && staged && esp_ota_set_boot_partition(staged) != ESP_OK)
    error = "Firmware activation failed";
  problem = error ? error : (staged ? "Firmware ready; restarting" : "Config saved; restarting");
#else
  problem = error ? error : "Config saved; restarting";
#endif
  redraw = true;
  if (!error) {
    restartAt = millis() + 500;
    restartPending = RestartAction::Application;
  }
}

#if BOOTSWITCH_ENABLE_REMOTE_FLASH
bool requestFlash() {
  if (restartPending != RestartAction::None || !M5.Power.M5pm1.isEnabled()) return false;
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
      bootswitch::preparePowerAccess() ? M5.Power.getBatteryLevel() : -1);
}

void drawDisplayTelemetry() {
  auto& display = M5.Display;
  display.fillRect(80, 4, display.width() - 88, 12, TFT_BLACK);
  display.setTextSize(1);
  display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  display.drawRightString(displayTelemetry.text(), display.width() - 8, 5);
}

void drawScreen() {
  auto& display = M5.Display;
  display.fillScreen(TFT_BLACK);
  display.setCursor(8, 5);
  display.setTextSize(1);
  display.setTextColor(TFT_LIGHTGREY);
  display.println("NEXT BOOT");
  display.setCursor(8, 26);
  display.setTextSize(3);
  display.setTextColor(selected.load() == Mode::Windows ? TFT_CYAN : TFT_GREEN);
  display.println(selected.load() == Mode::Windows ? "Windows" : "Linux");
  display.setTextSize(1);
  display.setTextColor(TFT_WHITE);
  display.setCursor(8, 62);
  display.println(problem ? problem : "A: toggle  B: wake PC");
  display.setCursor(8, 80);
  if (!usbReady) display.println("USB init failed");
  else display.println(ramDiskReady ? "CONFIG.INI: eject to apply" : "USB: boot selector");
  display.setCursor(8, 98);
  if (!networkEnabled) display.println("Wi-Fi: off");
  else if (WiFi.status() == WL_CONNECTED) {
    display.print(WiFi.localIP());
    if (!apiEnabled) display.print(" API off");
  }
  else display.println("Wi-Fi: connecting...");
  display.setCursor(8, 116);
  display.println(bootswitch::bleActive() ? "BLE: on (PIN pairing)" : "BLE: off");
  drawDisplayTelemetry();
  redraw = false;
}
}  // namespace

bool bootSwitchDiskWritable() { return ramDiskReady && !diskEjected.load(); }

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
  USB.productName("BootSwitch StickS3");
  disk.onRead(readDisk);
  disk.onWrite(writeDisk);
  disk.onStartStop(startStopDisk);
  disk.mediaPresent(true);
  usbReady = bootswitch::prepareUsbWake() && disk.begin(BootDisk::kSectorCount, BootDisk::kSectorSize);
  if (usbReady) usbReady = USB.begin();

  auto config = M5.config();
  config.internal_imu = false;
  config.internal_rtc = false;
  config.internal_spk = false;
  config.internal_mic = false;
  // No Grove/HAT accessories or IR: don't keep the external 5 V boost enabled.
  config.output_power = false;
  // A/B use direct GPIOs. The application never consumes the PMIC power key.
  config.pmic_button = false;
  bootswitch::preparePowerController();
  M5.begin(config);
  // StickS3's status LED belongs to the PM1, not Power.setLed().
  M5.Power.M5pm1.setLedEnLevel(false);
  // PM1 retains CHG_EN across ESP resets. Explicitly apply both policies so
  // switching from a measurement build to production restores charging.
  constexpr bool wantCharging = !BOOTSWITCH_DISABLE_BATTERY_CHARGING;
  bool actualCharging = false;
  if (M5.Power.M5pm1.isEnabled() &&
      M5.Power.M5pm1.setBatteryCharge(wantCharging) &&
      M5.Power.M5pm1.getBatteryCharge(&actualCharging) && actualCharging == wantCharging) {
    batteryChargingEnabled = actualCharging ? 1 : 0;
  } else problem = "Battery charge setup failed";
  powerControllerSleep = bootswitch::enablePowerControllerSleep();
  M5.BtnB.setHoldThresh(1500);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(60);
  startNetwork();
  startBluetooth();
  lastDisplayActivity = millis();
  pollDisplayTelemetry(lastDisplayActivity);
  drawScreen();
}

void loop() {
  M5.update();
  if (usbWakeRequested.exchange(false, std::memory_order_relaxed)) wakeDisplay();
  if (restartPending != RestartAction::None) {
    if (int32_t(millis() - restartAt) >= 0) {
      if (restartPending == RestartAction::Application) ESP.restart();
#if BOOTSWITCH_ENABLE_REMOTE_FLASH
      else {
        // M5PM1 SYS_CMD: key 0xA + command 3 = download, like the reset key hold.
        // No NVS write: "flash" is an action, never a saved OS selection.
        const bool sent = bootswitch::preparePowerAccess() &&
                          M5.Power.M5pm1.writeRegister8(0x0C, 0xA3);
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
  if (M5.BtnB.wasPressed()) wakeDisplay();
  if (M5.BtnB.wasClicked()) wakeHost();
  if (applyRequested.exchange(false) || M5.BtnB.wasHold()) applyConfiguration();
  if (M5.BtnA.wasPressed()) {
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
    M5.Display.sleep();
    screenAwake = false;
  }
  const bool telemetryChanged = pollDisplayTelemetry(millis());
  if (redraw && screenAwake) drawScreen();
  else if (telemetryChanged && screenAwake) drawDisplayTelemetry();
  // Button/UI/network polling at 50 Hz; TinyUSB has its own event-driven task.
  delay(20);
}
