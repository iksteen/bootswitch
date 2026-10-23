# Firmware installation and updates

[Home](../README.md)

## Downloads and installation

Download prebuilt binaries from [GitHub Releases](https://github.com/iksteen/bootswitch/releases):

| Asset | Purpose |
| --- | --- |
| `bootswitchx64.efi` | x86-64 EFI bridge; see [PC boot setup](boot-setup.md). |
| `bootswitch-sticks3-flash.zip` | Complete production installation package; signed USB updates enabled, remote download mode disabled. |
| `bootswitch-sticks3.bin` | Production application image only. |
| `FIRMWARE.BIN` | Signed production USB update; copy directly to BOOTSWITCH. |
| `bootswitch-sticks3-dev-flash.zip` | Complete development installation package; remote flash and USB firmware updates enabled. |
| `bootswitch-sticks3-dev.bin` | Development application image only. |
| `SHA256SUMS` | SHA-256 hashes of all six release downloads. |

The development build disables battery charging for power measurements with the
battery connected. Production firmware enables charging at startup; see
[build flags](development.md#development-feature-flags).

Both flash ZIPs contain the bootloader, partition table, application, flash
arguments, instructions, build feature flags, and checksums. Application-only
images can be flashed at `0x10000` with the matching bootloader and partition
table installed. USB updates use the signed `FIRMWARE.BIN` asset in production;
development firmware also accepts raw application images.

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

Both standard builds enable USB updates. Production firmware requires a valid
signature from the BootSwitch ECDSA P-256 key embedded in the running application.
Download **FIRMWARE.BIN**, copy it to the root of **BOOTSWITCH**, then safely eject:

```sh
cp FIRMWARE.BIN "/run/media/$USER/BOOTSWITCH/FIRMWARE.BIN"
gio mount --eject "/run/media/$USER/BOOTSWITCH"
```

On Windows, copy FIRMWARE.BIN to BOOTSWITCH, then use **Safely Remove Hardware/Eject**.
The display shows update progress, and the Stick restarts after validation and
activation. The saved OS selection and settings are retained. Valid CONFIG.INI
edits can be applied in the same eject operation. As with settings-only saves,
flushing/unmounting and holding B for 1.5 seconds also applies the file.

Development firmware also accepts unsigned raw application images, such as
`bootswitch-sticks3-dev.bin` or `.pio/build/sticks3-dev/firmware.bin`. Recognized
signed images must have a valid signature in either build. A development image
requires physical download mode to install from production unless explicitly
signed by the trusted key holder.

The updater verifies the complete image and signed length before erasing or
writing flash. It removes the signature trailer, writes only the application to
the inactive slot, and selects that slot only after ESP-IDF validates the image.
Unsigned production updates, invalid signatures, incomplete files, corrupt FAT chains, incompatible
chips and invalid image checksums/digests are rejected. The error stays on the
display while the USB disk is rebuilt from the saved configuration and exposed
again through a brief USB reconnect. The rejected firmware, temporary files and
unsaved configuration edits are discarded. Copy a corrected update and eject to
retry without resetting the Stick. The RAM disk is also rebuilt at startup, so
FIRMWARE.BIN is not reapplied on later ejects.

Use the signed update `.bin` for production, or a raw application `.bin` for
development. ZIPs, EFI binaries and merged flash images are not USB updates.
USB-disk updates do not replace the bootloader or partition table; use
the complete flash package when those need updating. Image validation does not
test the new application's behavior; the physical reset/power button remains
available for recovery through ROM download mode. This is application-level
update authentication; hardware Secure Boot and eFuse changes are not enabled.
Physical download mode can still install arbitrary firmware. Previously signed
images remain acceptable: there is no anti-rollback counter. Anyone with write
access to a development unit's USB disk can install unsigned firmware.

Older production firmware that disables USB updates needs one physical flash
to install firmware supporting signed updates. See [signing your own builds](signing.md).

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
