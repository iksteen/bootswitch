"""Verify production/development binaries and their packaged feature flags."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import zipfile
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from firmware_signing import public_der, verify_image, image_target

core = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
nm = core / "packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-nm"
update_symbols = ("bootswitch::stageFirmware(", "bootswitch::VirtualDisk::firmware(",
                  "esp_ota_write", "esp_ota_set_boot_partition")
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--board", choices=("sticks3", "tqt"))
args = parser.parse_args()
boards = [args.board] if args.board else ["sticks3", "tqt"]
for environment, development in ((board + suffix, bool(suffix)) for board in boards for suffix in ("", "-dev")):
    tqt = environment.startswith("tqt")
    build = Path(".pio/build") / environment
    symbols = subprocess.check_output([str(nm), "-C", str(build / "firmware.elf")], text=True)
    image = (build / "firmware.bin").read_bytes()
    assert image[288:296] == b"BSWBRD01"
    assert image_target(image) == int(tqt)
    assert ("(anonymous namespace)::recoverySerial" in symbols) == (tqt and development)
    if tqt and development:
        assert "USBCDC::enableReboot(bool)" in symbols
        assert "(anonymous namespace)::cdcDownloadRequested" in symbols
    for symbol in update_symbols:
        assert symbol in symbols, f"{environment}: missing {symbol}"
    assert (b"Entering flash mode\0" in image) == development
    assert b"FIRMWAREBIN" in image
    assert (b"Firmware signature required\0" in image) != development
    assert public_der() in image
    print(f"{environment}: USB updater present; unsigned images {'allowed' if development else 'rejected'}.")

    output = Path("build/release")
    if environment != "sticks3":
        output /= environment
    stem = "bootswitch-" + environment
    with zipfile.ZipFile(output / f"{stem}-flash.zip") as bundle:
        info = dict(line.split("=", 1) for line in bundle.read("BUILD_INFO.txt").decode().splitlines())
        assert info == {"environment": environment, "BOOTSWITCH_ENABLE_REMOTE_FLASH": str(int(development)),
                        "BOOTSWITCH_ENABLE_USB_UPDATE": "1",
                        "BOOTSWITCH_ALLOW_UNSIGNED_FIRMWARE": str(int(development)),
                        "BOOTSWITCH_DISABLE_BATTERY_CHARGING": str(int(development and not tqt))}
        charge_message = (b"T-QT has no battery or charging support" if tqt else
                          b"Battery charging is disabled at startup" if development else
                          b"Battery charging is enabled at startup")
        assert charge_message in bundle.read("README.txt")
        assert (b"--flash_size 4MB" if tqt else b"--flash_size 8MB") in bundle.read("flash_args")
        assert bundle.read("firmware.bin") == image
        assert (output / f"{stem}.bin").read_bytes() == image
        for line in bundle.read("SHA256SUMS").decode().splitlines():
            digest, name = line.split("  ", 1)
            assert hashlib.sha256(bundle.read(name)).hexdigest() == digest
        if not development:
            assert b"USB-disk updates require a valid BootSwitch ECDSA signature" in bundle.read("README.txt")
            assert f"update-{environment}.bin".encode() in bundle.read("README.txt")
    for obsolete in ("FIRMWARE.BIN", f"firmware-{environment}.bin"):
        assert not (output / obsolete).exists(), "Obsolete release filename must not linger"
    update = output / f"update-{environment}.bin"
    if update.exists():
        assert not development, "Development builds must not be signed automatically"
        assert verify_image(update.read_bytes()) == image
    print(f"{environment}: release package matches the checked firmware and feature flags.")
