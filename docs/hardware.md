# Hardware, power and USB behavior

[Home](../README.md)

- An **M5Stack StickS3 (K150)** or **LILYGO T-QT FN4R2**, and a USB **data** cable to the target PC.
- No external switch, soldering, GPIO connections, or programmer.
- A 2.4 GHz Wi-Fi network if using Wi-Fi remote control; BLE and physical control
  work without it.

The StickS3's ESP32-S3 has native USB; earlier ESP32-based StickC models cannot
run this firmware. See [M5Stack's StickS3 documentation](https://docs.m5stack.com/en/core/StickS3).
The USB port must initialize storage before the boot manager runs: disable UEFI
Fast Boot if necessary, and keep the internal boot manager first in boot order.
The StickS3 exposes a data disk, not an OS boot disk.

For Wi-Fi access while the PC is off, use a USB port that supplies standby power
(motherboard settings such as ErP may disable this), or a suitable powered USB hub
that retains power without backfeeding the host. The StickS3's built-in 250 mAh battery is
only a temporary power source; T-QT requires USB power. Do not splice a second power supply into the USB
cable.

## T-QT

The [LILYGO T-QT](https://github.com/Xinyuan-LilyGO/T-QT) target uses the
ESP32-S3 FN4R2 variant with **4 MiB flash and 2 MiB quad PSRAM**. Its native USB
port supports the same disk, HID wake, Wi-Fi and BLE protocols as the StickS3.
The EFI bridge and GRUB setup are shared between boards.

The 128×128 display uses the original GC9107 panel initialization from LILYGO's
`extras/old_panel` configuration. Newer panel revisions use different register
sequences and are not automatically detected. The left button (GPIO0) is A
(toggle/wake screen); the right button (GPIO47) is B (wake PC/apply settings).
The side button resets the MCU. LCD wiring is MOSI2, clock3, CS5, DC6, reset1,
and active-low backlight10.

T-QT has no battery or charger and requires USB power. The firmware does not
sample a battery ADC input or display a battery percentage on this board.
The charging diagnostic is `null`. There is no StickS3 PM1 controller.

Two **1.5 MiB application slots** support updates. The USB disk retains its
4 MiB logical size and FAT16 format, while a **1.75 MiB pool** stores written
sectors; untouched sectors are generated on demand. This fits the 2 MiB PSRAM
and accommodates an application image plus filesystem metadata. The host's
reported free disk space exceeds the write pool. A full pool returns a write
error; eject/apply or reset rebuilds the disk and releases its slots. It is a
configuration/update disk, not general storage.

Production and development images identify their board, and the updater rejects
images for the other board before erasing flash. Use the matching initial flash
package: the StickS3's memory configuration and partition map are incompatible.

## StickS3 power consumption

The CPU runs at **80 MHz**, retaining an 80 MHz APB clock for USB and SPI. The
application polls buttons/network at 50 Hz; TinyUSB runs independently in its
own task. The unused external 5 V boost output is disabled, so Grove/HAT 5 V and
the IR circuitry are not powered by the Stick.
The unused PMIC power-key polling is disabled; front A and side B still work.
Telemetry polling stops while the screen is off. ESP32 light/deep sleep is not
used; USB remains connected. The firmware requests that the PM1 turn off the
status LED.

The PM1 power controller uses five-second I²C idle sleep, preserving its power
outputs. Battery reads and download-mode commands wake it before accessing its
registers. It is also woken before board detection after an ESP reset. See
[M5Stack's PM1 sleep documentation](https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1#i2c-idle-sleep).

Production firmware enables battery charging at startup. The development build
sets `BOOTSWITCH_DISABLE_BATTERY_CHARGING=1` to exclude charging current from
power measurements on units with an intact battery. The battery remains
connected and available to power the Stick when USB power is removed. See
[build flags](development.md#development-feature-flags) for custom builds.

Wi-Fi [modem sleep](configuration.md#wi-fi-power-saving) is configured separately
and does not disconnect USB.

Bluetooth controller modem sleep uses the main crystal, independently of ESP32
CPU sleep. The pinned Arduino SDK disables it in the default controller
configuration; BootSwitch adapts the public initialization parameters through
the linker wrapper in [ble_power.cpp](../src/ble_power.cpp). A compile-time guard
requires review when changing ESP-IDF or the target. See
[Espressif's Bluetooth sleep configuration](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/kconfig.html#config-bt-ctrl-modem-sleep).

Measurements with an FNB-58 at the USB input on an intact StickS3, battery
connected but charging disabled, USB data connected, display asleep and status
LED off:

| Radio configuration | Average input power | HTTP response time |
| --- | --- | --- |
| Neither radio | **0.141 W** | — |
| BLE advertising, no client | **0.150 W** | — |
| BLE connected, idle | **0.193 W** | — |
| Wi-Fi only, default listen interval | **0.343–0.344 W** | **91–95 ms** |
| Wi-Fi only, listen interval 3 | **0.260 W** | **180 ms** |
| Wi-Fi only, listen interval 10 | **0.188 W** | **439 ms** |
| Wi-Fi associated and BLE advertising, default interval | **0.285 W** | Intermittent timeouts |
| Wi-Fi associated and BLE connected, default interval | **0.307 W** | Intermittent timeouts |

These are 60-second averages, or 120 seconds for connected BLE, at 80 MHz.
HTTP times are means of six requests after each quiet measurement; both default
Wi-Fi-only runs passed all six. The authenticated BLE read after each connected
idle measurement passed. The combined-radio cases showed Wi-Fi stalls, so their
lower consumption does not establish a usable power-saving configuration.
The stalls also occurred in comparison builds with Bluetooth modem sleep
disabled, with a 240 MHz CPU, with Bluetooth initialized before Wi-Fi, and with
the radio tasks on separate cores. They also reproduced after 75 seconds of
idle time with both Bluetooth and PM1 sleep disabled and the default NimBLE
advertising interval. An immediate post-startup request check does not establish
sustained responsiveness.
A Wi-Fi-sleep-disabled comparison rebooted before measurement; it provides no
valid power or link-stability result.
On a TP-Link Deco mesh, both test units exhibited timeouts on one radio and
passed all 12 post-idle probes each on another radio with the same power policy.
The original unit reproduced the timeouts when switched back while the reference
unit remained responsive. Access-point selection can therefore matter even with
a strong signal; see [BSSID selection](configuration.md#choosing-a-mesh-access-point).

With both radios enabled and the display continuously on, input power averaged
**0.331 W**; Wi-Fi timeouts also occurred in that configuration. Power depends on
network traffic, signal quality and the access point. These USB-input tests do
not measure battery discharge, usable capacity or runtime.

## USB disk and boot timing

The device generates a **writable 4 MiB RAM disk** with an MBR and a valid FAT16
partition, label `BOOTSWITCH`, UUID `B007-5E1E`. On StickS3, the disk uses 4 MiB of its 8 MiB PSRAM; T-QT uses the
sparse pool described above. It contains:

- `MODE.TXT`: `linux` or `windows`, padded to eight bytes including newline.
- `SWITCH.CFG`: `set bootswitch_mode=linux` or `…=windows`, with fixed file size.
- `CONFIG.INI`: [editable settings](configuration.md).
- `FIRMWARE.BIN`: [application update](firmware.md#usb-disk-updates) only in builds
  enabling USB updates.

The two boot files have read-only attributes; their payloads are generated from
the selected mode on reads. Host writes go to a RAM image, which supports normal
FAT allocation and editor saves. A valid configuration is committed to NVS and an
optional firmware update is installed after eject/apply when enabled in the
build. Filesystem damage, missing config, malformed settings and failed NVS saves
cannot replace the last working configuration. If PSRAM allocation fails, USB
falls back to the two-file
read-only selector and the display reports that the config disk is unavailable.

Both boot integrations read the selection once during boot: set it **before
powering on/restarting**. Changes made later apply on a subsequent boot. Host
firmware can cache reads; last-millisecond changes are not guaranteed. A mounted
OS may show stale boot-file contents; use the [Wi-Fi API](controls.md#wi-fi-and-http-api)
for live status.

## USB controller driver

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
