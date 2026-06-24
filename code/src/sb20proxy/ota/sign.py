"""ed25519 firmware signing for the signed-pull OTA path.

The chain (must match the on-device verifier in firmware/lib/proxy/OtaVerify.h):

    digest = BLAKE2b-512(firmware.bin)         # 64 bytes; streamed on the device
    sig    = ed25519_sign(private_key, digest)  # signs the DIGEST, not the image (the device can't
                                                # hold a 1 MB image in RAM to verify)
    manifest = { version, url, size, blake2b: hex(digest), sig: hex(sig) }

The device recomputes BLAKE2b as it streams the download, checks it equals manifest.blake2b, then
ed25519-verifies `sig` over that digest against the public key baked into the firmware. Any mismatch
aborts the update and keeps the running image.

ed25519 here (``cryptography``) and monocypher's ``crypto_ed25519_*`` are both RFC 8032 (Curve25519
+ SHA-512), so a signature made here verifies on the device. BLAKE2b-512 is standard on both sides
(``hashlib.blake2b`` default vs monocypher ``crypto_blake2b``), so the digests match byte-for-byte.
"""

from __future__ import annotations

import hashlib
import json

from cryptography.hazmat.primitives.asymmetric.ed25519 import (
    Ed25519PrivateKey,
    Ed25519PublicKey,
)

DIGEST_SIZE = 64  # BLAKE2b-512


def blake2b_digest(data: bytes) -> bytes:
    """The 64-byte BLAKE2b-512 digest of an image — what gets signed (the device recomputes it)."""
    return hashlib.blake2b(data, digest_size=DIGEST_SIZE).digest()


def generate_keypair(seed: bytes | None = None) -> tuple[Ed25519PrivateKey, bytes]:
    """Return (private_key, public_key_raw_32_bytes). Pass a 32-byte ``seed`` for a deterministic
    key (used by the golden-vector tests); omit it for a fresh random release key."""
    if seed is None:
        sk = Ed25519PrivateKey.generate()
    else:
        if len(seed) != 32:
            raise ValueError("ed25519 seed must be 32 bytes")
        sk = Ed25519PrivateKey.from_private_bytes(seed)
    return sk, sk.public_key().public_bytes_raw()


def public_key_hex(sk: Ed25519PrivateKey) -> str:
    """The 32-byte public key as hex — the value baked into the firmware (Config / OtaPubKey)."""
    return sk.public_key().public_bytes_raw().hex()


def sign_digest(sk: Ed25519PrivateKey, digest: bytes) -> bytes:
    """Sign a 64-byte image digest, returning the 64-byte ed25519 signature."""
    if len(digest) != DIGEST_SIZE:
        raise ValueError(f"digest must be {DIGEST_SIZE} bytes")
    return sk.sign(digest)


def verify_digest(public_key_raw: bytes, digest: bytes, signature: bytes) -> bool:
    """Verify a signature over a digest with a raw 32-byte public key (mirrors the device check)."""
    try:
        Ed25519PublicKey.from_public_bytes(public_key_raw).verify(signature, digest)
        return True
    except Exception:
        return False


def build_manifest(version: str, url: str, image: bytes, sk: Ed25519PrivateKey) -> dict:
    """Build the manifest the device fetches: version + url + size + blake2b digest + signature."""
    digest = blake2b_digest(image)
    sig = sign_digest(sk, digest)
    return {
        "version": version,
        "url": url,
        "size": len(image),
        "blake2b": digest.hex(),
        "sig": sig.hex(),
    }


def manifest_json(version: str, url: str, image: bytes, sk: Ed25519PrivateKey) -> str:
    """``build_manifest`` serialized to compact JSON (what gets written next to the .bin)."""
    return json.dumps(build_manifest(version, url, image, sk), separators=(",", ":"))
