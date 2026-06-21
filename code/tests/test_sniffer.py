"""Headless nRF-Sniffer address helpers (the pure half of scripts/sniff_ble.py).

The SnifferAPI serial I/O is the hardware seam; here we pin the address parsing +
the device-match predicate that decides which advertiser the sniffer follows.
"""

from __future__ import annotations

import pytest

from sb20proxy.ble.sniffer import address_matches, format_mac, parse_mac

SB20 = "E4:AA:5A:D6:0E:D4"  # the real bike, per findings/nrf-sniffer.md


def test_parse_mac_accepts_separators_and_case():
    want = [0xE4, 0xAA, 0x5A, 0xD6, 0x0E, 0xD4]
    assert parse_mac(SB20) == want
    assert parse_mac("e4:aa:5a:d6:0e:d4") == want
    assert parse_mac("E4-AA-5A-D6-0E-D4") == want
    assert parse_mac(" E4.AA.5A.D6.0E.D4 ") == want


@pytest.mark.parametrize("bad", [
    "E4:AA:5A:D6:0E",            # only 5 octets
    "E4:AA:5A:D6:0E:D4:99",      # 7 octets
    "E4:AA:5A:D6:0E:GG",         # non-hex
    "E4:AA:5A:D6:0E:1FF",        # out of range
    "",
])
def test_parse_mac_rejects_bad(bad):
    with pytest.raises(ValueError):
        parse_mac(bad)


def test_format_mac_ignores_type_byte():
    # the sniffer's 7-int list (6 MAC bytes display-order + address-type) -> MAC string
    assert format_mac([0xE4, 0xAA, 0x5A, 0xD6, 0x0E, 0xD4, 0]) == "e4:aa:5a:d6:0e:d4"
    assert format_mac([0xE4, 0xAA, 0x5A, 0xD6, 0x0E, 0xD4, 1]) == "e4:aa:5a:d6:0e:d4"


def test_format_mac_rejects_short():
    with pytest.raises(ValueError):
        format_mac([0xE4, 0xAA, 0x5A])


def test_address_matches_is_case_insensitive_and_type_agnostic():
    addr_public = [0xE4, 0xAA, 0x5A, 0xD6, 0x0E, 0xD4, 0]
    addr_random = [0xE4, 0xAA, 0x5A, 0xD6, 0x0E, 0xD4, 1]
    assert address_matches(addr_public, SB20)
    assert address_matches(addr_random, "e4:aa:5a:d6:0e:d4")
    # a different device must not match
    assert not address_matches([0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0], SB20)
    # malformed inputs are a clean False, never a raise
    assert not address_matches([0xE4, 0xAA], SB20)
    assert not address_matches(addr_public, "not-a-mac")
