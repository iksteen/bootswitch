# BootSwitch for M5Stack StickS3

Select Linux or Windows with the StickS3's **front A button**, a Wi-Fi web page,
or an HTTP API. The display shows the saved choice. Selection survives power
loss and applies when your PC next reads the device during boot. This does not
power on, restart, or switch a running PC into another OS.

Inspired by Stephen Holdaway's [Hardware boot selection switch](https://hackaday.io/project/179539-hardware-boot-selection-switch).
This is a new ESP32-S3 implementation with two PC integration options:

| Boot manager | Integration |
| --- | --- |
| **systemd-boot (preferred)** | A small x86-64 EFI bridge reads the device, sets `LoaderEntryOneShot`, then starts your existing systemd-boot. |
| GRUB | A configuration snippet reads the device directly. No EFI bridge needed. |

systemd-boot cannot execute the original project's GRUB script. Its configuration
lives on its own ESP/XBOOTLDR, so simply placing `loader.conf` on the StickS3 does
not change the PC's default. The bridge supplies the missing step using the
documented [Boot Loader Interface](https://systemd.io/BOOT_LOADER_INTERFACE/).

## Hardware

- An **M5Stack StickS3 (K150)** and a USB **data** cable to the target PC.
- No external switch, soldering, GPIO connections, or programmer.
- A 2.4 GHz Wi-Fi network for remote control.

The StickS3's ESP32-S3 has native USB; earlier ESP32-based StickC models cannot
run this firmware. See [M5Stack's StickS3 documentation](https://docs.m5stack.com/en/core/StickS3).
The USB port must initialize storage before the boot manager runs: disable UEFI
Fast Boot if necessary, and keep the internal boot manager first in boot order.
The StickS3 exposes a data disk, not an OS boot disk.

For Wi-Fi access while the PC is off, use a USB port that supplies standby power
(motherboard settings such as ErP may disable this), or a suitable powered USB hub
that retains power without backfeeding the host. The built-in 250 mAh battery is
only a temporary power source. Avoid sleep modes: the selector needs USB and
Wi-Fi running. Do not splice a second power supply into the USB cable.

## Build and flash the StickS3

Install [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html)
or use its VS Code extension. The toolchain and M5 libraries are pinned in
`platformio.ini`.

```sh
cp include/secrets.example.h include/secrets.h
# Edit secrets.h: Wi-Fi name/password and a NEW control password (16–96 characters).
pio run
```

The local environment created during development can also be used:

```sh
PLATFORMIO_CORE_DIR="$PWD/.pio-core" .venv/bin/pio run
```

Connect the StickS3 and hold its side **reset/power** button for about two seconds
until the internal green LED blinks, then release. This is the board's download
mode ([M5Stack upload instructions](https://docs.m5stack.com/en/arduino/m5sticks3/program)).
Flash with:

```sh
pio run --target upload --upload-port /dev/ttyACM0
```

Substitute the download port shown on your machine. Reset afterward if it remains
in download mode. The application presents mass storage only, so the serial
upload port disappears during normal use. Re-enter download mode to flash again.
The side reset/power button is **not** the mode toggle; use the front A button.

Without `secrets.h`, the firmware builds with Wi-Fi disabled and the button still
works. Credentials are compiled into the firmware: keep both `secrets.h` and any
credential-bearing binaries private. No provisioning access point or OTA is used.

## Button and Wi-Fi control

Press A once to toggle. The first boot defaults to Linux. Each successful change
is saved to NVS before it appears on USB or the display. A save error is shown
instead of acknowledging an unsaved change.

Visit `http://bootswitch.local/` or the IP shown on screen. Sign in as **admin**
using your control password. The page offers Linux, Windows, and Toggle controls.
It polls the device so button changes appear too. If mDNS is unavailable, use
the displayed IP or reserve a DHCP address.

For scripts, use the same password as a bearer token:

```sh
read -rs -p 'BootSwitch control password: ' BOOTSWITCH_TOKEN
echo
curl -H "Authorization: Bearer $BOOTSWITCH_TOKEN" http://bootswitch.local/api/mode

curl -X POST -H "Authorization: Bearer $BOOTSWITCH_TOKEN" \
  -H 'X-Bootswitch: 1' --data 'mode=windows' http://bootswitch.local/api/mode

curl -X POST -H "Authorization: Bearer $BOOTSWITCH_TOKEN" \
  -H 'X-Bootswitch: 1' http://bootswitch.local/api/toggle
unset BOOTSWITCH_TOKEN
```

Use explicit `mode=linux` / `mode=windows` for automation: retrying a toggle can
toggle twice. Mutations require POST and `X-Bootswitch: 1`; GET only reads state.
The browser uses HTTP Digest authentication. The API and page use HTTP on your
LAN; HTTP is unencrypted and bearer tokens can be observed on that network. Use
a trusted LAN or VPN, without forwarding this service to the public Internet.
Wi-Fi connects/reconnects independently of USB; no network is needed to boot.

## systemd-boot setup (x86-64 UEFI)

The included bridge runs **before** systemd-boot. Install it on the **same ESP**
as `EFI/systemd/systemd-bootx64.efi`. Linux and Windows must already be bootable
entries in systemd-boot. Use `bootctl list` to see their exact IDs; the bridge
does not create OS entries or search other ESPs for Windows.

1. Install GNU-EFI and GNU build tools (`gnu-efi`, `gcc`, `binutils`, `make` on
   Arch; `gnu-efi`, `build-essential`, `binutils` on Debian/Ubuntu), then build:

   ```sh
   make -C efi
   ```

   Override `EFI_INC` and `EFI_LIB` if your distribution puts GNU-EFI elsewhere.
   The result is `efi/build/bootswitchx64.efi`.

2. Run `bootctl status`, `bootctl list`, and `lsblk -o NAME,PATH,FSTYPE,PARTUUID,MOUNTPOINTS`
   to identify the correct ESP and entry IDs. Copy the files, adjusting the
   example `/efi` mount point to your system:

   ```sh
   sudo install -d /efi/EFI/BootSwitch
   sudo install -m 0644 efi/build/bootswitchx64.efi /efi/EFI/BootSwitch/
   sudo install -m 0644 efi/bootswitch.conf.example /efi/EFI/BootSwitch/bootswitch.conf
   sudoedit /efi/EFI/BootSwitch/bootswitch.conf
   ```

   Set `linux=` to the ID from `bootctl list` (often a `.conf` filename or a UKI
   filename). Set `windows=` to its exact ID (often `auto-windows`). Values are
   ASCII, without quotes or spaces around `=`. Avoid kernel-version-specific
   IDs if your setup provides a stable entry; otherwise update the config when
   kernel updates change IDs. An unknown ID falls back according to systemd-boot's
   own selection rules. Leave `loader=` at the supplied systemd-boot path.

3. Add a separate firmware boot entry. **Examples only**: substitute your actual
   ESP disk and partition number; do not run these using a guessed device.

   ```sh
   sudo efibootmgr --create-only --disk /dev/nvme0n1 --part 1 \
     --label 'BootSwitch' --loader '\EFI\BootSwitch\bootswitchx64.efi'
   sudo efibootmgr -v
   ```

   Record the new BootSwitch ID and the original boot order. Test once with
   `sudo efibootmgr --bootnext NEW_ID` (four hex digits, e.g. `0007`), then reboot
   with the StickS3 attached. Test both selections. If both work, place BootSwitch
   first in the UEFI setup's boot order, retaining the original Linux Boot Manager
   entry as a recovery option. No existing boot files need to be overwritten.

**Secure Boot:** this project produces an unsigned EFI application. With Secure
Boot enabled, sign the bridge using a key already trusted by your firmware and
install that signed output instead. A machine that trusts only Microsoft's keys
will not automatically trust a self-signed application. Keep your existing Secure
Boot workflow; the bridge does not enroll keys or bypass verification. Changing
the boot chain can also trigger BitLocker/TPM recovery; have the recovery key
available before testing.

At each boot the bridge reads `MODE.TXT` from the one FAT volume labeled
`BOOTSWITCH`. It sets a UTF-16 `LoaderEntryOneShot` EFI variable, then starts the
existing systemd-boot, which consumes and deletes the variable. It uses the normal
non-volatile attributes expected by systemd-boot's deletion code. The StickS3's
own saved selection is persistent, so it is applied again on later boots.

An existing `LoaderEntryOneShot` from the OS takes precedence (for example,
`systemctl reboot --boot-loader-entry=…`, update, or recovery requests). With no
unique valid selector, the bridge waits up to about two seconds, then leaves
systemd-boot's usual selection in control. Missing/invalid bridge configuration
also leaves the usual selection in control. It does not execute code from USB.

To undo: choose the original Linux Boot Manager in firmware, restore the original
boot order, delete only the BootSwitch firmware entry with
`efibootmgr --bootnum NEW_ID --delete-bootnum`, and remove `EFI/BootSwitch` from the
ESP. Leave `EFI/systemd`, `EFI/Microsoft`, and the normal fallback loader alone.

## GRUB setup (alternative)

Edit `grub/01_bootswitch`: replace both `YOUR-…-MENUENTRY-ID` values with IDs from
your `/boot/grub/grub.cfg`. Stable menuentry IDs are preferable to numeric positions.
For a submenu use the GRUB `submenu-id>entry-id` form. Then:

```sh
sudo install -m 0755 grub/01_bootswitch /etc/grub.d/01_bootswitch
# Debian/Ubuntu:
sudo update-grub
# Arch, instead:
sudo grub-mkconfig -o /boot/grub/grub.cfg
```

Do not edit generated `grub.cfg` directly. The snippet runs after `00_header`,
finds filesystem UUID `B007-5E1E`, and sources `SWITCH.CFG`. Missing devices leave
your normal GRUB default intact. The hardware choice overrides GRUB's default;
you can still choose another entry in its menu. Unlike the EFI bridge, this simple
snippet also overrides GRUB's saved/next default when the device is found.
To undo, remove `/etc/grub.d/01_bootswitch` and regenerate the configuration.

## USB behavior and timing

The device generates a read-only 16 MiB disk with an MBR and a valid FAT16
partition, label `BOOTSWITCH`, UUID `B007-5E1E`. Only two small files exist:

- `MODE.TXT`: `linux` or `windows`, padded to eight bytes including newline.
- `SWITCH.CFG`: `set bootswitch_mode=linux` or `…=windows`, with fixed file size.

All block writes are rejected and USB advertises write protection. There is no
shared writable flash filesystem. The mode is sampled once per USB read callback.
Metadata and file sizes never change. Both boot integrations read the selection
once during boot: set it **before powering on/restarting**. Changes made later
apply on a subsequent boot. Host firmware can cache reads; last-millisecond
changes are not guaranteed. A mounted OS may show stale file contents; use the
Wi-Fi API for live status. Never format the device.

## Verification

```sh
# C++ compiler, Python 3, dosfstools, mtools:
make test
# GNU-EFI, QEMU, OVMF, installed systemd-boot EFI binary:
python3 tests/check_efi.py
```

The native checks exercise full/partial/cross-sector reads and bounds under
AddressSanitizer/UBSan. `fsck.fat` and `mtype` independently validate both generated
filesystems. If running under a ptrace-based sandbox, use
`ASAN_OPTIONS=detect_leaks=0 make test` (LeakSanitizer cannot run under ptrace).

The EFI integration test boots the **real systemd-boot** in QEMU/OVMF, with the
generated disk attached as USB mass storage. Test payloads report the selected
entry and confirm one-shot consumption. Cases cover Linux, Windows, missing and
duplicate devices, an existing OS one-shot, and missing configuration. Override
`EFI_PREFIX`, `OVMF_CODE`, `OVMF_VARS`, and `SYSTEMD_BOOT` for different paths.

Development validation: both the button-only and Wi-Fi-configured ESP32 builds
passed; both FAT disk images passed independent checks; all six EFI integration
cases passed with systemd-boot 262.1 and OVMF 202608. The Wi-Fi build used example
credentials for compilation only, without connecting to a network.

Compilation and emulation cannot verify the StickS3's physical USB enumeration,
buttons, Wi-Fi connectivity, display, battery operation, or your motherboard's
USB initialization. Those need a test on the actual hardware before relying on
the setup.

Hardware validation on 2026-10-22: a StickS3 was backed up and flashed. Linux
recognized the read-only `BOOTSWITCH` disk and read both selection files. The
front button changed Linux to Windows on the display and in uncached USB reads;
Windows remained selected after a hardware reset. Wi-Fi was tested separately
afterward: the device joined Wi-Fi, Digest login and
bearer authentication worked, invalid/unauthenticated changes were rejected,
and both explicit HTTP selection and toggling agreed with uncached USB reads.
The original Windows selection was restored after those tests. The EFI bridge
was then installed and successfully tested on the physical PC with systemd-boot
258.1, using Arch Linux for the Linux position and Memtest for the Windows
position. Actual Windows booting was not part of that hardware test.
The full original firmware backup and verification record are in the ignored `build/backups/` directory;
keep the backup private because it may contain previous firmware settings.

To repeat the live Wi-Fi/USB checks, mount the USB volume read-only, then run
the following with the IP shown on the StickS3 and your actual mount path:

```sh
.venv/bin/python tests/check_device.py --host DEVICE_IP \
  --usb-mode-file "/run/media/$USER/BOOTSWITCH/MODE.TXT"
```

This uses the local `secrets.h`, temporarily changes the mode, and restores it.
It requires Python `requests`; the workspace PlatformIO environment includes it.

## License

[MIT](LICENSE), copyright 2026 Ingmar Steen.
