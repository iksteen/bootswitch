# Firmware installation and updates

[Home](../README.md)

## Downloads and installation

Download prebuilt binaries from [GitHub Releases](https://github.com/iksteen/bootswitch/releases):

| Asset | Purpose |
| --- | --- |
| `bootswitchx64.efi` | x86-64 EFI bridge; see [PC boot setup](boot-setup.md). |
| `bootswitch-sticks3-flash.zip` | Complete production installation package; signed USB updates enabled, remote download mode disabled. |
| `bootswitch-sticks3.bin` | Production application image only. |
| `update-sticks3.bin` | Signed StickS3 production update; copy to BOOTSWITCH as `UPDATE.BIN`. |
| `bootswitch-sticks3-dev-flash.zip` | Complete development installation package; remote flash and USB firmware updates enabled. |
| `bootswitch-sticks3-dev.bin` | Development application image only. |
| `bootswitch-tqt-flash.zip` | Complete T-QT production installation package. |
| `bootswitch-tqt.bin` | T-QT production application only. |
| `update-tqt.bin` | Signed T-QT production update; copy to BOOTSWITCH as `UPDATE.BIN`. |
| `bootswitch-tqt-dev-flash.zip` | Complete T-QT development installation package. |
| `bootswitch-tqt-dev.bin` | T-QT development application only. |
| `SHA256SUMS` | SHA-256 hashes of the release downloads. |

The StickS3 development build disables battery charging for power measurements with the
battery connected. StickS3 production firmware enables charging at startup; see
[build flags](development.md#development-feature-flags).

The flash ZIPs contain the bootloader, partition table, application, flash
arguments, instructions, build feature flags, and checksums. Application-only
images can be flashed at `0x10000` with the matching bootloader and partition
table installed. Production USB updates use the signed `update-<board>.bin`
asset; development firmware also accepts raw application images. Signed updates
are direct binary downloads with stable board-specific names, without a ZIP
wrapper. The `bootswitch-<board>.bin` images are raw serial-flash applications.
Current firmware expects `UPDATE.BIN` on the USB disk; see the older-firmware
upgrade instructions below.

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

**T-QT:** hold the left button (GPIO0), press and release the side reset
button, then release the left button. Its development firmware also exposes a
USB CDC serial port: opening that port at **1200 baud** enters ROM download mode.
This host-controlled recovery port is absent in production firmware. API/BLE
remote entry is available in the development build on both boards.

**StickS3:**

Connect the StickS3 and hold its side **reset/power** button for about two seconds
until the internal green LED blinks, then release. This is the board's download
mode ([M5Stack upload instructions](https://docs.m5stack.com/en/arduino/m5sticks3/program)).

The serial upload port disappears during normal use. The side reset/power button
is separate from the front A boot-selection button.

## Wi-Fi updates

With Wi-Fi connected, request the latest production release through authenticated
`POST /api/update`, or send the paired BLE command `update`:

```sh
curl -X POST -H "Authorization: Bearer $BOOTSWITCH_TOKEN" \
  -H 'X-Bootswitch: 1' http://bootswitch.local/api/update
curl -H "Authorization: Bearer $BOOTSWITCH_TOKEN" http://bootswitch.local/api/update
```

The POST returns `202` after queuing the update. GET reports `state` (`idle`,
`queued`, `downloading`, `validating`, `installing`, `restarting` or `error`),
`percent` (or `null`) and `error` (or `null`). The display wakes and shows progress.
HTTP control requests during an update are rejected with `409`; status reads remain
available between network/flash operations. A successful update restarts the
device. BLE can request an update even when the HTTP API is disabled.

BootSwitch uses GitHub's [latest release download link](https://docs.github.com/en/repositories/releasing-projects-on-github/linking-to-releases)
for `iksteen/bootswitch`, selecting `update-sticks3.bin` or `update-tqt.bin`.
Downloads use verified HTTPS, including redirects; clock synchronization is
requested when necessary for certificate validation. Outbound HTTPS and access
to `time.cloudflare.com` or `pool.ntp.org` for initial time synchronization must
be available. There is no periodic update check or automatic installation.

Both production and development firmware require the BootSwitch signature for
network downloads and check board compatibility before writing flash. This path
always installs production firmware, including when requested from a development
build. A request downloads the latest release even if it is already installed;
there is no version comparison or anti-rollback counter.

The download reuses the USB disk's RAM. USB disk access is unavailable during
the update; saved settings and boot selection are retained, while unapplied disk
edits are discarded. A missing release asset, interrupted download, invalid
signature or activation failure rebuilds the disk from saved settings and
reconnects USB. The error remains on screen and in `GET /api/update`; the command
can be retried without a reset. No partial download is activated.

Network updates are available when `BOOTSWITCH_ENABLE_USB_UPDATE=1`, sharing the
same storage and updater as USB updates. When disabled, the HTTP update routes
are absent and BLE `update` is ignored.

## USB-disk updates

Production and development builds enable USB updates. Use an image for the
matching board; wrong-board images are rejected. Production firmware requires a valid
signature from the BootSwitch ECDSA P-256 key embedded in the running application.
Download **update-sticks3.bin** for StickS3 or **update-tqt.bin** for T-QT.
Copy it to the root of **BOOTSWITCH** with destination name **UPDATE.BIN**,
then safely eject. For example, on StickS3:

```sh
cp update-sticks3.bin "/run/media/$USER/BOOTSWITCH/UPDATE.BIN"
gio mount --eject "/run/media/$USER/BOOTSWITCH"
```

On Windows, copy the downloaded binary to BOOTSWITCH, rename it to
**UPDATE.BIN**, then use **Safely Remove Hardware/Eject**.

If the device's currently installed firmware still expects `FIRMWARE.BIN`, copy
this upgrade under that name once. Once installed, use `UPDATE.BIN` for future
updates; new firmware does not treat `FIRMWARE.BIN` as an update request.

The display shows update progress, and the Stick restarts after validation and
activation. The saved OS selection and settings are retained. Valid CONFIG.INI
edits can be applied in the same eject operation. As with settings-only saves,
flushing/unmounting and holding B for 1.5 seconds also applies the file.

Development firmware also accepts unsigned raw application images, such as
`bootswitch-sticks3-dev.bin` or `bootswitch-tqt-dev.bin`, depending on the
board. Recognized
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
UPDATE.BIN is not reapplied on later ejects.

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
