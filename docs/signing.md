# Firmware signing

[Home](../README.md) · [Installation and updates](firmware.md)

Production firmware accepts USB updates signed by the ECDSA P-256 public key in
[`keys/firmware-public.pem`](../keys/firmware-public.pem). Development firmware
also accepts raw ESP32-S3 application binaries. Signature verification uses
SHA-256 and mbedTLS; no private key is embedded in either build.

## Local signing

Install the Python signing dependency:

```sh
python -m pip install -r requirements-signing.txt
```

The project signing key is stored locally at `.signing/bootswitch-private.pem`.
This directory is ignored by Git; keep a secure backup of the private key. Its
public counterpart is committed and is embedded during normal builds, which
require no private key or signing dependency.

Build and sign the production update:

```sh
pio run --environment sticks3 --target release
python scripts/firmware_signing.py sign build/release/bootswitch-sticks3.bin \
  --private-key .signing/bootswitch-private.pem \
  --output build/release/FIRMWARE.BIN
python scripts/firmware_signing.py verify build/release/FIRMWARE.BIN
```

Alternatively, set `BOOTSWITCH_SIGNING_KEY_FILE` when invoking the release target
to generate the signed update alongside the raw application and flash ZIP:

```sh
BOOTSWITCH_SIGNING_KEY_FILE="$PWD/.signing/bootswitch-private.pem" \
  pio run --environment sticks3 --target release
```

The signer checks that the private key is P-256 and matches the committed public
key. It refuses an already signed input and verifies the signature it creates.
Keep the raw `.bin` for serial flashing and `FIRMWARE.BIN` for USB updates;
the signature trailer is never written to the application partition.

## GitHub releases

Create a **firmware-signing** environment under **Settings → Environments**.
Require approval from a trusted reviewer, restrict deployment to tags matching
`v*`, and disable administrator bypass of the protection rules. A sole maintainer
who also creates release tags must leave **Prevent self-review** disabled to
approve their own releases. Protect creation, updates and deletion of `v*` tags
with a tag ruleset whose bypass list contains only trusted release maintainers.

Add **FIRMWARE_SIGNING_KEY** as an environment secret containing the PEM private
key. Keep the key exclusively at environment scope; a repository-wide copy would
remain accessible to jobs outside this environment's protections:

```sh
gh secret set FIRMWARE_SIGNING_KEY --env firmware-signing \
  < .signing/bootswitch-private.pem
```

Version-tag releases build and test both variants without the key, then sign
only the production application in the publish job. That job references
`firmware-signing` and waits for its required approval before starting. Review the
release tag and commit before selecting **Review deployments → Approve and deploy**.
The key is passed only to the signing step; it is never passed to branch/PR builds
or stored in an artifact. Workflow edits cannot override environment protection
settings; jobs omitting the environment cannot access its secret.
Signing fails if the secret is absent or does not match the committed public key.
The signed update is published with the other release assets and SHA256SUMS.
Development images are published unsigned.

## Custom keys and recovery

For a separate fork, generate a dedicated key pair into new paths:

```sh
python scripts/firmware_signing.py generate-key \
  --private-key .signing/custom-private.pem --public-key custom-public.pem
```

The generator refuses to overwrite keys and creates private keys with mode 0600.
Replace `keys/firmware-public.pem` with your new public key before building, and
install that build through physical download mode. Existing devices continue
to trust their embedded key. A key change can also be deployed by signing a
replacement application with the currently trusted private key; subsequent
updates use the public key embedded in that replacement.

An old signed application remains installable; there is no anti-rollback policy.
Signing a development application explicitly authorizes its unsigned updater.
The physical reset/power button still provides ROM download-mode recovery.

## Signed file format

The file contains the exact raw ESP32-S3 application followed by an 80-byte trailer:

| Trailer offset | Length | Contents |
| --- | --- | --- |
| 0 | 8 | ASCII `BSWUPD01`: BootSwitch format and version |
| 8 | 4 | Application byte length, unsigned little endian |
| 12 | 4 | Reserved flags, must be zero |
| 16 | 32 | ECDSA `r`, big endian |
| 48 | 32 | ECDSA `s`, big endian |

The signature authenticates SHA-256 of `application || trailer[0:16]`. The
running firmware supplies the trusted key; the upload cannot supply one. The
length must exactly match the file minus its trailer, and unknown flags are
rejected. The existing ESP-IDF image checks still validate the stripped
application's chip, checksum/digest and exact length before activation.

USB writes are blocked after eject/apply while verification and staging run.
Verification streams through a 4096-byte buffer and yields between chunks.
Invalid production signatures are rejected before `esp_ota_begin` can erase
the inactive slot. Interrupted writes leave the running slot selected.

This authentication is enforced by the installed application. It does not change
the bootloader or eFuses. ESP32-S3's hardware Secure Boot scheme uses RSA in
[the pinned ESP-IDF version](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/security/secure-boot-v2.html);
BootSwitch uses ECDSA for its USB update path while retaining physical flashing.
