"""Tests for the ed25519 firmware signer (sb20proxy.ota.sign).

The golden-vector test is the important one: it pins the exact public key / digest / signature that
the firmware host tests (firmware/test/test_proxy/test_main.cpp,
test_ed25519_verifies_python_signature) verify with vendored monocypher. If this drifts, the C side
stops matching — both fail together, which keeps the Python signer and the device verifier in step.
"""

from __future__ import annotations

import pytest

pytest.importorskip("cryptography")  # the `ota` extra; skip if a dev installed only [dev]

from sb20proxy.ota import sign  # noqa: E402  (after importorskip by design)

# Must match the constants baked into firmware/test/test_proxy/test_main.cpp.
SEED = bytes(range(32))
IMAGE = bytes(range(256))
GOLDEN_PUBKEY = "03a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8"
GOLDEN_DIGEST = (
    "1ecc896f34d3f9cac484c73f75f6a5fb58ee6784be41b35f46067b9c65c63a67"
    "94d3d744112c653f73dd7deb6666204c5a9bfa5b46081fc10fdbe7884fa5cbf8"
)
GOLDEN_SIG = (
    "b2e355e1bbc88f6f324fe8b444197d0f301162d91fb9484523afb0e77f64e769"
    "eae7217d166ebe394e9addd3589406f01a7b614c06787bb6bff227689a63cb01"
)


def test_golden_vectors_match_firmware_constants():
    sk, pub = sign.generate_keypair(SEED)
    digest = sign.blake2b_digest(IMAGE)
    sig = sign.sign_digest(sk, digest)
    assert pub.hex() == GOLDEN_PUBKEY
    assert digest.hex() == GOLDEN_DIGEST
    assert sig.hex() == GOLDEN_SIG  # ed25519 (RFC 8032) is deterministic -> reproducible sig


def test_sign_verify_roundtrip():
    sk, pub = sign.generate_keypair()
    digest = sign.blake2b_digest(b"some firmware bytes")
    sig = sign.sign_digest(sk, digest)
    assert sign.verify_digest(pub, digest, sig)


def test_verify_rejects_tampering():
    sk, pub = sign.generate_keypair(SEED)
    digest = sign.blake2b_digest(IMAGE)
    sig = sign.sign_digest(sk, digest)
    bad_sig = bytearray(sig)
    bad_sig[0] ^= 0x01
    assert not sign.verify_digest(pub, digest, bytes(bad_sig))
    bad_digest = bytearray(digest)
    bad_digest[0] ^= 0x01
    assert not sign.verify_digest(pub, bytes(bad_digest), sig)


def test_build_manifest_shape():
    sk, _ = sign.generate_keypair(SEED)
    m = sign.build_manifest("0.2.0", "https://ota.example/fw-0.2.0.bin", IMAGE, sk)
    assert m["version"] == "0.2.0"
    assert m["url"].endswith("fw-0.2.0.bin")
    assert m["size"] == len(IMAGE)
    assert len(m["blake2b"]) == sign.DIGEST_SIZE * 2  # 128 hex chars
    assert len(m["sig"]) == 64 * 2  # ed25519 signature, 128 hex chars
    # The manifest's digest must verify under the manifest's own signature + the key's public half.
    _, pub = sign.generate_keypair(SEED)
    assert sign.verify_digest(pub, bytes.fromhex(m["blake2b"]), bytes.fromhex(m["sig"]))


def test_seed_must_be_32_bytes():
    import pytest

    with pytest.raises(ValueError):
        sign.generate_keypair(b"too short")
