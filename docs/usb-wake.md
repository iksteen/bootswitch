# Wake the PC over USB

[Home](../README.md)

The device presents USB mass storage **and a passive HID keyboard interface**.
The keyboard interface lets the host OS offer its normal USB wake permissions.
Wake uses USB remote-resume signaling; the firmware does not send keyboard input.

For API authentication and BLE pairing, see [Controls](controls.md). Configure
wake permission separately in each OS using the instructions below.

Three controls request the same wake action, without changing the saved boot mode:

- Briefly press and release the **side B user button**, even with the display off.
  Holding B for 1.5 seconds still applies USB settings after flush/unmount.
- Use **Wake PC from sleep** on the web page, or send `POST /api/wake` with the
  same authentication and `X-Bootswitch: 1` header as a mode change.
- Write ASCII `wake` to the existing BLE command characteristic after pairing.

For example, with `BOOTSWITCH_TOKEN` set to your API key:

```sh
curl -X POST -H "Authorization: Bearer $BOOTSWITCH_TOKEN" \
  -H 'X-Bootswitch: 1' http://bootswitch.local/api/wake

curl -H "Authorization: Bearer $BOOTSWITCH_TOKEN" \
  http://bootswitch.local/api/usb
```

A successful request returns HTTP 200 and `{"wake_requested":true}`. This means
the USB resume signal was requested, not that the PC has confirmed waking.
HTTP 409 means the USB bus is not suspended or the host has not armed remote
wake; HTTP 503 means USB is not configured by a connected host. The display also
shows the result. GET `/api/usb` reports `configured`, `suspended`, and
`remote_wakeup_enabled`; the last field reflects the host permission reported at
the current suspend. Polling it does not wake the display or the PC.

USB suspend also happens during runtime power saving, so it does not prove that
the whole PC is asleep. Wake commands are explicit: choosing Linux/Windows or
toggling the saved mode never automatically wakes the PC.

The host must arm USB remote wake **before** suspending, and its firmware,
USB controller, and any intervening hub must permit USB wake. Enable USB keyboard
wake in UEFI/BIOS if needed. The feature targets sleep; shutdown and hibernation
wake support depend on the PC. See the [Linux USB power-management documentation](https://cdn.kernel.org/doc/html/latest/driver-api/usb/power-management.html)
and [Microsoft's HID power-management documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/power-management).

Wake permissions are configured separately in each OS; enabling them in Linux
does not enable them in Windows.

## Linux wake permission

From the repository root, install the [Python 3 helper](../host/bootswitch_usb_wake.py)
and its [udev rule](../host/99-bootswitch-wakeup.rules):

```sh
sudo install -D -m 0755 host/bootswitch_usb_wake.py /usr/local/libexec/bootswitch-usb-wake
sudo install -m 0644 host/99-bootswitch-wakeup.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo /usr/local/libexec/bootswitch-usb-wake
```

The last command applies permissions immediately to connected BootSwitches;
reloading udev alone does not apply rules to an already-connected device. On
each later USB add/change event, including insertion, re-enumeration and boot,
the rule passes the application's current sysfs path to the helper. It matches
the application's USB product name and IDs, excluding the ROM download mode.

The helper resolves that device's **actual parent chain**, enabling supported
`power/wakeup` attributes on the StickS3, upstream hubs, root hub, and controller
ancestors. It discovers them again on each event, so bus numbers, USB ports and
hub depth can change without editing rules. Unsupported attributes are skipped.
Enabled ancestor permissions remain enabled after unplugging the StickS3.

Inspect the current chain without changing settings; this does not need root:

```sh
/usr/local/libexec/bootswitch-usb-wake --check
```

Each supported node should report `enabled`; `--check` exits nonzero if a
permission is disabled or no BootSwitch application is found. Verify after
rebooting too, since another power-management tool may override the settings.
If necessary, rerun `sudo /usr/local/libexec/bootswitch-usb-wake` while the PC is
awake, before suspending it.

If `/etc/udev/rules.d/99-bootswitch-parent-wakeup.rules` is installed, remove
that specific file and reload the rules. Only the dynamic helper and its rule
are needed; generated files under `build/` are not part of installation.

## Windows wake permission

1. Open **Device Manager → Keyboards**.
2. Find the StickS3's **HID Keyboard Device**. Unplug/reconnect the StickS3 to
   identify which entry disappears and returns; several keyboards may have the
   same display name.
3. Open that device's **Properties → Power Management**.
4. Enable **Allow this device to wake the computer**, then apply the change.

In an **administrator terminal**, verify that the keyboard is armed:

```powershell
powercfg /devicequery wake_armed
```

If it is absent, list devices whose wake permission Windows allows configuring:

```powershell
powercfg /devicequery wake_programmable
```

If the Power Management tab is missing or the device cannot be armed, inspect
the available sleep states with `powercfg /a` and check the PC's USB/firmware
wake support. See [Microsoft's powercfg reference](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/powercfg-command-line-options#devicequery).

## Test wake from sleep

After enabling the host's wake permissions, put the PC into **Sleep**, then
briefly press B or send BLE `wake` / `POST /api/wake` from another device.
The saved Linux/Windows selection should stay unchanged. GET `/api/usb` can
diagnose host permission while suspended: `remote_wakeup_enabled` should be
`true` once the host has armed wake and suspended the device. It is `false` while
the USB bus is active, even if the OS is configured to allow wake on its next
suspend. HTTP 200 confirms only that a wake signal was requested; confirm the
PC actually resumes. On Windows, `powercfg /lastwake` reports the most recent
wake source.
