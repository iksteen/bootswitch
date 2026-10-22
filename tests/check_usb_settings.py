"""Hardware test: replace CONFIG.INI, eject, check new key, restore settings.

Linux/UDisks2 only. Keeps a private recovery copy under ignored build/backups.
The test temporarily changes the API key and hostname, never the Wi-Fi network.
"""
import argparse
import os
from pathlib import Path
import re
import secrets
import subprocess
import time

import requests


def fields(text):
    result = {}
    for line in text.splitlines():
        if line.strip() and not line.lstrip().startswith(("#", ";")):
            key, value = line.split("=", 1)
            result[key.strip()] = value
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--mount", type=Path, required=True)
    parser.add_argument("--offline-provisioning", action="store_true",
                        help="Also disable Wi-Fi through USB, then provision it again offline")
    parser.add_argument("--disabled-services", action="store_true",
                        help="Check blank API key with Wi-Fi on, and an entirely empty config")
    args = parser.parse_args()
    config = args.mount / "CONFIG.INI"
    original = config.read_text(encoding="utf-8-sig")
    before = fields(original)
    backup = Path(f"build/backups/usb-settings-original-{time.time_ns()}.ini")
    backup.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(backup, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w") as output:
        output.write(original)
    session = requests.Session()
    session.trust_env = False
    endpoint = f"http://{args.host}/api/mode"
    block = "/dev/disk/by-label/BOOTSWITCH"
    info = subprocess.check_output(["udisksctl", "info", "--block-device", block], text=True)
    drive = re.search(r"^\s*Drive:\s+'([^']+)'", info, re.M).group(1)

    def status(key):
        return session.get(endpoint, headers={"Authorization": "Bearer " + key}, timeout=2)

    initial = status(before["api_key"])
    initial.raise_for_status()
    original_mode = initial.json()["mode"]
    changed = dict(before, api_key=secrets.token_urlsafe(24), hostname="bootswitch-usb-test")
    edited = "# Temporary editor-save hardware test\n" + "".join(
        f"{key}={value}\n" for key, value in changed.items()
    )

    def replace_and_eject(text):
        # Exercise the common safe-save pattern rather than an in-place write.
        temporary = args.mount / "EDITOR.TMP"
        with temporary.open("w") as output:
            output.write(text)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, config)
        directory = os.open(args.mount, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
        subprocess.run(["udisksctl", "unmount", "--block-device", block],
                       check=True, capture_output=True)
        result = subprocess.run([
            "busctl", "--system", "call", "org.freedesktop.UDisks2", drive,
            "org.freedesktop.UDisks2.Drive", "Eject", "a{sv}", "0"
        ], capture_output=True)
        # Restart can race the Eject method's final readiness checks. Verify the
        # result by reconnecting and checking the actual settings below.
        if result.returncode:
            print("Eject raced the device restart; checking the applied settings.", flush=True)
        time.sleep(2)

    def remount():
        for _ in range(25):
            if Path(block).exists():
                subprocess.run(["udisksctl", "mount", "--block-device", block], capture_output=True)
                if config.exists():
                    return fields(config.read_text(encoding="utf-8-sig"))
            time.sleep(0.5)
        raise RuntimeError("USB volume did not return; original config is in build/backups")

    def wait_for_key(key):
        for _ in range(25):
            try:
                response = status(key)
                if response.status_code == 200:
                    assert response.json()["mode"] == original_mode
                    return
            except requests.RequestException:
                pass
            time.sleep(0.5)
        raise RuntimeError("Device did not accept the expected key after restart")

    def verify_http_off():
        # A missing key must close the HTTP listener, not accept empty auth or
        # merely reject a previously valid key on an otherwise running API.
        for path in ("/", "/api/mode"):
            try:
                session.get(f"http://{args.host}{path}", timeout=2)
            except requests.RequestException:
                pass
            else:
                raise AssertionError("HTTP listener answered while API was disabled")

    try:
        print("Saving a temporary key and hostname through CONFIG.INI, then ejecting.", flush=True)
        replace_and_eject(edited)
        assert remount() == changed, "USB settings did not survive restart"
        wait_for_key(changed["api_key"])
        assert status(before["api_key"]).status_code == 401
        print("New key works; old key rejected; all settings and boot mode survived restart.", flush=True)
        if args.disabled_services:
            no_api = dict(before, api_key="")
            replace_and_eject("".join(f"{key}={value}\n" for key, value in no_api.items()))
            assert remount() == no_api
            for attempt in range(10):
                result = subprocess.run(["ping", "-c", "1", "-W", "1", args.host], capture_output=True)
                if result.returncode == 0:
                    break
                time.sleep(0.5)
            else:
                raise AssertionError("Wi-Fi did not stay connected when only the API key was cleared")
            verify_http_off()
            print("Blank API key: Wi-Fi responds to ping; HTTP and API listeners are disabled.", flush=True)
        if args.offline_provisioning:
            offline = dict(before, wifi_ssid="", wifi_password="")
            text = "".join(f"{key}={value}\n" for key, value in offline.items())
            replace_and_eject(text)
            assert remount() == offline
            try:
                status(before["api_key"])
            except requests.RequestException:
                pass
            else:
                raise AssertionError("Device answered HTTP while Wi-Fi was disabled in CONFIG.INI")
            print("USB settings disabled Wi-Fi; configuration disk remains usable offline.", flush=True)
        if args.disabled_services:
            replace_and_eject("")
            assert remount() == {"wifi_ssid": "", "wifi_password": "", "api_key": "",
                                 "hostname": "bootswitch", "ble_passkey": "",
                                 "screen_timeout": "60"}
            verify_http_off()
            print("Empty CONFIG.INI: no Wi-Fi credentials or API key invented; both services off.", flush=True)
    finally:
        # Leave the user's original working settings in place even if an
        # assertion fails. Never print the config or the temporary token.
        if not config.exists():
            remount()
        replace_and_eject(original)
        assert remount() == before
        wait_for_key(before["api_key"])
        assert status(changed["api_key"]).status_code == 401
        print("Original settings restored; temporary key rejected; boot mode unchanged.", flush=True)
    print("USB configuration save/eject/restart hardware test passed.", flush=True)


if __name__ == "__main__":
    main()
