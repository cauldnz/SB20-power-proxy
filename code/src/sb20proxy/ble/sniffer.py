"""Pure helpers for the headless nRF Sniffer capture (``scripts/sniff_ble.py``).

The SnifferAPI serial I/O is the hardware seam (it lives in the script); the
BLE-address parsing and the device-match predicate are here so they're host-tested
without a dongle.

The nRF Sniffer represents a device address as a list of 7 ints: **6 MAC bytes in
display order** (MSB first, i.e. ``E4`` then ``AA`` … for ``E4:AA:…``) followed by a
final **address-type** byte (``0`` = public, ``1`` = random). To follow a target we
scan, then match a discovered device by formatting its address list and comparing it
case-insensitively to the wanted MAC — no manual byte-order juggling, no constructing
an address by hand. See ``findings/nrf-sniffer.md``.
"""

from __future__ import annotations

from collections.abc import Sequence

_SEPARATORS = ("-", ".")


def parse_mac(text: str) -> list[int]:
    """``"E4:AA:5A:D6:0E:D4"`` → ``[0xE4, 0xAA, 0x5A, 0xD6, 0x0E, 0xD4]``.

    Accepts ``:``, ``-`` or ``.`` separators (case-insensitive). Raises
    ``ValueError`` on anything that isn't exactly six hex octets in range.
    """
    cleaned = text.strip()
    for sep in _SEPARATORS:
        cleaned = cleaned.replace(sep, ":")
    parts = [p for p in cleaned.split(":") if p != ""]
    if len(parts) != 6:
        raise ValueError(f"expected 6 hex octets, got {text!r}")
    try:
        octets = [int(p, 16) for p in parts]
    except ValueError as exc:
        raise ValueError(f"non-hex octet in {text!r}") from exc
    if any(o < 0 or o > 0xFF for o in octets):
        raise ValueError(f"octet out of range in {text!r}")
    return octets


def format_mac(address: Sequence[int]) -> str:
    """A sniffer address list (≥6 bytes, display order) → ``"e4:aa:5a:d6:0e:d4"``.

    Only the first six bytes are used; a trailing address-type byte is ignored.
    """
    if len(address) < 6:
        raise ValueError(f"address needs >=6 bytes, got {address!r}")
    return ":".join(format(int(b) & 0xFF, "02x") for b in address[:6])


def address_matches(address: Sequence[int], target_mac: str) -> bool:
    """True iff the sniffer device ``address`` is the ``target_mac`` (type byte ignored)."""
    try:
        return format_mac(address) == format_mac(parse_mac(target_mac))
    except (ValueError, TypeError):
        return False
