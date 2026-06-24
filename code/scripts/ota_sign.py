#!/usr/bin/env python3
"""Sign a firmware image for the signed-pull OTA path, and manage the signing key.

    # one-time: make a release key (keep ota_key.pem OFFLINE + backed up; it is the crown jewel)
    python scripts/ota_sign.py keygen --out ota_key.pem

    # print the public key to bake into the firmware (Config::OTA_PUBKEY / a generated header)
    python scripts/ota_sign.py pubkey --key ota_key.pem

    # sign a build -> writes manifest.json next to it (or --out)
    python scripts/ota_sign.py sign --key ota_key.pem --version 0.2.0 \
        --url https://ota.example/fw-0.2.0.bin --bin firmware.bin --out manifest.json

The device (firmware/lib/proxy/OtaVerify.h, vendored monocypher) recomputes the BLAKE2b digest as it
downloads and ed25519-verifies the manifest signature over it before applying. See
code/findings/ota-update-plan.md. Requires the `ota` extra: pip install -e ".[ota]".
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

from sb20proxy.ota import sign


def _load_key(path: Path) -> Ed25519PrivateKey:
    key = serialization.load_pem_private_key(path.read_bytes(), password=None)
    if not isinstance(key, Ed25519PrivateKey):
        sys.exit(f"{path} is not an ed25519 private key")
    return key


def cmd_keygen(args: argparse.Namespace) -> None:
    out = Path(args.out)
    if out.exists() and not args.force:
        sys.exit(f"{out} exists; refusing to overwrite (pass --force). NEVER lose/leak this key.")
    sk = Ed25519PrivateKey.generate()
    out.write_bytes(
        sk.private_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PrivateFormat.PKCS8,
            encryption_algorithm=serialization.NoEncryption(),
        )
    )
    print(f"wrote {out} (KEEP OFFLINE + BACK UP — losing it means no more signed updates)")
    print(f"public key: {sign.public_key_hex(sk)}")


def cmd_pubkey(args: argparse.Namespace) -> None:
    sk = _load_key(Path(args.key))
    pub = sk.public_key().public_bytes_raw()
    print(sign.public_key_hex(sk))
    # C array for embedding in the firmware.
    body = ", ".join(f"0x{b:02x}" for b in pub)
    print(f"\n// firmware: const uint8_t OTA_PUBKEY[32] = {{{body}}};")


def cmd_sign(args: argparse.Namespace) -> None:
    sk = _load_key(Path(args.key))
    image = Path(args.bin).read_bytes()
    manifest = sign.build_manifest(args.version, args.url, image, sk)
    out = Path(args.out) if args.out else Path(args.bin).with_suffix(".manifest.json")
    out.write_text(json.dumps(manifest, separators=(",", ":")))
    print(f"wrote {out}  (version={manifest['version']} size={manifest['size']} bytes)")


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    g = sub.add_parser("keygen", help="generate a release signing key (PEM)")
    g.add_argument("--out", default="ota_key.pem")
    g.add_argument("--force", action="store_true")
    g.set_defaults(func=cmd_keygen)

    k = sub.add_parser("pubkey", help="print the public key (+ a C array to embed)")
    k.add_argument("--key", required=True)
    k.set_defaults(func=cmd_pubkey)

    s = sub.add_parser("sign", help="sign a firmware.bin -> manifest.json")
    s.add_argument("--key", required=True)
    s.add_argument("--version", required=True)
    s.add_argument("--url", required=True)
    s.add_argument("--bin", required=True)
    s.add_argument("--out")
    s.set_defaults(func=cmd_sign)

    args = p.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
