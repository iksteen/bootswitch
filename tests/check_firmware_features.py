"""Verify production/development binaries and their packaged feature flags."""
import hashlib
import os
from pathlib import Path
import subprocess
import zipfile

core = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
nm = core / "packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-nm"
update_symbols = ("bootswitch::stageFirmware(", "bootswitch::VirtualDisk::firmware(",
                  "esp_ota_write", "esp_ota_set_boot_partition")
for environment, enabled in (("sticks3", False), ("sticks3-dev", True)):
    build = Path(".pio/build") / environment
    symbols = subprocess.check_output([str(nm), "-C", str(build / "firmware.elf")], text=True)
    image = (build / "firmware.bin").read_bytes()
    for symbol in update_symbols:
        assert (symbol in symbols) == enabled, f"{environment}: unexpected {symbol} availability"
    for marker in (b"Entering flash mode\0", b"FIRMWAREBIN"):
        assert (marker in image) == enabled, f"{environment}: unexpected development command/parser"
    print(f"{environment}: remote download entry and USB updater {'present' if enabled else 'absent'}.")

    output = Path("build/release")
    if environment != "sticks3":
        output /= environment
    stem = "bootswitch-" + environment
    with zipfile.ZipFile(output / f"{stem}-flash.zip") as bundle:
        info = dict(line.split("=", 1) for line in bundle.read("BUILD_INFO.txt").decode().splitlines())
        assert info == {"environment": environment, "BOOTSWITCH_ENABLE_REMOTE_FLASH": str(int(enabled)),
                        "BOOTSWITCH_ENABLE_USB_UPDATE": str(int(enabled)),
                        "BOOTSWITCH_DISABLE_BATTERY_CHARGING": str(int(enabled))}
        charge_message = (b"Battery charging is disabled at startup" if enabled else
                          b"Battery charging is enabled at startup")
        assert charge_message in bundle.read("README.txt")
        assert bundle.read("firmware.bin") == image
        assert (output / f"{stem}.bin").read_bytes() == image
        for line in bundle.read("SHA256SUMS").decode().splitlines():
            digest, name = line.split("  ", 1)
            assert hashlib.sha256(bundle.read(name)).hexdigest() == digest
    print(f"{environment}: release package matches the checked firmware and feature flags.")
