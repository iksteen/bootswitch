"""PlatformIO `release` target: package the exact upload images and offsets.

Separate images preserve NVS; a single padded image would erase saved settings.
"""
from pathlib import Path
import hashlib
import os
import shlex
import sys
import zipfile

Import("env")
sys.path.insert(0, str(Path(env.subst("$PROJECT_DIR")) / "scripts"))
from firmware_signing import sign_image


def package_firmware(source, target, env):
    environment = env.subst("$PIOENV")
    output = Path(env.subst("$PROJECT_DIR")) / "build/release"
    if environment != "sticks3":
        output /= environment
    stem = "bootswitch-" + environment
    update_name = "update-" + environment + ".bin"
    output.mkdir(parents=True, exist_ok=True)
    tqt = environment.startswith("tqt")
    features = {"BOOTSWITCH_ENABLE_REMOTE_FLASH": 0, "BOOTSWITCH_ENABLE_USB_UPDATE": 1,
                "BOOTSWITCH_ALLOW_UNSIGNED_FIRMWARE": 0,
                "BOOTSWITCH_DISABLE_BATTERY_CHARGING": 0}
    for definition in env.get("CPPDEFINES", []):
        if isinstance(definition, (tuple, list)) and definition[0] in features:
            features[definition[0]] = int(str(definition[1]), 0)
        elif isinstance(definition, str) and definition in features:
            features[definition] = 1
    firmware = Path(env.subst("$BUILD_DIR/${PROGNAME}.bin"))
    signing_key = os.environ.get("BOOTSWITCH_SIGNING_KEY_FILE")
    signed = (sign_image(firmware.read_bytes(), signing_key)
              if signing_key and not features["BOOTSWITCH_ALLOW_UNSIGNED_FIRMWARE"] else None)
    images = [(offset, Path(env.subst(str(path))))
              for offset, path in env["FLASH_EXTRA_IMAGES"]]
    images.append((env.subst("$ESP32_APP_OFFSET"), firmware))
    names = [path.name for _, path in images]
    if len(set(names)) != len(names):
        raise ValueError("Flash image names must be unique")

    args = ["--chip", env.BoardConfig().get("build.mcu"),
            "--after", "watchdog_reset", "write_flash", "-z",
            "--flash_mode", env.subst("${__get_board_flash_mode(__env__)}"),
            "--flash_freq", env.subst("${__get_board_f_image(__env__)}"),
            "--flash_size", env.BoardConfig().get("upload.flash_size")]
    contents = {}
    for offset, path in images:
        contents[path.name] = path.read_bytes()
        args.extend([str(offset), path.name])
    contents["flash_args"] = (shlex.join(args) + "\n").encode()
    contents["BUILD_INFO.txt"] = (f"environment={environment}\n" + "".join(
        f"{name}={value}\n" for name, value in features.items())).encode()
    remote_instructions = (
        'Remote download mode is enabled: authenticated POST /api/mode with\n'
        'mode=flash, or the paired BLE command "flash", enters ROM download mode.\n'
        if features["BOOTSWITCH_ENABLE_REMOTE_FLASH"] else
        'Remote download-mode commands are disabled in this firmware.\n')
    usb_instructions = (
        'USB-disk firmware updates are enabled. Copy the application firmware.bin\n'
        'to BOOTSWITCH as FIRMWARE.BIN, then safely eject to install and restart.\n'
        'Unsigned images are accepted in this development build.\n'
        'This preserves settings and does not update the bootloader/partition map.\n'
        if features["BOOTSWITCH_ALLOW_UNSIGNED_FIRMWARE"] else
        'USB-disk updates require a valid BootSwitch ECDSA signature. Copy\n'
        f'{update_name} to BOOTSWITCH as FIRMWARE.BIN,\n'
        'then safely eject to install. Raw firmware.bin is only for serial flashing.\n') if features["BOOTSWITCH_ENABLE_USB_UPDATE"] else (
        'USB-disk firmware updates are disabled. FIRMWARE.BIN is ignored.\n')
    charge_instructions = 'T-QT has no battery or charging support.\n' if tqt else (
        'Battery charging is disabled at startup for power measurements.\n'
        if features["BOOTSWITCH_DISABLE_BATTERY_CHARGING"] else
        'Battery charging is enabled at startup.\n')
    board_name = ("LILYGO T-QT FN4R2 (4 MiB flash / 2 MiB PSRAM)" if tqt else
                  "M5Stack StickS3 (8 MiB flash / 8 MiB PSRAM)")
    download_instructions = (
        "Hold the left button (GPIO0), press and release side reset, then release the left button."
        if tqt else
        "Hold the side reset/power button for about two seconds until the green LED blinks, then release.")
    serial_instructions = (
        "Development firmware exposes USB CDC: opening it at 1200 baud enters ROM download mode."
        if tqt and features["BOOTSWITCH_ENABLE_REMOTE_FLASH"] else
        "The application has no serial port. Enter download mode using the buttons.")
    contents["README.txt"] = f"""BootSwitch firmware for {board_name}

Build environment: {environment}. Feature flags are listed in BUILD_INFO.txt.

Extract this archive, then open a terminal in its directory.
Install the same esptool version used by the build (a Python virtualenv is fine):
  python -m pip install esptool==4.9.0

{download_instructions}
Flash all supplied images using the generated address list:
  python -m esptool --port /dev/ttyACM0 @flash_args
On Windows replace /dev/ttyACM0 with your COM port, for example COM5.

{serial_instructions}
The watchdog reset starts the application after flashing. If needed, briefly
press reset afterward. No erase_flash command is needed. Separate images leave
the NVS partition and its saved settings/selection intact with this partition map.
On a new device, provision CONFIG.INI through the BOOTSWITCH virtual USB disk.

firmware.bin is the application image at the offset listed in flash_args.
The separate {stem}.bin download is identical to firmware.bin; it
does not include the bootloader or partition table. Use this archive for initial
installation. Never flash the application image at address zero.

{remote_instructions}
{usb_instructions}
{charge_instructions}

SHA256SUMS contains hashes of the supplied images and instructions.
""".encode()
    contents["SHA256SUMS"] = "".join(
        f"{hashlib.sha256(data).hexdigest()}  {name}\n"
        for name, data in sorted(contents.items())
    ).encode()
    archive = output / f"{stem}-flash.zip"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as bundle:
        for name, data in sorted(contents.items()):
            # Fixed timestamps make the archive repeatable for the same inputs.
            entry = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            bundle.writestr(entry, data)
    (output / f"{stem}.bin").write_bytes(firmware.read_bytes())
    # Signed release filenames identify the board. FIRMWARE.BIN is only the
    # destination filename required by the virtual USB disk.
    for obsolete in ("FIRMWARE.BIN", f"firmware-{environment}.bin"):
        (output / obsolete).unlink(missing_ok=True)
    update = output / update_name
    if signed is not None:
        update.write_bytes(signed)
        print(f"Signed USB update {update}")
    else:
        # A previous signed build must not masquerade as this build's update.
        update.unlink(missing_ok=True)
    print(f"Packaged {archive}")


env.AddCustomTarget(
    "release", "$BUILD_DIR/${PROGNAME}.bin", package_firmware,
    title="Release firmware", description="Build application and package flash images"
)
