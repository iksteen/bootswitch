"""Signing-tool tests and disposable keys/fixtures for the real mbedTLS updater."""
import argparse
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import firmware_signing as signing
from cryptography.exceptions import InvalidSignature


def application():
    # The signature trailer straddles a cluster, including its magic bytes.
    image = bytearray((i * 73 + i // 512) & 255 for i in range(1300479))
    image[:24] = bytes(24)
    image[0] = 0xE9
    image[12:14] = b"\x09\x00"
    image[32:36] = struct.pack("<I", 0xABCD5432)
    return bytes(image)


def fixtures(directory):
    directory.mkdir(parents=True, exist_ok=True)
    private, public = directory / "test-private.pem", directory / "test-public.pem"
    # These are disposable test keys, never the official BootSwitch key.
    if not private.exists():
        signing.generate_key(private, public)
    other_private, other_public = directory / "other-private.pem", directory / "other-public.pem"
    if not other_private.exists():
        signing.generate_key(other_private, other_public)
    signing.embed_key(directory / "firmware_public_key.h", public)
    image = application()
    (directory / "raw.bin").write_bytes(image)
    (directory / "signed.bin").write_bytes(signing.sign_image(image, private, public))
    (directory / "wrong-key.bin").write_bytes(signing.sign_image(image, other_private, other_public))


class SigningTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.private, self.public = self.directory / "private.pem", self.directory / "public.pem"
        signing.generate_key(self.private, self.public)
        self.image = application()[:5000]
        self.signed = signing.sign_image(self.image, self.private, self.public)

    def test_roundtrip_and_permissions(self):
        self.assertEqual(signing.verify_image(self.signed, self.public), self.image)
        self.assertEqual(self.private.stat().st_mode & 0o777, 0o600)
        self.assertEqual(len(self.signed), len(self.image) + 80)
        self.assertEqual(len(signing.public_der(self.public)), 91)

    def test_rejects_tampering_truncation_and_extra_bytes(self):
        for offset in (0, 4096, len(self.image) - 1, len(self.image),
                       len(self.image) + 8, len(self.image) + 12, len(self.image) + 16, len(self.signed) - 1):
            bad = bytearray(self.signed)
            bad[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises((ValueError, InvalidSignature)):
                signing.verify_image(bytes(bad), self.public)
        for bad in (self.image, self.signed[:-1], self.signed[:-80], self.signed + b"\0", b""):
            with self.assertRaises((ValueError, InvalidSignature)):
                signing.verify_image(bad, self.public)

    def test_rejects_wrong_keys_and_double_signing(self):
        private, public = self.directory / "other.pem", self.directory / "other-public.pem"
        signing.generate_key(private, public)
        with self.assertRaises(ValueError):
            signing.sign_image(self.image, private, self.public)
        with self.assertRaises(InvalidSignature):
            signing.verify_image(self.signed, public)
        with self.assertRaises(ValueError):
            signing.sign_image(self.signed, self.private, self.public)

    def test_key_generation_never_overwrites(self):
        before = self.private.read_bytes()
        with self.assertRaises(ValueError):
            signing.generate_key(self.private, self.public)
        self.assertEqual(self.private.read_bytes(), before)

    def test_curve_restriction(self):
        from cryptography.hazmat.primitives import serialization
        from cryptography.hazmat.primitives.asymmetric import ec
        key = ec.generate_private_key(ec.SECP384R1())
        self.public.write_bytes(key.public_key().public_bytes(serialization.Encoding.PEM,
                                 serialization.PublicFormat.SubjectPublicKeyInfo))
        with self.assertRaises(ValueError):
            signing.sign_image(self.image, self.private, self.public)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixtures", type=Path)
    args = parser.parse_args()
    if args.fixtures:
        fixtures(args.fixtures)
    unittest.main(argv=[sys.argv[0]])
