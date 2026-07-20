"""Live A/B power-meter comparison — the Python twin of firmware/lib/proxy/MeterCompare.h.

Feed timestamped watts (and an optional cadence) from two meters; :class:`MeterCompare` pairs the
freshest samples within a small time window and keeps rolling agreement stats: the latest paired
readings + delta, the rolling mean ratio (B/A) and bias (%), a pair count, and the band views — by
power, by TORQUE, and a power×cadence grid — plus a downsampled scatter for a Bland-Altman plot.
This is the always-on "do these two agree?" surface the head-unit's LVGL Compare screen and the web
deep-dive (``GET /compare``) render; it is distinct from :mod:`sb20proxy.calibration`, which fits a
*correction* from paired samples.

Pure logic, no I/O: the bleak/CLI seam lives in ``scripts/compare_meters.py`` (mirroring the
:mod:`sb20proxy.ble.multi_capture` module ↔ ``scripts/capture_ble_multi.py`` seam split). The parity
guard ``tests/test_compare_parity.py`` pins a shared golden dataset that this module and the C++
core (``firmware/test/test_metercompare/test_main.cpp``) both assert, so the twins can't drift —
the very drift that let the torque views land in C++ while this twin lagged.
"""

from __future__ import annotations

from dataclasses import dataclass

# |bias| below this reads as "these meters agree" — the shared rule every Compare surface uses
# (the head-unit verdict, the web verdict, this twin). Mirrors kAgreeBandPct in MeterCompare.h: one
# threshold, one place. If it moves, it moves on both sides of the twin.
AGREE_BAND_PCT = 2.0

# Power-band table: 12 bands of 50 W (0..600 W). Mirrors kBandW / kBands.
BAND_W = 50
BANDS = 12
# Torque-band table: 12 bands of 5 N·m (0..60 N·m). Mirrors kTorqueBandNm / kTorqueBands.
TORQUE_BAND_NM = 5
TORQUE_BANDS = 12
# Power×cadence grid: 8 power bins (0..400 W, 50 W) × 6 cadence bins (<45 rpm, then 15 rpm each).
GRID_P_BINS = 8
GRID_C_BINS = 6
GRID_P_BIN_W = 50
GRID_C_BIN_LO = 45
GRID_C_BIN_W = 15
# Rolling-window depth — the 512-pair cap the C++ fixed ring holds.
MAX_PAIRS = 512

# rad/s per rpm (2π/60): torque N·m = W / (rpm · this). Mirrors the C++ literal 0.10471976f.
_RPM_TO_RADS = 0.10471976


@dataclass
class MeterBand:
    """Per-band agreement. ``lo_w`` is the band's lower edge — watts for :meth:`MeterCompare.bands`,
    N·m for :meth:`MeterCompare.torque_bands`. ``mean_ratio`` ~1.0 ⇒ they agree in-band."""

    lo_w: int = 0
    n_pairs: int = 0
    mean_ratio: float = 0.0  # mean(b/a) in this band (0 if empty)
    mean_bias_pct: float = 0.0  # mean((b-a)/a*100)


@dataclass
class MeterGridCell:
    """One cell of the power×cadence agreement grid (the web heatmap)."""

    n_pairs: int = 0
    mean_bias_pct: float = 0.0


@dataclass
class Grid2D:
    """The power×cadence agreement grid: ``cells[p_bin][c_bin]`` plus the axis parameters."""

    cells: list[list[MeterGridCell]]
    p_bin_w: int = GRID_P_BIN_W
    c_bin_lo: int = GRID_C_BIN_LO
    c_bin_w: int = GRID_C_BIN_W


@dataclass
class SamplePair:
    """One (a, b) point for a Bland-Altman scatter."""

    a: int
    b: int


@dataclass
class MeterCompareStats:
    """The rolling agreement summary (twin of the C++ MeterCompareStats)."""

    valid: bool = False  # at least one usable pair?
    a_watts: int = 0  # latest paired readings
    b_watts: int = 0
    delta_w: int = 0  # b - a (latest pair)
    mean_ratio: float = 1.0  # rolling mean of b/a (1.0 = perfect agreement)
    mean_bias_pct: float = 0.0  # rolling mean of (b-a)/a * 100
    n_pairs: int = 0  # pairs in the rolling window

    def agrees(self) -> bool:
        return -AGREE_BAND_PCT < self.mean_bias_pct < AGREE_BAND_PCT


def _bias_pct(a: int, b: int) -> float:
    return (b - a) / a * 100.0


def _ratio_from_bias(bias: float) -> float:
    # mean(b/a) === 1 + mean(bias%)/100, so the ratio is derived, never summed a second time.
    return 1.0 + bias / 100.0


def _clamp_idx(idx: int, n: int) -> int:
    return 0 if idx < 0 else (n - 1 if idx >= n else idx)


class _Acc:
    """Every band/grid table reduces to this: count pairs, sum their bias, divide."""

    __slots__ = ("n", "sum_bias")

    def __init__(self) -> None:
        self.n = 0
        self.sum_bias = 0.0

    def add(self, bias: float) -> None:
        self.n += 1
        self.sum_bias += bias

    def mean(self) -> float:
        return self.sum_bias / self.n if self.n else 0.0


def _finish(bands: list[MeterBand], acc: list[_Acc]) -> None:
    for band, a in zip(bands, acc, strict=True):
        band.n_pairs = a.n
        if a.n:
            band.mean_bias_pct = a.mean()
            band.mean_ratio = _ratio_from_bias(band.mean_bias_pct)


