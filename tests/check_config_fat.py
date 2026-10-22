"""Use real FAT utilities to exercise normal writes and editor rename replacement."""
from pathlib import Path
import subprocess

image = Path("build/config-disk.img")
image_arg = f"{image}@@{2048 * 512}"
initial = subprocess.check_output(["mtype", "-i", image_arg, "::CONFIG.INI"])
config = (
    b"\xef\xbb\xbf# Saved by a text editor\r\n"
    + b"# Long comment to span multiple clusters\r\n" * 40
    + b"wifi_ssid=New Wi-Fi network\r\n"
    + b"wifi_password=new-test-password\r\n"
    + b"api_key=a-new-api-key-with-32-characters!\r\n"
    + b"hostname=new-bootswitch\r\n"
)
source = Path("build/edited-config.ini")
source.write_bytes(config)
assert initial != config


def extract():
    target = Path("build/extracted-config.ini")
    subprocess.run(["build/config_disk_test", "--extract", str(image), str(target)], check=True)
    assert target.read_bytes() == config
    assert subprocess.check_output(["mtype", "-i", image_arg, "::MODE.TXT"]) == b"linux  \n"
    assert subprocess.check_output(["mtype", "-i", image_arg, "::SWITCH.CFG"]) == b"set bootswitch_mode=linux  \n"
    data = image.read_bytes()
    partition = Path("build/config-partition.img")
    partition.write_bytes(data[2048 * 512 :])
    subprocess.run(["fsck.fat", "-n", str(partition)], check=True)


# Ordinary overwrite; mtools allocates a FAT chain for the larger new file.
subprocess.run(["mcopy", "-o", "-i", image_arg, str(source), "::CONFIG.INI"], check=True)
extract()
# A common editor's safe-save: write a temporary file, delete, rename. This
# necessarily relocates CONFIG.INI to different clusters than the initial file.
subprocess.run(["mcopy", "-i", image_arg, str(source), "::EDITOR.TMP"], check=True)
subprocess.run(["mdel", "-i", image_arg, "::CONFIG.INI"], check=True)
subprocess.run(["mren", "-i", image_arg, "::EDITOR.TMP", "CONFIG.INI"], check=True)
extract()
print("Ordinary overwrite and temporary-file/rename saves passed with real FAT tools.")
