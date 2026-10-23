# Configuration

[Home](../README.md)

On first boot, **Wi-Fi, HTTP/API and BLE are disabled**. Credentials and keys
come only from `CONFIG.INI`; none are compiled in or generated automatically.
Settings are saved in NVS after validation and restored into the file at startup.
Saved settings survive firmware updates.

## CONFIG.INI

Open **CONFIG.INI** on the **BOOTSWITCH** USB drive in a plain text editor:

```ini
wifi_ssid=
wifi_password=
wifi_bssid=
api_key=
hostname=bootswitch
ble_passkey=
screen_timeout=60
wifi_listen_interval=0
```

Save and close the file, then **safely eject** the USB drive. The StickS3 validates
the complete file, saves all settings together, and restarts to apply them.
Its selected OS is retained. The disk reappears with the saved settings. Ordinary
saves and editors that replace files using a temporary file/rename are supported.
On Linux, use **Eject**, rather than just Unmount or Power Off; for example:

```sh
gio mount --eject "/run/media/$USER/BOOTSWITCH"
```

If your tool cannot eject the device, flush and unmount the volume first, then
hold the **side B user button** for 1.5 seconds to apply. This is separate from the
reset/power button. Do not restart or unplug before applying: unsaved session
changes are discarded. If validation fails, the display shows an error and the
previous network settings keep working. Briefly press reset to restore the last
saved CONFIG.INI, then edit it again.

## Settings reference

| Key | Default | Meaning and accepted values |
| --- | --- | --- |
| `wifi_ssid` | Empty | Empty disables Wi-Fi; otherwise up to 32 UTF-8 bytes. |
| `wifi_password` | Empty | Empty for an open network; otherwise 8–63 bytes or exactly 64 hex digits for a raw WPA key. Unused with Wi-Fi off. |
| `wifi_bssid` | Empty | Optional access-point MAC, e.g. `02:11:22:33:44:55`. Empty allows automatic AP selection; a value pins one radio for the configured SSID. |
| `api_key` | Empty | Empty disables HTTP/API; otherwise 16–96 printable ASCII characters without whitespace. Also the web password for user `admin`. |
| `hostname` | `bootswitch` | 1–63 lowercase letters, digits or hyphens; no leading or trailing hyphen. |
| `ble_passkey` | Empty | Empty disables BLE; otherwise exactly six digits, including any leading zeros. |
| `screen_timeout` | `60` | Seconds before the display sleeps, from 0 to 4294967. `0` keeps it on. |
| `wifi_listen_interval` | `0` | `0` follows the AP's DTIM; `1`–`10` selects a receive interval in AP beacons. See below. |

Wi-Fi can connect with HTTP/API disabled. An empty API key never permits
anonymous access. BLE is independent of Wi-Fi and the API key. Button and USB
boot selection work with both radios off. See [Controls](controls.md) for login,
pairing and commands.

## Choosing a mesh access point

`wifi_bssid` can pin a stationary Stick to one radio in a mesh network. Use the
radio's BSSID, which can differ from the node's printed hardware MAC. The
authenticated `/api/power` response reports the current `wifi_bssid`,
`wifi_channel` and `wifi_rssi_dbm` to identify the active radio.

With a pin set, the Stick scans for that BSSID across channels, so an AP channel
change does not require editing the setting. It will not fail over to a different
BSSID if the selected radio is unavailable. Clear `wifi_bssid` to restore automatic
selection. Use distinct hostnames when running several BootSwitch units.

## Wi-Fi power saving

`wifi_listen_interval=0` uses modem sleep following the access point's DTIM
interval; it does not disable Wi-Fi modem sleep. Values `1` through `10`
select maximum modem sleep and specify how
many AP beacon intervals to wait between receives. With a typical 102.4 ms
beacon interval, `10` is approximately one second.

Intervals longer than the AP's DTIM period can save idle power but add receive
latency and may miss broadcast or multicast traffic, including `.local`
discovery. Use a reserved IP address if discovery is unreliable, or return to
`0`. Actual timing and savings depend on the AP and network traffic. See
[Espressif's modem-sleep documentation](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-guides/wifi.html#station-sleep).

## File format and persistence

- Values are literal text after `=`: do not add quotes or spaces around `=`.
  Spaces and `#` within a Wi-Fi value are preserved. Lines beginning with `#` or
  `;` are comments. LF, CRLF and a UTF-8 BOM are accepted.
- Keys may be omitted but cannot appear more than once. Omitted keys take their
  defaults. Emptying CONFIG.INI disables Wi-Fi, HTTP/API and BLE; deleting the
  file is an error.
- CONFIG.INI may be at most 4096 bytes. Do not format the volume or change its
  boot files. Other files and editor backups exist only in RAM and disappear on
  restart, except that [FIRMWARE.BIN](firmware.md#usb-disk-updates) can install an
  update when enabled in the running build.

Anyone who can access this USB disk can read and change its Wi-Fi password and
API key. The configuration file intentionally contains those values in plaintext.
Changing the key invalidates the previous web/API password after the restart.
