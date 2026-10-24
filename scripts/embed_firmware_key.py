"""Embed only the committed verification key; a firmware build needs no secret."""
from pathlib import Path
import sys

Import("env")
root = Path(env.subst("$PROJECT_DIR"))
sys.path.insert(0, str(root / "scripts"))
from firmware_signing import embed_key

generated = Path(env.subst("$BUILD_DIR")) / "generated"
embed_key(generated / "firmware_public_key.h", root / "keys/firmware-public.pem")
env.Append(CPPPATH=[str(generated)])

# Keep the board descriptor immediately after ESP-IDF's 256-byte app descriptor.
env.Append(LINKFLAGS=["-Wl,--undefined=bootswitch_board_descriptor"])
