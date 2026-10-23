"""BootSwitch signed USB updates: P-256/SHA-256, with an 80-byte trailer.

The signed message is the complete ESP application followed by the 16-byte
metadata (domain/version, exact image length, reserved zero). The final 64 bytes
are unsigned big-endian r || s. No key supplied by an update is ever trusted.
"""
import argparse
import base64
import os
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
PUBLIC_KEY = ROOT / "keys/firmware-public.pem"
MAGIC = b"BSWUPD01"
METADATA = struct.Struct("<8sII")
TRAILER_SIZE = 80


def public_der(path=PUBLIC_KEY):
    pem = Path(path).read_text().strip().splitlines()
    if pem[0] != "-----BEGIN PUBLIC KEY-----" or pem[-1] != "-----END PUBLIC KEY-----":
        raise ValueError("Expected a PEM public key")
    return base64.b64decode("".join(pem[1:-1]), validate=True)


def embed_key(output, public_key=PUBLIC_KEY):
    der = public_der(public_key)
    text = ("// Generated from keys/firmware-public.pem; public key only.\n#pragma once\n"
            "static const unsigned char kFirmwarePublicKey[] = {\n  " +
            ", ".join(f"0x{byte:02x}" for byte in der) + "\n};\n")
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text() != text:
        output.write_text(text)


def load_public(path=PUBLIC_KEY):
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    key = serialization.load_pem_public_key(Path(path).read_bytes())
    if not isinstance(key, ec.EllipticCurvePublicKey) or not isinstance(key.curve, ec.SECP256R1):
        raise ValueError("BootSwitch requires an ECDSA P-256 key")
    return key


def generate_key(private_path, public_path):
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    private_path, public_path = Path(private_path), Path(public_path)
    if private_path.exists() or public_path.exists():
        raise ValueError("Refusing to replace an existing signing key")
    key = ec.generate_private_key(ec.SECP256R1())
    private_path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    public_path.parent.mkdir(parents=True, exist_ok=True)
    private = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                serialization.NoEncryption())
    with os.fdopen(os.open(private_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "wb") as file:
        file.write(private)
    with public_path.open("xb") as file:
        file.write(key.public_key().public_bytes(serialization.Encoding.PEM,
                   serialization.PublicFormat.SubjectPublicKeyInfo))


def validate_image(image):
    # The ESP-IDF updater remains the authority on full image validity.
    if (len(image) < 36 or image[0] != 0xE9 or image[12:14] != b"\x09\x00" or
            image[32:36] != b"\x32\x54\xcd\xab"):
        raise ValueError("Expected a raw ESP32-S3 application image")
    if len(image) >= TRAILER_SIZE and image[-TRAILER_SIZE:-TRAILER_SIZE+8] == MAGIC:
        raise ValueError("Image already has a BootSwitch signature")


def sign_image(image, private_path, public_path=PUBLIC_KEY):
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec, utils
    validate_image(image)
    key = serialization.load_pem_private_key(Path(private_path).read_bytes(), password=None)
    public = load_public(public_path)
    if (not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(key.curve, ec.SECP256R1) or
            key.public_key().public_numbers() != public.public_numbers()):
        raise ValueError("Signing key does not match the trusted BootSwitch P-256 public key")
    metadata = METADATA.pack(MAGIC, len(image), 0)
    r, s = utils.decode_dss_signature(key.sign(image + metadata, ec.ECDSA(hashes.SHA256())))
    result = image + metadata + r.to_bytes(32, "big") + s.to_bytes(32, "big")
    verify_image(result, public_path)
    return result


def verify_image(signed, public_path=PUBLIC_KEY):
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import ec, utils
    if len(signed) <= TRAILER_SIZE:
        raise ValueError("Missing firmware signature")
    image, metadata, signature = signed[:-80], signed[-80:-64], signed[-64:]
    magic, length, flags = METADATA.unpack(metadata)
    if magic != MAGIC or length != len(image) or flags != 0:
        raise ValueError("Invalid firmware signature metadata")
    der = utils.encode_dss_signature(int.from_bytes(signature[:32], "big"),
                                     int.from_bytes(signature[32:], "big"))
    load_public(public_path).verify(der, image + metadata, ec.ECDSA(hashes.SHA256()))
    validate_image(image)
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    generate = commands.add_parser("generate-key")
    generate.add_argument("--private-key", type=Path, required=True)
    generate.add_argument("--public-key", type=Path, default=PUBLIC_KEY)
    embed = commands.add_parser("embed-key")
    embed.add_argument("--public-key", type=Path, default=PUBLIC_KEY)
    embed.add_argument("--output", type=Path, required=True)
    for name in ("sign", "verify"):
        command = commands.add_parser(name)
        command.add_argument("image", type=Path)
        command.add_argument("--public-key", type=Path, default=PUBLIC_KEY)
        if name == "sign":
            command.add_argument("--private-key", type=Path, required=True)
            command.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "generate-key":
            generate_key(args.private_key, args.public_key)
            print(f"Private key created at {args.private_key}; keep a secure backup.")
        elif args.command == "embed-key":
            embed_key(args.output, args.public_key)
        elif args.command == "sign":
            if args.output.resolve() == args.image.resolve():
                raise ValueError("Input and output must be different files")
            args.output.write_bytes(sign_image(args.image.read_bytes(), args.private_key, args.public_key))
            print(f"Signed update written to {args.output}")
        else:
            verify_image(args.image.read_bytes(), args.public_key)
            print("Valid BootSwitch firmware signature.")
    except Exception as error:
        parser.exit(1, f"{type(error).__name__}: {error}\n")


if __name__ == "__main__":
    main()