class MeterCompare:
    """Pairing + rolling agreement stats for two power streams (twin of the C++ MeterCompare).

    ``pair_window_ms``: A and B samples closer than this in time form a pair. ``min_w`` guards the
    ratio/bias math against tiny denominators (coasting). ``max_pairs`` is the ring depth — the
    newest pair evicts the oldest, mirroring the C++ fixed ring.
    """

    def __init__(self, pair_window_ms: int = 700, min_w: int = 20, max_pairs: int = MAX_PAIRS):
        self.pair_window_ms = pair_window_ms
        self.min_w = min_w
        self.max_pairs = max_pairs
        # each pair is (a_watts, b_watts, a_cadence); insertion order is oldest→newest.
        self._pairs: list[tuple[int, int, int]] = []
        self._a: tuple[int, int, int] | None = None  # (watts, t_ms, cadence)
        self._b: tuple[int, int, int] | None = None
        self._seq_a = self._seq_b = 0
        self._paired = (0, 0)

    # cadence (rpm) is optional: -1 = unknown (keeps power-only behaviour). It enables the torque
    # and power×cadence views — torque from meter A (the reference): Nm = W / (rpm·2π/60).
    def on_a(self, watts: int, t_ms: int, cadence: int = -1) -> None:
        self._seq_a += 1
        self._a = (watts, t_ms, cadence)
        self._try_pair()

    def on_b(self, watts: int, t_ms: int, cadence: int = -1) -> None:
        self._seq_b += 1
        self._b = (watts, t_ms, cadence)
        self._try_pair()

    def reset(self) -> None:
        self._pairs.clear()
        self._seq_a = self._seq_b = 0
        self._paired = (0, 0)

    def _try_pair(self) -> None:
        if self._seq_a == 0 or self._seq_b == 0:  # need at least one of each
            return
        if abs(self._a[1] - self._b[1]) > self.pair_window_ms:  # not co-temporal
            return
        if (self._seq_a, self._seq_b) == self._paired:  # already paired this duo
            return
        self._paired = (self._seq_a, self._seq_b)
        # cadence comes from A (the reference), like the C++ core (ring stores latestA_.cad).
        self._pairs.append((self._a[0], self._b[0], self._a[2]))
        if len(self._pairs) > self.max_pairs:  # full ring: the oldest pair is overwritten
            self._pairs.pop(0)

    def pair_count(self) -> int:
        return len(self._pairs)

    def stats(self) -> MeterCompareStats:
        if not self._pairs:
            return MeterCompareStats()
        a, b, _cad = self._pairs[-1]
        s = MeterCompareStats(valid=True, a_watts=a, b_watts=b, delta_w=b - a,
                              n_pairs=len(self._pairs))
        acc = _Acc()
        for pa, pb, _c in self._pairs:
            if pa < self.min_w:  # ignore near-zero denominators
                continue
            acc.add(_bias_pct(pa, pb))
        if acc.n:
            s.mean_bias_pct = acc.mean()
            s.mean_ratio = _ratio_from_bias(s.mean_bias_pct)  # else the 1.0 neutral default stands
        return s

    def bands(self) -> list[MeterBand]:
        """Per-power-band table (indexed by watts // BAND_W). Out-of-range power is DROPPED (unlike
        torque_bands, which clamps) — a 700 W spike belongs in no band here."""
        out = [MeterBand(lo_w=i * BAND_W) for i in range(BANDS)]
        acc = [_Acc() for _ in range(BANDS)]
        for a, b, _cad in self._pairs:
            if a < self.min_w:
                continue
            idx = a // BAND_W
            if idx < 0 or idx >= BANDS:
                continue
            acc[idx].add(_bias_pct(a, b))
        _finish(out, acc)
        return out

    def torque_bands(self) -> list[MeterBand]:
        """Bias by TORQUE band (5 N·m) — reveals torque-dependent error that power bins mix away.
        Torque from meter A: Nm = W / (rpm · 2π/60). Pairs with unknown cadence are skipped; sprints
        past the top edge CLAMP into the last band rather than vanishing from the view."""
        out = [MeterBand(lo_w=i * TORQUE_BAND_NM) for i in range(TORQUE_BANDS)]
        acc = [_Acc() for _ in range(TORQUE_BANDS)]
        for a, b, cad in self._pairs:
            if a < self.min_w or cad <= 0:
                continue
            torque = a / (cad * _RPM_TO_RADS)
            acc[_clamp_idx(int(torque / TORQUE_BAND_NM), TORQUE_BANDS)].add(_bias_pct(a, b))
        _finish(out, acc)
        return out

    def grid2d(self) -> Grid2D:
        """The power×cadence agreement grid (the web heatmap): cell[power_bin][cadence_bin]."""
        cells = [[MeterGridCell() for _ in range(GRID_C_BINS)] for _ in range(GRID_P_BINS)]
        acc = [[_Acc() for _ in range(GRID_C_BINS)] for _ in range(GRID_P_BINS)]
        for a, b, cad in self._pairs:
            if a < self.min_w or cad <= 0:
                continue
            pi = _clamp_idx(a // GRID_P_BIN_W, GRID_P_BINS)
            ci = _clamp_idx(int((cad - GRID_C_BIN_LO) / GRID_C_BIN_W), GRID_C_BINS)
            acc[pi][ci].add(_bias_pct(a, b))
        for pi in range(GRID_P_BINS):
            for ci in range(GRID_C_BINS):
                cells[pi][ci].n_pairs = acc[pi][ci].n
                cells[pi][ci].mean_bias_pct = acc[pi][ci].mean()
        return Grid2D(cells=cells)

    def sample_pairs(self, max_n: int = 120) -> list[SamplePair]:
        """Downsampled (a, b) pairs for a Bland-Altman scatter (every k-th pair, up to ~max_n)."""
        n = len(self._pairs)
        if n == 0 or max_n <= 0:
            return []
        step = max(1, n // max_n)
        return [SamplePair(a, b) for a, b, _cad in self._pairs[::step]]
