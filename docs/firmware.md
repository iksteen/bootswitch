# Firmware installation and updates

[Home](../README.md)

## Downloads and installation

Download prebuilt binaries from [GitHub Releases](https://github.com/iksteen/bootswitch/releases):

| Asset | Purpose |
| --- | --- |
| `bootswitchx64.efi` | x86-64 EFI bridge; see [PC boot setup](boot-setup.md). |
| `bootswitch-sticks3-flash.zip` | Complete production installation package; remote flash and USB firmware updates disabled. |
| `bootswitch-sticks3.bin` | Production application image only. |
| `bootswitch-sticks3-dev-flash.zip` | Complete development installation package; remote flash and USB firmware updates enabled. |
| `bootswitch-sticks3-dev.bin` | Development application image only. |
| `SHA256SUMS` | SHA-256 hashes of all five release downloads. |

Both flash ZIPs contain the bootloader, partition table, application, flash
arguments, instructions, build feature flags, and checksums. Application-only
images can be flashed at `0x10000` with the matching bootloader and partition
table installed, or installed through the USB disk when the running firmware
enables that feature.

To install the firmware, extract the flash ZIP and open a terminal in its
directory. Install `esptool==4.9.0` in a Python environment, put the StickS3 in
[download mode](#download-mode), then run:

```sh
python -m pip install esptool==4.9.0
python -m esptool --port /dev/ttyACM0 @flash_args
```

Replace the port with the one on your machine (`COM5`, for example, on Windows).
The supplied arguments use a watchdog reset to start the application afterward.
The package writes separate images at the build's actual upload offsets, leaving
the NVS settings partition intact. Do not erase the flash before updating and do
not flash the application-only binary at address zero.

For a source build, see [Development](development.md#build-firmware). After the
first installation, [configure the Stick](configuration.md) and
[set up the PC's boot manager](boot-setup.md). No credentials are included in
either firmware variant.

## Download mode

Connect the StickS3 and hold its side **reset/power** button for about two seconds
until the internal green LED blinks, then release. This is the board's download
mode ([M5Stack upload instructions](https://docs.m5stack.com/en/arduino/m5sticks3/program)).

The serial upload port disappears during normal use. The side reset/power button
is separate from the front A boot-selection button.

## USB-disk updates

Requires `BOOTSWITCH_ENABLE_USB_UPDATE=1` in the **running** firmware; see
[development feature flags](development.md#development-feature-flags). Copy the
raw application binary (`bootswitch-sticks3.bin` from a release, or
`.pio/build/sticks3/firmware.bin` from a local build) to the root of **BOOTSWITCH**
as **FIRMWARE.BIN**, then safely eject the drive:

```sh
cp bootswitch-sticks3.bin "/run/media/$USER/BOOTSWITCH/FIRMWARE.BIN"
gio mount --eject "/run/media/$USER/BOOTSWITCH"
```

On Windows, copy and rename the file, then use **Safely Remove Hardware/Eject**.
The display shows update progress, and the Stick restarts after validation and
activation. The saved OS selection and settings are retained. Valid CONFIG.INI
edits can be applied in the same eject operation. As with settings-only saves,
flushing/unmounting and holding B for 1.5 seconds also applies the file.

The updater writes the inactive application slot and selects it only after
ESP-IDF validates the image. Incomplete files, corrupt FAT chains, incompatible
chips and invalid image checksums/digests are rejected. An error appears on the
display; reset the Stick to restore the USB disk before trying again. The RAM
disk is rebuilt at startup, so FIRMWARE.BIN is not reapplied on later ejects.

Use the application `.bin`, rather than the ZIP, EFI binary or a merged flash
image. USB-disk updates do not replace the bootloader or partition table; use
the complete flash package when those need updating. Image validation does not
test the new application's behavior; the physical reset/power button remains
available for recovery through the ROM download mode. Anyone with write access
to this USB disk can install firmware while this development feature is enabled.
Installing the default production image disables subsequent USB-disk updates.

## Enter download mode remotely

Requires `BOOTSWITCH_ENABLE_REMOTE_FLASH=1` in the running firmware. Use the
[API key](controls.md#wi-fi-and-http-api) as a bearer token, or use a paired BLE
connection.

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
serial upload port appears. Flash using the
[serial installation workflow](#downloads-and-installation), or briefly press
reset to return to the installed application. Eject/apply any pending USB
settings edits before requesting download mode; unsaved RAM-disk edits are lost.
