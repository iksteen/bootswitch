# BootSwitch

Choose Linux or Windows using a button on an M5Stack StickS3 or LILYGO T-QT,
a Wi-Fi web page, an HTTP API, or Bluetooth LE. The saved choice survives power loss and applies the
next time the PC boots. The display shows the selected OS and Wi-Fi signal strength, plus battery level
on StickS3.

BootSwitch can also wake a sleeping PC through USB when the host permits it.
Changing the selected OS does not restart the PC or switch a running OS.
Firmware can be [updated over Wi-Fi on request](docs/firmware.md#wi-fi-updates)
using signed GitHub release downloads.

Inspired by Stephen Holdaway's [Hardware boot selection switch](https://hackaday.io/project/179539-hardware-boot-selection-switch).

## What you need

- An **M5Stack StickS3 (K150)** or **LILYGO T-QT FN4R2** (4 MB flash,
  2 MB PSRAM), and a USB **data** cable. Earlier StickC models cannot run this firmware.
- Linux and Windows already installed and bootable through systemd-boot or GRUB.
- Optional: a 2.4 GHz Wi-Fi network and/or a BLE client for remote control.

No external switch, soldering or programmer is required. The Stick presents a
USB data disk; the PC's internal boot manager starts the selected OS.

| Boot manager | Integration |
| --- | --- |
| **systemd-boot** | An x86-64 EFI bridge reads the Stick and selects the next entry before starting the existing systemd-boot. |
| **GRUB** | A configuration snippet reads the Stick directly. |

## Quick start

1. [Install the firmware](docs/firmware.md) from a [GitHub Release](https://github.com/iksteen/bootswitch/releases).
   Use the production flash ZIP for normal use; it accepts signed USB updates.
   Development images also accept unsigned updates and enable remote download mode.
2. Optionally [edit CONFIG.INI](docs/configuration.md) on the **BOOTSWITCH** USB
   drive to enable Wi-Fi, the API or BLE. Save and **safely eject** to apply.
   All three are disabled until configured; the button works without them.
3. [Set up systemd-boot or GRUB](docs/boot-setup.md) on the PC.
4. Press **A** to choose an OS, then boot the PC. If the screen is asleep, the
   first press wakes it; press again to change the selection.

## Guides

| Topic | Guide |
| --- | --- |
| Firmware downloads, installation and updates | [Firmware](docs/firmware.md) |
| Signing keys and authenticated USB updates | [Firmware signing](docs/signing.md) |
| Wi-Fi credentials, API key, BLE PIN and display timeout | [Configuration](docs/configuration.md) |
| Buttons, display, HTTP API and Bluetooth commands | [Controls](docs/controls.md) |
| EFI bridge, Secure Boot, GRUB and removing the integration | [PC boot setup](docs/boot-setup.md) |
| Linux and Windows USB wake permissions | [Wake from sleep](docs/usb-wake.md) |
| Hardware requirements, power draw and USB behavior | [Hardware](docs/hardware.md) |
| Source builds, development flags, tests and CI releases | [Development](docs/development.md) |

## License

[MIT](LICENSE), copyright 2026 Ingmar Steen.
