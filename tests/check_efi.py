"""Boot the real systemd-boot via our EFI bridge with emulated USB mass storage.

Run `make test` first. Requires GNU-EFI, QEMU, OVMF and systemd-boot's EFI binary.
Environment overrides: EFI_PREFIX, OVMF_CODE, OVMF_VARS, SYSTEMD_BOOT.
"""
import os
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parent.parent
out = root / "build/efi-test"
out.mkdir(parents=True, exist_ok=True)
prefix = Path(os.environ.get("EFI_PREFIX", "/usr"))
ovmf_code = Path(os.environ.get("OVMF_CODE", "/usr/share/edk2/x64/OVMF_CODE.4m.fd"))
ovmf_vars = Path(os.environ.get("OVMF_VARS", "/usr/share/edk2/x64/OVMF_VARS.4m.fd"))
sdboot = Path(os.environ.get("SYSTEMD_BOOT", "/usr/lib/systemd/boot/efi/systemd-bootx64.efi"))
subprocess.run(["make", "-C", "efi", f"EFI_PREFIX={prefix}"], cwd=root, check=True)

for name, flags in (("probe", []), ("seed", ["-DSEED_ONESHOT"])):
    subprocess.run([
        "gcc", f"-I{prefix}/include/efi", f"-I{prefix}/include/efi/x86_64",
        "-fpic", "-fshort-wchar", "-mno-red-zone", "-fno-stack-protector",
        "-ffreestanding", "-fno-builtin", "-DEFI_FUNCTION_WRAPPER", *flags,
        "-c", str(root / "tests/efi_probe.c"), "-o", str(out / f"{name}.o")
    ], check=True)
    subprocess.run([
        "ld", "-nostdlib", "-znocombreloc", "-shared", "-Bsymbolic",
        "-T", f"{prefix}/lib/elf_x86_64_efi.lds", f"{prefix}/lib/crt0-efi-x86_64.o",
        str(out / f"{name}.o"), f"-L{prefix}/lib", "-lefi", "-lgnuefi",
        "-o", str(out / f"{name}.so")
    ], check=True)
    subprocess.run([
        "objcopy", "-j", ".text", "-j", ".sdata", "-j", ".data", "-j", ".rodata", "-j", ".dynamic", "-j", ".dynstr",
        "-j", ".dynsym", "-j", ".rel", "-j", ".rela", "-j", ".reloc",
        "--input-target=elf64-x86-64", "--output-target=pei-x86-64", "--subsystem=10",
        str(out / f"{name}.so"), str(out / f"{name}.efi")
    ], check=True)

cases = [
    ("linux", ["linux"], "linux.conf", False, True),
    ("windows", ["windows"], "windows.conf", False, True),
    ("absent", [], "fallback.conf", False, True),
    ("duplicate", ["linux", "windows"], "fallback.conf", False, True),
    ("os-oneshot", ["linux"], "windows.conf", True, True),
    ("missing-config", ["windows"], "fallback.conf", False, False),
]
for name, devices, expected, seed, configured in cases:
    case = out / name
    esp = case / "esp"
    for sub in ("EFI/BOOT", "EFI/BootSwitch", "EFI/systemd", "loader/entries"):
        (esp / sub).mkdir(parents=True, exist_ok=True)
    bridge = root / "efi/build/bootswitchx64.efi"
    shutil.copy2(out / "seed.efi" if seed else bridge, esp / "EFI/BOOT/BOOTX64.EFI")
    shutil.copy2(bridge, esp / "EFI/BootSwitch/bootswitchx64.efi")
    shutil.copy2(sdboot, esp / "EFI/systemd/systemd-bootx64.efi")
    shutil.copy2(out / "probe.efi", esp / "EFI/BootSwitch/probe.efi")
    config = esp / "EFI/BootSwitch/bootswitch.conf"
    if configured:
        config.write_text("linux=linux.conf\nwindows=windows.conf\n")
    elif config.exists():
        config.unlink()
    (esp / "loader/loader.conf").write_text("default fallback.conf\ntimeout 0\n")
    for entry in ("linux", "windows", "fallback"):
        (esp / f"loader/entries/{entry}.conf").write_text(
            f"title {entry}\nefi /EFI/BootSwitch/probe.efi\n"
        )
    variables = case / "vars.fd"
    shutil.copy2(ovmf_vars, variables)
    debug = case / "debug.log"
    command = [
        "qemu-system-x86_64", "-machine", "q35,accel=tcg", "-m", "256",
        "-display", "none", "-serial", f"file:{case}/serial.log", "-monitor", "none", "-net", "none",
        "-drive", f"if=pflash,format=raw,readonly=on,file={ovmf_code}",
        "-drive", f"if=pflash,format=raw,file={variables}",
        "-drive", f"format=raw,file=fat:rw:{esp}",
        "-device", "qemu-xhci", "-debugcon", f"file:{debug}",
        "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04", "-no-reboot",
    ]
    for index, mode in enumerate(devices):
        command += [
            "-drive", f"if=none,id=selector{index},format=raw,readonly=on,file={root}/build/{mode}.img",
            "-device", f"usb-storage,drive=selector{index}",
        ]
    result = subprocess.run(command, capture_output=True, timeout=45,
                            env={**os.environ, "TMPDIR": str(out)})
    log = debug.read_text() if debug.exists() else ""
    assert result.returncode == 33, (name, result.returncode, result.stderr.decode(), log)
    assert f"selected={expected}\n" in log, (name, log)
    assert "oneshot=consumed\n" in log, (name, log)
    print(f"{name}: actual systemd-boot selected {expected}; one-shot consumed", flush=True)
print("EFI bridge integration checks passed.")
