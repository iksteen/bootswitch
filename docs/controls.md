# Buttons, display and remote control

[Home](../README.md)

## Buttons and display

| Button | Action |
| --- | --- |
| Front **A**, screen on | Toggle Linux/Windows. |
| Front **A**, screen asleep | Wake the display; press again to toggle. |
| Side **B**, brief press and release | Wake the display and request [USB wake](usb-wake.md). |
| Side **B**, hold 1.5 seconds | Apply USB files after flushing and unmounting the disk; does not request PC wake. |
| Separate reset/power button | Press to reset; hold about two seconds for [download mode](firmware.md#download-mode). |

On T-QT, A is the left button (GPIO0), B is the right button (GPIO47),
and the side button resets the board. Its download-mode sequence is described
in [Firmware](firmware.md#download-mode).

The first boot defaults to Linux. Each successful selection is saved to NVS
before it appears on USB or the display. A failed save is shown as an error.

The telemetry area (top on StickS3, bottom on T-QT) shows Wi-Fi signal strength
in dBm. StickS3 also shows battery percentage; T-QT has no battery.
Readings are sampled once per second while the display is on; sampling stops when
it sleeps and resumes immediately when it wakes. These updates do not extend
the screen timeout, and only the small status area is repainted when a value
changes. Wi-Fi shows `off` when disabled and `--` while disconnected; unavailable
StickS3 battery readings show `--`. Battery percentage is a voltage-based
estimate; a physically disconnected battery may not be reliably detected.

The display sleeps after `screen_timeout` seconds (**60 by default**) without a
button press, a web/API or BLE mode command, or a USB reset/resume event. Set
`screen_timeout=0` in [CONFIG.INI](configuration.md) to keep it always on. USB
storage and Wi-Fi continue working while the display sleeps. USB reset/resume
wakes it automatically, including during normal host reboots and cable
reconnection. These events also occur during some
sleep/wake transitions or USB resets; they are not definitive reboot detection.
A reboot that leaves the USB connection untouched cannot be detected this way.
Routine disk access, web-page polling and Wi-Fi reconnection do not wake the
display or extend its timer.

## Wi-Fi and HTTP API

Visit `http://bootswitch.local/` or the IP shown on screen. Sign in as **admin**
using the `api_key` from CONFIG.INI. The page offers Linux, Windows, Toggle and
Wake PC controls. It polls while visible so button changes appear too. If mDNS
is unavailable, use the displayed IP or reserve a DHCP address.

| Method and path | Action |
| --- | --- |
| `GET /api/mode` | Read the saved `mode` and `saved` status. |
| `POST /api/mode`, `mode=linux` or `mode=windows` | Set the next boot selection. |
| `POST /api/toggle` | Toggle the selection. |
| `GET /api/usb` | Read USB configuration, suspend and wake-permission status. |
| `GET /api/power` | Read CPU, display and radio power-saving status. |
| `POST /api/wake` | Request [PC wake from sleep](usb-wake.md). |
| `POST /api/update` | Download and install the [latest signed firmware](firmware.md#wi-fi-updates) for this board. |
| `GET /api/update` | Read update `state`, `percent` and `error`; does not start an update. |
| `POST /api/mode`, `mode=flash` | Enter [download mode](firmware.md#enter-download-mode-remotely) when enabled in the build. |

For scripts, use the same password as a bearer token:

```bash
read -rs -p 'BootSwitch control password: ' BOOTSWITCH_TOKEN
echo
curl -H "Authorization: Bearer $BOOTSWITCH_TOKEN" http://bootswitch.local/api/mode

curl -X POST -H "Authorization: Bearer $BOOTSWITCH_TOKEN" \
  -H 'X-Bootswitch: 1' --data 'mode=windows' http://bootswitch.local/api/mode

curl -X POST -H "Authorization: Bearer $BOOTSWITCH_TOKEN" \
  -H 'X-Bootswitch: 1' http://bootswitch.local/api/toggle
unset BOOTSWITCH_TOKEN
```

Use explicit `mode=linux` / `mode=windows` for automation: retrying a toggle can
toggle twice. Mutations require POST and `X-Bootswitch: 1`; GET only reads state.
The browser uses HTTP Digest authentication. The API and page use HTTP on your
LAN; HTTP is unencrypted and bearer tokens can be observed on that network. Use
a trusted LAN or VPN, without forwarding this service to the public Internet.
Wi-Fi connects/reconnects independently of USB; no network is needed to boot.

`GET /api/power` requires the same authentication and does not wake the screen or
extend its timer. It returns `cpu_mhz`, `apb_hz`, `display_awake`,
`display_idle_ms`, `wifi_connected`, `wifi_sleep` (`0` none, `1` DTIM modem sleep,
`2` maximum modem sleep, `-1` unavailable), `ble_active`, `ble_modem_sleep`,
`ble_sleeping`, `pmic_idle_sleep`, and `battery_charging_enabled`. The modem/PMIC
configuration flags describe enabled power-saving modes; `ble_sleeping` is an
instantaneous controller sample. `battery_charging_enabled` is the charger-enable
state confirmed at startup, or `null` if unsupported or setup failed; it does not indicate
whether current is flowing into the battery. Reading the diagnostics does not
poll or wake the PM1.
The associated radio is identified by `wifi_bssid`, `wifi_channel` and
`wifi_rssi_dbm`; all three are `null` when no AP is associated.

## Bluetooth LE

Set `ble_passkey` in [CONFIG.INI](configuration.md) to your own six-digit PIN,
save and safely eject as usual. Leave it empty to disable BLE. The device
advertises its `hostname`
(default `bootswitch`; the BLE scan name is limited to 29 characters). BLE works
with Wi-Fi off. Control requires PIN-authenticated, encrypted pairing; phones
can retain their bond across device reboots. Changing the PIN clears the device's
old bonds on startup; also forget/unpair it on your phone before pairing again.

Advertising uses a 500–750 ms interval. Bluetooth controller modem sleep reduces
idle power between radio events; USB remains available.

In [BLESPloit's GATT browser](https://blesplo.it/docs/mobile/connections/):

1. Scan for `bootswitch` (or your hostname), then use the **link/manual connection**
   control and open **Device Services**.
2. Find service `7e6e0001-bb6e-4b08-8e8a-fc9f1b6a4450`.
3. Read the **state** characteristic ending in `0002` below. If prompted, pair
   using the PIN you put in CONFIG.INI. If necessary, pair in your phone's
   Bluetooth settings first, then reconnect in BLESPloit.
4. Write one of these exact lowercase UTF-8/ASCII values to the **command**
   characteristic ending in `0003`. Use a normal write **with response**.

| Command | Hex bytes (if the app uses a hex editor) |
| --- | --- |
| `toggle` | `74 6f 67 67 6c 65` |
| `linux` | `6c 69 6e 75 78` |
| `windows` | `77 69 6e 64 6f 77 73` |
| `wake` | `77 61 6b 65` |
| `update` | `75 70 64 61 74 65` |
| `flash` (development feature) | `66 6c 61 73 68` |

| Characteristic | UUID | Access |
| --- | --- | --- |
| Saved mode | `7e6e0002-bb6e-4b08-8e8a-fc9f1b6a4450` | Read / notify; returns `linux` or `windows` |
| Command | `7e6e0003-bb6e-4b08-8e8a-fc9f1b6a4450` | Write with response; accepts the commands above |

Do not include quotes, a newline, or a trailing NUL. Invalid commands are ignored.
Commands enter a bounded eight-command queue; excess writes are ignored. The BLE
write response acknowledges receipt, before the firmware saves the selection.
Read the state characteristic after processing or subscribe to its notifications
to confirm the saved result. Physical buttons and Wi-Fi changes update it too.
The `wake` command requests USB wake without changing the saved mode; its outcome
appears on the display, and USB permission can be inspected with GET `/api/usb`.
The `update` command downloads the latest signed production firmware over an
already-connected Wi-Fi network; see [Wi-Fi updates](firmware.md#wi-fi-updates).
It wakes the display without changing the saved boot selection. BLE itself does
not carry the firmware. Its write response acknowledges the queued command;
progress and errors appear on the display and through `GET /api/update`.
When [enabled in the build](development.md#development-feature-flags), `flash`
enters ROM download mode after acknowledging the write; it disconnects BLE and
USB storage without changing the saved mode.
BLE mode commands wake the screen and restart its configured timer; scanning,
connecting, and reading the state do not. For automation, explicit `linux` and
`windows` commands are safe to retry; `toggle` can toggle twice if repeated.
