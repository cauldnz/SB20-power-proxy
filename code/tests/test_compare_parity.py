"""Parity guard: the on-device C++ MeterCompare must match the Python twin.

``firmware/lib/proxy/MeterCompare.h`` and :mod:`sb20proxy.compare` are one core in two languages.
This test pins a SHARED golden dual-meter stream + the exact stats / band / grid / sample outputs,
and the SAME stream + SAME expected values are asserted in C++ by
``firmware/test/test_metercompare/test_main.cpp`` (``feedGoldenSweep`` / ``test_parity_golden``).
Change the core on one side and this test (or the C++ one) fails until both agree — the drift that
let the cadence/torque views land in C++ while this twin lagged can't recur silently.

The golden is a pure +10% scale error (B := A + A//10, exact: every A is a multiple of 10) swept
over a spread of power and cadence, so every populated band/cell reads +10.0% and the assertions pin
the BINNING (per-band pair histograms), the ratio/bias math, the pairing, and the downsample —
independent of the float32-vs-float64 difference between the C++ core and this one. The shared agree
threshold is ``AGREE_BAND_PCT`` here / ``kAgreeBandPct`` there.
"""

from __future__ import annotations

import pytest

from sb20proxy.compare import AGREE_BAND_PCT, MeterCompare

# Shared golden sweep — keep identical to feedGoldenSweep() in test_main.cpp.
# (a_watts, cadence_rpm); B := a + a // 10 (exact +10%, since every a is a multiple of 10).
_SWEEP = [
    (100, 90), (150, 90), (200, 90), (250, 90), (300, 90),
    (100, 60), (150, 60), (200, 60), (250, 60), (300, 60),
]
_REPS = 4  # -> 40 pairs


def _golden() -> MeterCompare:
    mc = MeterCompare()
    t = 0
    for _rep in range(_REPS):
        for a, cad in _SWEEP:
            b = a + a // 10  # a * 1.1, exact
            mc.on_a(a, t, cad)
            mc.on_b(b, t + 10, cad)
            t += 1000
    return mc


def test_shared_agree_threshold_matches_the_cpp():
    # The one domain rule, pinned on both sides — kAgreeBandPct in MeterCompare.h.
    assert AGREE_BAND_PCT == 2.0


def test_stats_match_the_cpp_golden():
    s = _golden().stats()
    assert s.valid is True
    assert s.n_pairs == 40
    assert (s.a_watts, s.b_watts, s.delta_w) == (300, 330, 30)  # latest pair
    assert s.mean_bias_pct == pytest.approx(10.0, abs=1e-3)
    assert s.mean_ratio == pytest.approx(1.10, abs=1e-3)
    assert s.agrees() is False  # +10% is well past the 2% band


def test_power_bands_match_the_cpp_golden():
    bands = _golden().bands()
    # The per-band pair histogram — pins the power binning. Asserted as powerBandN in test_main.cpp.
    assert [b.n_pairs for b in bands] == [0, 0, 8, 8, 8, 8, 8, 0, 0, 0, 0, 0]
    for b in bands:
        if b.n_pairs:
            assert b.mean_bias_pct == pytest.approx(10.0, abs=1e-3)
            assert b.mean_ratio == pytest.approx(1.10, abs=1e-3)


def test_torque_bands_match_the_cpp_golden():
    tb = _golden().torque_bands()
    # Torque = A / (rpm·2π/60), binned at 5 N·m. Asserted as torqueBandN in test_main.cpp.
    assert [b.n_pairs for b in tb] == [0, 0, 4, 8, 8, 4, 8, 4, 0, 4, 0, 0]
    for b in tb:
        if b.n_pairs:
            assert b.mean_bias_pct == pytest.approx(10.0, abs=1e-3)


def test_grid_matches_the_cpp_golden():
    g = _golden().grid2d()
    counts = [[g.cells[pi][ci].n_pairs for ci in range(6)] for pi in range(8)]
    # cadence 60 -> c-bin 1, cadence 90 -> c-bin 3; power 100..300 W -> p-bins 2..6. Asserted as
    # gridN in test_main.cpp.
    assert counts == [
        [0, 0, 0, 0, 0, 0],  # p-bin 0 (0-50 W)
        [0, 0, 0, 0, 0, 0],  # p-bin 1 (50-100 W)
        [0, 4, 0, 4, 0, 0],  # p-bin 2 (100-150 W)
        [0, 4, 0, 4, 0, 0],  # p-bin 3 (150-200 W)
        [0, 4, 0, 4, 0, 0],  # p-bin 4 (200-250 W)
        [0, 4, 0, 4, 0, 0],  # p-bin 5 (250-300 W)
        [0, 4, 0, 4, 0, 0],  # p-bin 6 (300-350 W)
        [0, 0, 0, 0, 0, 0],  # p-bin 7 (350-400 W)
    ]
    for pi in range(8):
        for ci in range(6):
            if g.cells[pi][ci].n_pairs:
                assert g.cells[pi][ci].mean_bias_pct == pytest.approx(10.0, abs=1e-3)


def test_sample_pairs_match_the_cpp_golden():
    sp = _golden().sample_pairs(max_n=12)  # step = 40 // 12 = 3 -> 14 samples
    got = [(p.a, p.b) for p in sp]
    # The exact downsample (every 3rd pair). Asserted as expectPairs in test_main.cpp.
    assert got == [
        (100, 110), (250, 275), (150, 165), (300, 330), (200, 220),
        (100, 110), (250, 275), (150, 165), (300, 330), (200, 220),
        (100, 110), (250, 275), (150, 165), (300, 330),
    ]
