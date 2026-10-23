# BootSwitch for M5Stack StickS3

Select Linux or Windows with the StickS3's **front A button**, a Wi-Fi web page,
an HTTP API, or optional Bluetooth LE. The display shows the saved choice.
Selection survives power loss and applies when your PC next reads the device
during boot. This does not restart or switch a running PC into another OS.
USB remote wake can resume a sleeping PC when the host supports and permits it.

Inspired by Stephen Holdaway's [Hardware boot selection switch](https://hackaday.io/project/179539-hardware-boot-selection-switch).
This is a new ESP32-S3 implementation with two PC integration options:

| Boot manager | Integration |
| --- | --- |
| **systemd-boot (preferred)** | A small x86-64 EFI bridge reads the device, sets `LoaderEntryOneShot`, then starts your existing systemd-boot. |
| GRUB | A configuration snippet reads the device directly. No EFI bridge needed. |

systemd-boot cannot execute the original project's GRUB script. Its configuration
lives on its own ESP/XBOOTLDR, so simply placing `loader.conf` on the StickS3 does
not change the PC's default. The bridge supplies the missing step using the
documented [Boot Loader Interface](https://systemd.io/BOOT_LOADER_INTERFACE/).

## Hardware

- An **M5Stack StickS3 (K150)** and a USB **data** cable to the target PC.
- No external switch, soldering, GPIO connections, or programmer.
- A 2.4 GHz Wi-Fi network for remote control.

The StickS3's ESP32-S3 has native USB; earlier ESP32-based StickC models cannot
run this firmware. See [M5Stack's StickS3 documentation](https://docs.m5stack.com/en/core/StickS3).
The USB port must initialize storage before the boot manager runs: disable UEFI
Fast Boot if necessary, and keep the internal boot manager first in boot order.
The StickS3 exposes a data disk, not an OS boot disk.

For Wi-Fi access while the PC is off, use a USB port that supplies standby power
(motherboard settings such as ErP may disable this), or a suitable powered USB hub
that retains power without backfeeding the host. The built-in 250 mAh battery is
only a temporary power source. Avoid ESP32 sleep modes: the selector needs USB and
Wi-Fi running. Do not splice a second power supply into the USB cable.

The CPU runs at **80 MHz**, retaining an
80 MHz APB clock for USB and SPI. The application polls buttons/network at 50 Hz;
TinyUSB runs independently in its own task. The unused external 5 V boost output
is disabled, so Grove/HAT 5 V and the IR circuitry are not powered by the Stick.
The unused PMIC power-key polling is disabled; front A and side B still work.
Telemetry polling stops while the screen is off. ESP32 light/deep sleep is not
used; USB remains connected. The firmware requests that the PM1 turn off the
status LED.

A battery-disconnected StickS3 measured a stable **0.132 W** with the display,
Wi-Fi, BLE and status LED off. This measurement does not cover operation with
either radio enabled.

## Build and flash the StickS3

Install [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html)
or use its VS Code extension. The toolchain and M5 libraries are pinned in
`platformio.ini`.

```sh
pio run
# Configure Wi-Fi and the API key through CONFIG.INI after flashing.
```

The local environment created during development can also be used:

```sh
PLATFORMIO_CORE_DIR="$PWD/.pio-core" .venv/bin/pio run
```

Connect the StickS3 and hold its side **reset/power** button for about two seconds
until the internal green LED blinks, then release. This is the board's download
mode ([M5Stack upload instructions](https://docs.m5stack.com/en/arduino/m5sticks3/program)).
Flash with:

```sh
pio run --target upload --upload-port /dev/ttyACM0
```

Substitute the download port shown on your machine. Reset afterward if it remains
in download mode. The application presents mass storage only, so the serial
upload port disappears during normal use. Re-enter download mode to flash again.
The side reset/power button is **not** the mode toggle; use the front A button.

### Optional development features

The default **sticks3** build disables remote download-mode entry. Set the
compile-time flag `BOOTSWITCH_ENABLE_REMOTE_FLASH=1` in `build_flags`, or use the
**sticks3-dev** environment to enable API `mode=flash` and BLE `flash`:

```sh
pio run --environment sticks3-dev
```

Its application is `.pio/build/sticks3-dev/firmware.bin`. This feature cannot be
enabled through CONFIG.INI. When disabled, authenticated API `mode=flash` is
rejected with HTTP 400 and BLE `flash` is ignored. The physical reset/power button
provides download mode in either build.

### Enter download mode remotely

Requires `BOOTSWITCH_ENABLE_REMOTE_FLASH=1` in the running firmware.

Send an authenticated `POST /api/mode` with `mode=flash` and `X-Bootswitch: 1`,
or write exact ASCII `flash` to the paired BLE command characteristic. For
example, with `BOOTSWITCH_TOKEN` set to your API key:

```sh
curl -X POST -H "Authorization: Bearer $BOOTSWITCH_TOKEN" \
  -H 'X-Bootswitch: 1' --data 'mode=flash' http://bootswitch.local/api/mode
```

HTTP returns **202** with `{"flash_requested":true}` before the Stick enters
download mode. This is a one-time action: the saved selection remains `linux`
or `windows`. The USB disk, HTTP server and BLE service disconnect, and the ROM
serial upload port appears. Flash using the serial workflow above, or briefly
press reset to return to the installed application. Eject/apply any pending USB
settings edits before requesting download mode; unsaved RAM-disk edits are lost.

Firmware builds contain no credentials and do not read a secrets header. On
first boot, Wi-Fi credentials and the API key are empty, so **Wi-Fi and HTTP/API
are disabled**. Configure them through `CONFIG.INI`; validated edits are retained
in NVS and restored into the file at startup. Existing USB-saved settings survive
firmware updates. No credentials or API keys are generated automatically.
BLE is disabled until you configure a `ble_passkey` through the USB disk.

## Edit settings over USB

Open **CONFIG.INI** on the **BOOTSWITCH** USB drive in a plain text editor:

```ini
wifi_ssid=
wifi_password=
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

- Fields may be omitted, but may not appear more than once. Missing Wi-Fi fields
  and API key default to empty; the default hostname is `bootswitch`. Emptying
  CONFIG.INI disables both services. A missing/deleted file is still an error.
- A blank or omitted `wifi_ssid` keeps Wi-Fi off. A blank or omitted `api_key`
  keeps the HTTP server and all web/API endpoints off, even when Wi-Fi connects.
  An empty key never permits anonymous access. To enable both, enter your Wi-Fi
  network and a key; `api_key` is also the web password for username `admin`.
- Values are literal text after `=`: do not add quotes or spaces around `=`.
  Spaces and `#` within a Wi-Fi value are preserved. Lines beginning with `#` or
  `;` are comments. LF, CRLF, and a UTF-8 BOM are accepted.
- SSID: at most 32 UTF-8 bytes. Wi-Fi password: empty for an open network, 8–63
  bytes for a passphrase, or exactly 64 hex digits for a raw WPA key. The password
  is unused while the SSID is empty. Button/USB selection works with Wi-Fi off.
- API key: empty to disable HTTP/API, otherwise 16–96 printable ASCII characters
  without whitespace. Hostname: 1–63 lowercase letters, digits and hyphens,
  without a leading or trailing hyphen.
- BLE: blank or omitted `ble_passkey` keeps Bluetooth off. Set exactly six digits
  (including leading zeros) to enable PIN-protected BLE independently of Wi-Fi
  and the API key. Use your own PIN; it is readable on this USB disk too.
- `screen_timeout`: seconds before the display sleeps. Defaults to `60` when
  omitted; `0` keeps it always on. Accepts whole numbers from 0 to 4294967.
- `wifi_listen_interval`: defaults to `0`, which uses modem sleep following the
  access point's DTIM interval. Values `1` through `10` select maximum modem sleep
  and specify how many AP beacon intervals to wait between receives. With a
  typical 102.4 ms beacon interval, `10` is approximately one second. Longer
  intervals than the AP's DTIM period can save idle power but add receive latency
  and may miss broadcast or multicast traffic, including `.local` discovery. Use
  a reserved IP address if discovery is unreliable, or return to `0`. Actual
  timing and savings depend on the AP and network traffic. See
  [Espressif's modem-sleep documentation](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-guides/wifi.html#station-sleep).
- CONFIG.INI may be at most 4096 bytes. Do not format the volume or change its boot
  files. Other files and editor backups exist only in RAM and disappear on restart.

Anyone who can access this USB disk can read and change its Wi-Fi password and
API key. The configuration file intentionally contains those values in plaintext.
Changing the key invalidates the previous web/API password after the restart.

## Button and Wi-Fi control

While the screen is on, press A once to toggle. When it is off, the first press
only wakes the screen; press again to toggle. The first boot defaults to Linux.
Each successful change is saved to NVS before it appears on USB or the display.
A save error is shown instead of acknowledging an unsaved change.

The top line also shows Wi-Fi signal strength in dBm and battery percentage.
Both are sampled once per second while the display is on; sampling stops when
it sleeps and resumes immediately when it wakes. These updates do not extend
the screen timeout, and only the small status area is repainted when a value
changes. Wi-Fi shows `off` when disabled and `--` while disconnected; unsupported
battery readings show `--`. Battery percentage is M5Unified's voltage-based
estimate; a physically disconnected battery may not be reliably detected.

The display sleeps after `screen_timeout` seconds (**60 by default**) without a
button press, a web/API or BLE mode command, or a USB reset/resume event. Set
`screen_timeout=0` in CONFIG.INI to keep it always on. Only the display sleeps: USB storage and
Wi-Fi continue working. USB reset/resume wakes it automatically, including during
normal host reboots and cable reconnection. These events also occur during some
sleep/wake transitions or USB resets; they are not definitive reboot detection.
A reboot that leaves the USB connection untouched cannot be detected this way.
Routine disk access, web-page polling, and Wi-Fi reconnection do not wake the
display or extend its timer. The side B user button also wakes it; holding B still
applies USB settings after flushing and unmounting the disk.
Briefly pressing and releasing B additionally requests that the PC wake from
sleep. Holding B does not issue this wake request.

Visit `http://bootswitch.local/` or the IP shown on screen. Sign in as **admin**
using the `api_key` from CONFIG.INI. The page offers Linux, Windows, and Toggle controls.
It polls the device so button changes appear too. If mDNS is unavailable, use
the displayed IP or reserve a DHCP address.

For scripts, use the same password as a bearer token:

```sh
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

## Wake the PC over USB

The device presents USB mass storage **and a passive HID keyboard interface**.
The keyboard interface lets the host OS offer its normal USB wake permissions.
Wake uses USB remote-resume signaling; the firmware does not send keyboard input.

Three controls request the same wake action, without changing the saved boot mode:

- Briefly press and release the **side B user button**, even with the display off.
  Holding B for 1.5 seconds still applies USB settings after flush/unmount.
- Use **Wake PC from sleep** on the web page, or send `POST /api/wake` with the
  same authentication and `X-Bootswitch: 1` header as a mode change.
- Write ASCII `wake` to the existing BLE command characteristic after pairing.

For example, with `BOOTSWITCH_TOKEN` set to your API key:

```sh
curl -X POST -H "Authorization: Bearer $BOOTSWITCH_TOKEN" \
  -H 'X-Bootswitch: 1' http://bootswitch.local/api/wake

curl -H "Authorization: Bearer $BOOTSWITCH_TOKEN" \
  http://bootswitch.local/api/usb
```

A successful request returns HTTP 200 and `{"wake_requested":true}`. This means
the USB resume signal was requested, not that the PC has confirmed waking.
HTTP 409 means the USB bus is not suspended or the host has not armed remote
wake; HTTP 503 means USB is not configured by a connected host. The display also
shows the result. GET `/api/usb` reports `configured`, `suspended`, and
`remote_wakeup_enabled`; the last field reflects the host permission reported at
the current suspend. Polling it does not wake the display or the PC.

USB suspend also happens during runtime power saving, so it does not prove that
the whole PC is asleep. Wake commands are explicit: choosing Linux/Windows or
toggling the saved mode never automatically wakes the PC.

The host must arm USB remote wake **before** suspending, and its firmware,
USB controller, and any intervening hub must permit USB wake. Enable USB keyboard
wake in UEFI/BIOS if needed. The feature targets sleep; shutdown and hibernation
wake support depend on the PC. See the [Linux USB power-management documentation](https://cdn.kernel.org/doc/html/latest/driver-api/usb/power-management.html)
and [Microsoft's HID power-management documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/power-management).

Wake permissions are configured separately in each OS; enabling them in Linux
does not enable them in Windows.

### Linux wake permission

Install the Python 3 helper and its udev rule from the repository directory:

```sh
sudo install -D -m 0755 host/bootswitch_usb_wake.py /usr/local/libexec/bootswitch-usb-wake
sudo install -m 0644 host/99-bootswitch-wakeup.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo /usr/local/libexec/bootswitch-usb-wake
```

The last command applies permissions immediately to connected BootSwitches;
reloading udev alone does not apply rules to an already-connected device. On
each later USB add/change event, including insertion, re-enumeration and boot,
the rule passes the application's current sysfs path to the helper. It matches
the application's USB product name and IDs, excluding the ROM download mode.

The helper resolves that device's **actual parent chain**, enabling supported
`power/wakeup` attributes on the StickS3, upstream hubs, root hub, and controller
ancestors. It discovers them again on each event, so bus numbers, USB ports and
hub depth can change without editing rules. Unsupported attributes are skipped.
Enabled ancestor permissions remain enabled after unplugging the StickS3.

Inspect the current chain without changing settings; this does not need root:

```sh
/usr/local/libexec/bootswitch-usb-wake --check
```

Each supported node should report `enabled`; `--check` exits nonzero if a
permission is disabled or no BootSwitch application is found. Verify after
rebooting too, since another power-management tool may override the settings.
If necessary, rerun `sudo /usr/local/libexec/bootswitch-usb-wake` while the PC is
awake, before suspending it.

The earlier `99-bootswitch-parent-wakeup.rules` example used fixed port names
and is superseded by this helper. If you installed it, remove that specific
file from `/etc/udev/rules.d/` and reload the rules; no generated files under
`build/` are needed for installation.

### Windows wake permission

1. Open **Device Manager → Keyboards**.
2. Find the StickS3's **HID Keyboard Device**. Unplug/reconnect the StickS3 to
   identify which entry disappears and returns; several keyboards may have the
   same display name.
3. Open that device's **Properties → Power Management**.
4. Enable **Allow this device to wake the computer**, then apply the change.

In an **administrator terminal**, verify that the keyboard is armed:

```powershell
powercfg /devicequery wake_armed
```

If it is absent, list devices whose wake permission Windows allows configuring:

```powershell
powercfg /devicequery wake_programmable
```

If the Power Management tab is missing or the device cannot be armed, inspect
the available sleep states with `powercfg /a` and check the PC's USB/firmware
wake support. See [Microsoft's powercfg reference](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/powercfg-command-line-options#devicequery).

### Test wake from sleep

After enabling the host's wake permissions, put the PC into **Sleep**, then
briefly press B or send BLE `wake` / `POST /api/wake` from another device.
The saved Linux/Windows selection should stay unchanged. GET `/api/usb` can
diagnose host permission while suspended: `remote_wakeup_enabled` should be
`true` once the host has armed wake and suspended the device. It is `false` while
the USB bus is active, even if the OS is configured to allow wake on its next
suspend. HTTP 200 confirms only that a wake signal was requested; confirm the
PC actually resumes. On Windows, `powercfg /lastwake` reports the most recent
wake source.

## Optional BLE control

Set `ble_passkey` in CONFIG.INI to your own six-digit PIN, save, and safely eject
as usual. Leave it empty to disable BLE. The device advertises its `hostname`
(default `bootswitch`; the BLE scan name is limited to 29 characters). BLE works
with Wi-Fi off. Control requires PIN-authenticated, encrypted pairing; phones
can retain their bond across device reboots. Changing the PIN clears the device's
old bonds on startup; also forget/unpair it on your phone before pairing again.

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
| `flash` (requires `BOOTSWITCH_ENABLE_REMOTE_FLASH=1`) | `66 6c 61 73 68` |

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
When enabled in the build, `flash` enters ROM download mode after acknowledging
the write; it disconnects BLE and USB storage without changing the saved mode.
BLE mode commands wake the screen and restart its configured timer; scanning,
connecting, and reading the state do not. For automation, explicit `linux` and
`windows` commands are safe to retry; `toggle` can toggle twice if repeated.

## systemd-boot setup (x86-64 UEFI)

The included bridge runs **before** systemd-boot. Install it on the **same ESP**
as `EFI/systemd/systemd-bootx64.efi`. Linux and Windows must already be bootable
entries in systemd-boot. Use `bootctl list` to see their exact IDs; the bridge
does not create OS entries or search other ESPs for Windows.

1. Install GNU-EFI and GNU build tools (`gnu-efi`, `gcc`, `binutils`, `make` on
   Arch; `gnu-efi`, `build-essential`, `binutils` on Debian/Ubuntu), then build:

   ```sh
   make -C efi
   ```

   Override `EFI_INC` and `EFI_LIB` if your distribution puts GNU-EFI elsewhere.
   The result is `efi/build/bootswitchx64.efi`.

2. Run `bootctl status`, `bootctl list`, and `lsblk -o NAME,PATH,FSTYPE,PARTUUID,MOUNTPOINTS`
   to identify the correct ESP and entry IDs. Copy the files, adjusting the
   example `/efi` mount point to your system:

   ```sh
   sudo install -d /efi/EFI/BootSwitch
   sudo install -m 0644 efi/build/bootswitchx64.efi /efi/EFI/BootSwitch/
   sudo install -m 0644 efi/bootswitch.conf.example /efi/EFI/BootSwitch/bootswitch.conf
   sudoedit /efi/EFI/BootSwitch/bootswitch.conf
   ```

   Set `linux=` to the ID from `bootctl list` (often a `.conf` filename or a UKI
   filename). Set `windows=` to its exact ID (often `auto-windows`). Values are
   ASCII, without quotes or spaces around `=`. Avoid kernel-version-specific
   IDs if your setup provides a stable entry; otherwise update the config when
   kernel updates change IDs. An unknown ID falls back according to systemd-boot's
   own selection rules. Leave `loader=` at the supplied systemd-boot path.

3. Add a separate firmware boot entry. **Examples only**: substitute your actual
   ESP disk and partition number; do not run these using a guessed device.

   ```sh
   sudo efibootmgr --create-only --disk /dev/nvme0n1 --part 1 \
     --label 'BootSwitch' --loader '\EFI\BootSwitch\bootswitchx64.efi'
   sudo efibootmgr -v
   ```

   Record the new BootSwitch ID and the original boot order. Test once with
   `sudo efibootmgr --bootnext NEW_ID` (four hex digits, e.g. `0007`), then reboot
   with the StickS3 attached. Test both selections. If both work, place BootSwitch
   first in the UEFI setup's boot order, retaining the original Linux Boot Manager
   entry as a recovery option. No existing boot files need to be overwritten.

**Secure Boot:** this project produces an unsigned EFI application. With Secure
Boot enabled, sign the bridge using a key already trusted by your firmware and
install that signed output instead. A machine that trusts only Microsoft's keys
will not automatically trust a self-signed application. Keep your existing Secure
Boot workflow; the bridge does not enroll keys or bypass verification. Changing
the boot chain can also trigger BitLocker/TPM recovery; have the recovery key
available before testing.

At each boot the bridge reads `MODE.TXT` from the one FAT volume labeled
`BOOTSWITCH`. It sets a UTF-16 `LoaderEntryOneShot` EFI variable, then starts the
existing systemd-boot, which consumes and deletes the variable. It uses the normal
non-volatile attributes expected by systemd-boot's deletion code. The StickS3's
own saved selection is persistent, so it is applied again on later boots.

An existing `LoaderEntryOneShot` from the OS takes precedence (for example,
`systemctl reboot --boot-loader-entry=…`, update, or recovery requests). With no
unique valid selector, the bridge waits up to about two seconds, then leaves
systemd-boot's usual selection in control. Missing/invalid bridge configuration
also leaves the usual selection in control. It does not execute code from USB.

To undo: choose the original Linux Boot Manager in firmware, restore the original
boot order, delete only the BootSwitch firmware entry with
`efibootmgr --bootnum NEW_ID --delete-bootnum`, and remove `EFI/BootSwitch` from the
ESP. Leave `EFI/systemd`, `EFI/Microsoft`, and the normal fallback loader alone.

## GRUB setup (alternative)

Edit `grub/01_bootswitch`: replace both `YOUR-…-MENUENTRY-ID` values with IDs from
your `/boot/grub/grub.cfg`. Stable menuentry IDs are preferable to numeric positions.
For a submenu use the GRUB `submenu-id>entry-id` form. Then:

```sh
sudo install -m 0755 grub/01_bootswitch /etc/grub.d/01_bootswitch
# Debian/Ubuntu:
sudo update-grub
# Arch, instead:
sudo grub-mkconfig -o /boot/grub/grub.cfg
```

Do not edit generated `grub.cfg` directly. The snippet runs after `00_header`,
finds filesystem UUID `B007-5E1E`, and sources `SWITCH.CFG`. Missing devices leave
your normal GRUB default intact. The hardware choice overrides GRUB's default;
you can still choose another entry in its menu. Unlike the EFI bridge, this simple
snippet also overrides GRUB's saved/next default when the device is found.
To undo, remove `/etc/grub.d/01_bootswitch` and regenerate the configuration.

## USB behavior and timing

The pinned Arduino ESP32 2.0.17 USB controller driver reports a bus suspend as
`DCD_EVENT_UNPLUGGED` and resets its FIFO allocator. This loses the configuration
and host wake permission, so a wake attempt incorrectly reports “USB host not
connected.” BootSwitch builds a local copy in `src/vendor/dcd_esp32sx.c` instead
of linking that driver from the framework archive. It reports suspend correctly,
preserves the FIFO allocation, and enables the existing resume interrupt handler.
Actual OTG session-end events still report a disconnect.

The MIT-licensed driver comes from
[esp32-arduino-lib-builder commit 3eb9cb0](https://github.com/espressif/esp32-arduino-lib-builder/blob/3eb9cb06afb534d37f47e98bca9f45e99126f81a/components/arduino_tinyusb/src/dcd_esp32sx.c).
Its license is retained in the file. The build rejects a different Arduino core
or TinyUSB minor version so this override is reviewed when dependencies change.
See also the [upstream suspend/wake report](https://github.com/espressif/arduino-esp32/issues/10831).

The device generates a **writable 4 MiB RAM disk** with an MBR and a valid FAT16
partition, label `BOOTSWITCH`, UUID `B007-5E1E`. The label and UUID are unchanged
from the original version, so the existing EFI bridge and GRUB snippet work
without changes. The disk uses 4 MiB of the StickS3's 8 MiB PSRAM and contains:

- `MODE.TXT`: `linux` or `windows`, padded to eight bytes including newline.
- `SWITCH.CFG`: `set bootswitch_mode=linux` or `…=windows`, with fixed file size.
- `CONFIG.INI`: editable Wi-Fi, API key, and hostname settings.

The two boot files have read-only attributes; their payloads are generated from
the selected mode on reads. Host writes go to a RAM image, which supports normal
FAT allocation and editor saves. Only a valid configuration is committed to NVS
after eject/apply. Filesystem damage, missing config, malformed settings and failed
NVS saves cannot replace the last working configuration. If PSRAM allocation
fails, USB falls back to the two-file read-only selector and the display reports
that the config disk is unavailable.

Both boot integrations read the selection once during boot: set it **before
powering on/restarting**. Changes made later apply on a subsequent boot. Host
firmware can cache reads; last-millisecond changes are not guaranteed. A mounted
OS may show stale boot-file contents; use the Wi-Fi API for live status.

## Verification

```sh
# C++ compiler, Python 3, dosfstools, mtools:
make test
# GNU-EFI, QEMU, OVMF, installed systemd-boot EFI binary:
python3 tests/check_efi.py
```

The native checks exercise full/partial/cross-sector reads and writes, settings
validation, corrupt FAT chains, and bounds under AddressSanitizer/UBSan. Real FAT
tools also test overwriting CONFIG.INI and replacing it via a temporary file and
rename, including a multi-cluster configuration. `fsck.fat` and `mtype` independently validate both generated
filesystems. If running under a ptrace-based sandbox, use
`ASAN_OPTIONS=detect_leaks=0 make test` (LeakSanitizer cannot run under ptrace).

The EFI integration test boots the **real systemd-boot** in QEMU/OVMF, with the
generated disk attached as USB mass storage. Test payloads report the selected
entry and confirm one-shot consumption. Cases cover Linux, Windows, missing and
duplicate devices, an existing OS one-shot, and missing configuration. Override
`EFI_PREFIX`, `OVMF_CODE`, `OVMF_VARS`, and `SYSTEMD_BOOT` for different paths.

Development validation: both the button-only and Wi-Fi-configured ESP32 builds
passed; both FAT disk images passed independent checks; all six EFI integration
cases passed with systemd-boot 262.1 and OVMF 202608. The Wi-Fi build used example
credentials for compilation only, without connecting to a network.

Native tests also cover USB wake permission checks, the minimum idle guard,
resume/disconnect races during that guard, and malformed BLE wake commands.
Compilation and emulation cannot verify the StickS3's physical USB enumeration,
buttons, Wi-Fi connectivity, display, battery operation, or your motherboard's
USB initialization. Those need a test on the actual hardware before relying on
the setup.

Hardware validation on 2026-10-22: a StickS3 was backed up and flashed. Linux
recognized the read-only `BOOTSWITCH` disk and read both selection files. The
front button changed Linux to Windows on the display and in uncached USB reads;
Windows remained selected after a hardware reset. Wi-Fi was tested separately
afterward: the device joined Wi-Fi, Digest login and
bearer authentication worked, invalid/unauthenticated changes were rejected,
and both explicit HTTP selection and toggling agreed with uncached USB reads.
The original Windows selection was restored after those tests. The EFI bridge
was then installed and successfully tested on the physical PC with systemd-boot
258.1, using Arch Linux for the Linux position and Memtest for the Windows
position. Actual Windows booting was not part of that hardware test.
The writable-disk firmware was also tested on the same StickS3: editor-style
replacement of CONFIG.INI, safe-eject/restart, API-key and hostname changes, old-key
rejection, Wi-Fi disablement, and provisioning the original network again entirely
through USB all passed. The original credentials were restored and the selected
boot mode was retained throughout.
Configuration-only defaults were verified on hardware as well: clearing the API
key left Wi-Fi connected (ping worked) with no HTTP listener; clearing the SSID
disabled Wi-Fi; and an empty CONFIG.INI survived restart with blank credentials
and key, leaving both services off. Original working settings were restored.
The full original firmware backup and verification record are in the ignored `build/backups/` directory;
keep the backup private because it may contain previous firmware settings.

To repeat the live Wi-Fi/USB checks, mount the USB volume read-only, then run
the following with the IP shown on the StickS3 and your actual mount path:

```sh
.venv/bin/python tests/check_device.py --host DEVICE_IP \
  --usb-mode-file "/run/media/$USER/BOOTSWITCH/MODE.TXT"
```

Add `--check-usb-wake` to verify the wake endpoints' authentication and refusal
while the USB bus is active. This leaves the boot mode unchanged; it does not
test waking a suspended PC, which needs a separate physical sleep/wake test.

This reads the current key and hostname from the mounted CONFIG.INI, temporarily
changes the mode, and restores it. You can also pass `--config-file PATH`; the
`BOOTSWITCH_API_KEY` can supply a key when the configuration file is unavailable.
The HTTP checks require an enabled API.
It requires Python `requests`; the workspace PlatformIO environment includes it.

To exercise USB configuration changes on a physical device, including temporary
API-key rotation and provisioning Wi-Fi again while it is disabled:

```sh
.venv/bin/python tests/check_usb_settings.py --host DEVICE_IP \
  --mount "/run/media/$USER/BOOTSWITCH" --offline-provisioning --disabled-services
```

The Linux/UDisks2 test saves a private recovery copy in `build/backups`, replaces
CONFIG.INI using an editor-style temporary file and rename, ejects, checks the
settings after restart, and restores the original configuration and boot mode.

## License

[MIT](LICENSE), copyright 2026 Ingmar Steen.
