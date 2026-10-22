"""Exercise a real BootSwitch over HTTP and optionally verify uncached USB reads.

Uses the local include/secrets.h without printing credentials. Requires requests.
Temporarily changes the selected mode and restores it, including on test failure.
"""
import argparse
import ast
import mmap
import os
from pathlib import Path
import re
import time

import requests
from requests.auth import HTTPDigestAuth


def setting(name):
    config = Path(__file__).resolve().parent.parent / "include/secrets.h"
    match = re.search(r'^\s*#define\s+' + name + r'\s+("(?:\\.|[^"\\])*")',
                      config.read_text(), re.MULTILINE)
    if not match:
        raise ValueError(f"Missing {name} in local configuration")
    return ast.literal_eval(match.group(1))


def usb_mode(path):
    descriptor = os.open(path, os.O_RDONLY | os.O_DIRECT)
    try:
        with mmap.mmap(-1, 512) as buffer:
            length = os.readv(descriptor, [buffer])
            return buffer[:length].decode("ascii").strip()
    finally:
        os.close(descriptor)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", help="Device IP or hostname; defaults to configured .local name")
    parser.add_argument("--usb-mode-file", type=Path,
                        help="Mounted MODE.TXT path, read with Linux O_DIRECT")
    args = parser.parse_args()
    password = setting("BOOTSWITCH_CONTROL_PASSWORD")
    host = args.host or setting("BOOTSWITCH_HOSTNAME") + ".local"
    base = f"http://{host}"
    session = requests.Session()
    session.trust_env = False
    authorized = {"Authorization": "Bearer " + password}
    mutation = {**authorized, "X-Bootswitch": "1"}

    def call(method, path, expected, **kwargs):
        response = session.request(method, base + path, timeout=5, **kwargs)
        assert response.status_code == expected, (
            f"{method} {path}: expected HTTP {expected}, got {response.status_code}"
        )
        return response

    def state():
        result = call("GET", "/api/mode", 200, headers=authorized).json()
        assert result["mode"] in ("linux", "windows") and result["saved"] is True
        return result["mode"]

    def verify_mode(expected):
        assert state() == expected
        if args.usb_mode_file:
            assert usb_mode(args.usb_mode_file) == expected, "HTTP and USB modes differ"

    original = state()
    other = "windows" if original == "linux" else "linux"
    print(f"Connected to {base}; original selection: {original}", flush=True)
    try:
        call("GET", "/api/mode", 401)
        call("GET", "/api/mode", 401,
             headers={"Authorization": "Bearer deliberately-invalid-test-token"})
        page = call("GET", "/", 200, auth=HTTPDigestAuth("admin", password))
        assert "<title>BootSwitch</title>" in page.text
        print("Unauthenticated/invalid requests rejected; browser Digest login works.", flush=True)
        call("GET", "/api/toggle", 404, headers=authorized)
        call("POST", "/api/mode", 403, headers=authorized, data={"mode": other})
        call("POST", "/api/mode", 400, headers=mutation, data={"mode": "invalid"})
        verify_mode(original)
        print("GET, missing change header, and invalid mode cannot change the selection.", flush=True)
        selected = call("POST", "/api/mode", 200, headers=mutation, data={"mode": other}).json()
        assert selected["mode"] == other
        verify_mode(other)
        print(f"Explicit Wi-Fi selection confirmed: {other}", flush=True)
        toggled = call("POST", "/api/toggle", 200, headers=mutation).json()
        assert toggled["mode"] == original
        verify_mode(original)
        print(f"Wi-Fi toggle confirmed: {original}", flush=True)
    finally:
        for attempt in range(3):
            try:
                call("POST", "/api/mode", 200, headers=mutation, data={"mode": original})
                verify_mode(original)
                print(f"Original selection restored: {original}", flush=True)
                break
            except (requests.RequestException, AssertionError, OSError):
                if attempt == 2:
                    raise
                time.sleep(1)
    print("Physical device HTTP checks passed" +
          (" with USB agreement." if args.usb_mode_file else "."), flush=True)


if __name__ == "__main__":
    main()
