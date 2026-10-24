#!/usr/bin/python3
"""Enable wake on a connected BootSwitch and its current sysfs ancestors."""

import argparse
from pathlib import Path
import sys


def is_bootswitch(device):
    try:
        return ((device / "idVendor").read_text().strip().lower() == "303a"
                and (device / "idProduct").read_text().strip().lower() == "1001"
                and (device / "product").read_text().strip() in ("BootSwitch StickS3", "BootSwitch T-QT"))
    except OSError:
        return False


def discover(sysfs_root=Path("/sys")):
    return [device for device in sorted((sysfs_root / "bus/usb/devices").iterdir())
            if is_bootswitch(device)]


def configure_device(device, sysfs_root=Path("/sys"), check=False):
    devices_root = (sysfs_root / "devices").resolve(strict=True)
    device = device.resolve(strict=True)
    try:
        device.relative_to(devices_root)
    except ValueError:
        raise ValueError("Device must be inside the sysfs devices tree") from None
    if not is_bootswitch(device):
        raise ValueError("Device is not the BootSwitch USB application")
    own_wake = device / "power/wakeup"
    if not own_wake.is_file() or own_wake.read_text().strip() not in ("enabled", "disabled"):
        raise ValueError("BootSwitch does not advertise USB wake; check the installed firmware")

    states = []
    for node in (device, *device.parents):
        if node == devices_root:
            break
        wake = node / "power/wakeup"
        if not wake.is_file():
            continue
        # Kernel wake attributes are either enabled, disabled, or empty when
        # unsupported. Skip unsupported ancestors rather than writing to them.
        value = wake.read_text().strip()
        if value not in ("enabled", "disabled"):
            continue
        if not check and value == "disabled":
            wake.write_text("enabled\n")
            value = wake.read_text().strip()
            if value != "enabled":
                raise RuntimeError("Wake permission did not become enabled: " + str(wake))
        states.append((wake, value))
    return states


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Report permissions without changing them")
    parser.add_argument("device", nargs="?", type=Path,
                        help="USB sysfs device path from udev; omit to find connected BootSwitches")
    args = parser.parse_args()
    try:
        devices = [args.device] if args.device else discover()
        if not devices:
            raise ValueError("No BootSwitch USB application found")
        all_enabled = True
        for device in devices:
            for wake, value in configure_device(device, check=args.check):
                print(str(wake) + ": " + value)
                all_enabled &= value == "enabled"
        return 0 if all_enabled else 1
    except (OSError, ValueError, RuntimeError) as error:
        print("bootswitch-usb-wake: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
