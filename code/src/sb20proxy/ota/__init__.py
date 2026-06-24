"""Signed-OTA release tooling: sign a firmware image (ed25519 over a BLAKE2b digest) and
emit the manifest the device fetches + verifies. The device verifies with vendored monocypher
(firmware/lib/monocypher) — this is the dev/release-only signing half. See
code/findings/ota-update-plan.md.
"""

from .sign import (
    DIGEST_SIZE,
    blake2b_digest,
    build_manifest,
    generate_keypair,
    public_key_hex,
    sign_digest,
    verify_digest,
)

__all__ = [
    "DIGEST_SIZE",
    "blake2b_digest",
    "build_manifest",
    "generate_keypair",
    "public_key_hex",
    "sign_digest",
    "verify_digest",
]
