"""Use independent FAT tools to validate the generated USB image and payloads."""
from pathlib import Path
import subprocess

for mode, expected in (("linux", b"linux  \n"), ("windows", b"windows\n")):
    disk = Path(f"build/{mode}.img")
    data = disk.read_bytes()
    partition = Path(f"build/{mode}-partition.img")
    partition.write_bytes(data[2048 * 512 :])
    subprocess.run(["fsck.fat", "-n", "-v", str(partition)], check=True)
    image_arg = f"{disk}@@{2048 * 512}"
    contents = subprocess.check_output(["mtype", "-i", image_arg, "::MODE.TXT"])
    assert contents == expected, (mode, contents)
    config = subprocess.check_output(["mtype", "-i", image_arg, "::SWITCH.CFG"])
    assert config == b"set bootswitch_mode=" + expected, config
print("Both FAT16 images and their files passed independent checks.")
