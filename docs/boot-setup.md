# PC boot manager setup

[Home](../README.md)

Run repository commands from the repository root. The PC must initialize USB
storage before its boot manager runs; disable UEFI Fast Boot if needed.

systemd-boot cannot execute the original project's GRUB script. Its configuration
lives on its own ESP/XBOOTLDR, so simply placing `loader.conf` on the StickS3 does
not change the PC's default. The bridge supplies the missing step using the
documented [Boot Loader Interface](https://systemd.io/BOOT_LOADER_INTERFACE/).

## systemd-boot (x86-64 UEFI)

The included bridge runs **before** systemd-boot. Install it on the **same ESP**
as `EFI/systemd/systemd-bootx64.efi`. Linux and Windows must already be bootable
entries in systemd-boot. Use `bootctl list` to see their exact IDs; the bridge
does not create OS entries or search other ESPs for Windows.

A prebuilt bridge is available in the [release downloads](firmware.md#downloads-and-installation).
The steps below build it from source; substitute the downloaded binary's path
in the copy command if using that instead. The bridge settings template is
[efi/bootswitch.conf.example](../efi/bootswitch.conf.example).

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

## GRUB

Edit the [GRUB snippet](../grub/01_bootswitch): replace both `YOUR-…-MENUENTRY-ID` values with IDs from
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
