"""Copy a large firmware file into a fragmented FAT volume using real tools."""
from pathlib import Path
import subprocess

image = Path("build/firmware-disk.img")
image_arg = f"{image}@@{2048 * 512}"
filler = Path("build/firmware-filler.bin")
filler.write_bytes(bytes(65536))
for index in range(20):
    subprocess.run(["mcopy", "-i", image_arg, str(filler), f"::FILL{index:02}.TMP"], check=True)
for index in range(0, 20, 2):
    subprocess.run(["mdel", "-i", image_arg, f"::FILL{index:02}.TMP"], check=True)
source = Path("build/test-firmware.bin")
source.write_bytes(bytes(range(256)) * 5079 + b"partial final sector")
subprocess.run(["mcopy", "-i", image_arg, str(source), "::UPDATE.BIN"], check=True)
target = Path("build/extracted-firmware.bin")
subprocess.run(["build/firmware_update_test", "--extract", str(image), str(target)], check=True)
assert target.read_bytes() == source.read_bytes()
assert subprocess.check_output(["mtype", "-i", image_arg, "::MODE.TXT"]) == b"linux  \n"
print("Large firmware copy and fragmented FAT streaming passed with real FAT tools.")
