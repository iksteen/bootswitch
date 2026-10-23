# Build, test and publish

[Home](../README.md)

Run commands from the repository root.

## Build firmware

Install [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html)
or use its VS Code extension. The toolchain and M5 libraries are pinned in
[platformio.ini](../platformio.ini).

```sh
pio run
# Configure Wi-Fi and the API key through CONFIG.INI after flashing.
```

To build the same firmware downloads as CI, run `pio run --environment sticks3
--target release`. The application binary and flash ZIP are written to
`build/release/`.

For a serial upload from source, put the Stick in [download mode](firmware.md#download-mode),
then run `pio run --environment sticks3 --target upload --upload-port /dev/ttyACM0`.
Use `--environment sticks3-dev` for the development build.

## Development feature flags

The default **sticks3** build enables signed USB updates and battery charging,
and disables remote download-mode entry. Development builds accept unsigned
USB updates as well.
These compile-time options are independent and cannot be changed through CONFIG.INI:

| Build flag | Default | Effect when `1` |
| --- | --- | --- |
| `BOOTSWITCH_ENABLE_REMOTE_FLASH` | `0` | API `mode=flash` and BLE `flash` |
| `BOOTSWITCH_ENABLE_USB_UPDATE` | `1` | Installing FIRMWARE.BIN on eject/apply |
| `BOOTSWITCH_ALLOW_UNSIGNED_FIRMWARE` | `0` | Accept raw unsigned USB updates as well as valid signed updates |
| `BOOTSWITCH_DISABLE_BATTERY_CHARGING` | `0` | Disable battery charging for power measurements |

Set a flag to `1` in a custom PlatformIO environment's `build_flags` to apply
its listed behavior. The **sticks3-dev** environment sets all four to `1`,
including disabling battery charging:

```sh
pio run --environment sticks3-dev
pio run --environment sticks3-dev --target release
```

Its application is `.pio/build/sticks3-dev/firmware.bin`. Development release
files are named `bootswitch-sticks3-dev.bin` / `bootswitch-sticks3-dev-flash.zip`
under `build/release/sticks3-dev/`, separate from production artifacts. Each ZIP
includes BUILD_INFO.txt with its feature flags. CI checks and publishes both
variants under their distinct filenames.

Each build explicitly applies its charging policy at startup and reads it back
from the PM1. Installing production firmware after a measurement build restores
charging. The authenticated [power diagnostics](controls.md#wi-fi-and-http-api)
report the confirmed startup state.

With remote flash disabled, authenticated API `mode=flash` is rejected with HTTP
400 and BLE `flash` is ignored. With USB updates disabled, FIRMWARE.BIN is an
ordinary RAM-disk file: it is ignored on eject and discarded at restart. Normal
CONFIG.INI saves still work. The physical reset/power button provides download
mode in either build.

## CI and releases

[GitHub Actions](https://github.com/iksteen/bootswitch/actions/workflows/build.yml)
builds EFI and both firmware variants on branch pushes, pull requests and manual
runs. The EFI job runs the native tests and boots the EFI bridge with
systemd-boot in QEMU/OVMF; the firmware job builds and packages the pinned
production and development
environments. Successful jobs upload separate EFI, production firmware and
development firmware artifacts, retained for 30 days.

Pushing a version tag such as `v0.1.0` runs those same builds and tests, then
publishes a GitHub Release with the EFI binary, both firmware variants and their
flash ZIPs and checksums. Tags containing a hyphen, such as `v0.1.0-rc.1`,
produce prereleases. Publishing uses the repository's automatic `GITHUB_TOKEN`.
The publish job uses the protected **firmware-signing** environment and waits for
its required approval. It requires that environment's **FIRMWARE_SIGNING_KEY**
secret and adds `FIRMWARE.BIN`, signed against the committed public key. Branch
and PR builds never receive the key. See [firmware signing](signing.md).
Only the publish job has release write permission. All action dependencies are
pinned to commit SHAs, with weekly Dependabot checks for updates. See the
[build](../.github/workflows/build.yml) and
[release](../.github/workflows/release.yml) workflows.

## Verification

```sh
# C++ compiler, Python 3, mbedTLS 2.x headers/library, dosfstools, mtools:
python -m pip install -r requirements-signing.txt
make test
# Optional separate data-race instrumentation for the coordinator:
make test-thread-sanitizer
# GNU-EFI, QEMU, OVMF, installed systemd-boot EFI binary:
python3 tests/check_efi.py
# Production package, development firmware, and compiled feature checks:
pio run --environment sticks3 --target release
pio run --environment sticks3-dev --target release
python3 tests/check_firmware_features.py
```

The native checks exercise full/partial/cross-sector reads and writes, settings
validation, corrupt FAT chains, and bounds under AddressSanitizer/UBSan. Real FAT
tools also test overwriting CONFIG.INI and replacing it via a temporary file and
rename, including a multi-cluster configuration. `fsck.fat` and `mtype`
independently validate both generated filesystems. If running under a
ptrace-based sandbox, use
`ASAN_OPTIONS=detect_leaks=0 make test` (LeakSanitizer cannot run under ptrace).

BLE command tests cover remote flash both enabled and disabled. Firmware feature
checks verify that both binaries contain the USB updater and public key, that
production requires signatures, and that only development enables remote flash.
Signing tests use disposable keys and real mbedTLS verification in both policies,
covering wrong keys, unsigned images, altered content/metadata/signatures,
truncation, extra bytes, fragmented FAT files and signature-trailer stripping.
The same update coordinator used by the firmware runs in native multithreaded
tests with real signature verification and simulated USB/NVS/OTA backends.
Condition-variable checkpoints force admitted and queued writes to race eject,
pause after authentication, and hold recovery before formatting and publication.
The tests use no scheduling sleeps and run under ASan/UBSan in both build policies.
CI includes them through `make test`; `make test-thread-sanitizer` runs the same
coordinator schedules separately with ThreadSanitizer.
Injected failures cover OTA begin/write/end, image verification, settings saves,
activation and disk recovery, including a corrected signed upload after failure.
Valid settings saved before an activation failure remain committed; recovery
rebuilds from those saved settings. Partial physical flash writes and power loss
remain hardware-test concerns.
Bluetooth power-adapter tests check initialization defaults, preservation of
explicit caller settings, and propagation of controller errors. The live HTTP
checks also verify authenticated power diagnostics and the CPU/APB clocks.

The EFI integration test boots the **real systemd-boot** in QEMU/OVMF, with the
generated disk attached as USB mass storage. Test payloads report the selected
entry and confirm one-shot consumption. Cases cover Linux, Windows, missing and
duplicate devices, an existing OS one-shot, and missing configuration. Override
`EFI_PREFIX`, `OVMF_CODE`, `OVMF_VARS`, and `SYSTEMD_BOOT` for different paths.

Native tests also cover USB wake permission checks, the minimum idle guard,
resume/disconnect races during that guard, and malformed BLE wake commands.
Compilation and emulation cannot verify the StickS3's physical USB enumeration,
buttons, Wi-Fi connectivity, display, battery operation, or your motherboard's
USB initialization. Those need a test on the actual hardware before relying on
the setup.

## Physical-device checks

To repeat the live Wi-Fi/USB checks, mount the USB volume read-only, then run
the following with the IP shown on the StickS3 and your actual mount path:

```sh
python3 tests/check_device.py --host DEVICE_IP \
  --usb-mode-file "/run/media/$USER/BOOTSWITCH/MODE.TXT"
```

Add `--check-usb-wake` to verify the wake endpoints' authentication and refusal
while the USB bus is active. This leaves the boot mode unchanged; it does not
test waking a suspended PC, which needs a separate physical sleep/wake test.

This reads the current key and hostname from the mounted CONFIG.INI, temporarily
changes the mode, and restores it. You can also pass `--config-file PATH`; the
`BOOTSWITCH_API_KEY` can supply a key when the configuration file is unavailable.
The HTTP checks require an enabled API and Python `requests`.

To exercise USB configuration changes on a physical device, including temporary
API-key rotation and provisioning Wi-Fi again while it is disabled:

```sh
python3 tests/check_usb_settings.py --host DEVICE_IP \
  --mount "/run/media/$USER/BOOTSWITCH" --offline-provisioning --disabled-services
```

The Linux/UDisks2 test saves a private recovery copy in `build/backups`, replaces
CONFIG.INI using an editor-style temporary file and rename, ejects, checks the
settings after restart, and restores the original configuration and boot mode.
