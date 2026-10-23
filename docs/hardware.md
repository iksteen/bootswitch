# Hardware, power and USB behavior

[Home](../README.md)

- An **M5Stack StickS3 (K150)** and a USB **data** cable to the target PC.
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
that retains power without backfeeding the host. The built-in 250 mAh battery is
only a temporary power source. Do not splice a second power supply into the USB
cable.

## Power consumption

The CPU runs at **80 MHz**, retaining an 80 MHz APB clock for USB and SPI. The
application polls buttons/network at 50 Hz; TinyUSB runs independently in its
own task. The unused external 5 V boost output is disabled, so Grove/HAT 5 V and
the IR circuitry are not powered by the Stick.
The unused PMIC power-key polling is disabled; front A and side B still work.
Telemetry polling stops while the screen is off. ESP32 light/deep sleep is not
used; USB remains connected. The firmware requests that the PM1 turn off the
status LED.

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

A battery-disconnected StickS3 measured a stable **0.132 W** with the display,
Wi-Fi, BLE and status LED off. This measurement does not cover operation with
either radio enabled.

## USB disk and boot timing

The device generates a **writable 4 MiB RAM disk** with an MBR and a valid FAT16
partition, label `BOOTSWITCH`, UUID `B007-5E1E`. The disk uses 4 MiB of the
StickS3's 8 MiB PSRAM and contains:

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
