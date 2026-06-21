# SB20 power-topology — does erg run off the right meter, and is "200 W" really 200 W?

**Status: 🟠 PHASE 1 DONE (2026-06-21) — single-sided REFUTED.** The dense FIT reconciliation shows the SB20
reads a fairly flat **~1.3× below the Assioma (~70–75 % of total), not the ~2×** the live spot-read implied.
Mechanism still open → **Phase 2 (simultaneous multi-device capture)** still needed. Companion to
[`shifter-erg-control.md`](shifter-erg-control.md) and [`ftms-protocol.md`](ftms-protocol.md).

## The question
Session 4 §C proved the SB20 **accepts + holds** a third-party erg Set-Target-Power. But an independent
power read (owner's Garmin = Assioma over ANT+, plus the ESP's `src_power_w`) showed the SB20's erg power is
**well below the Assioma**, and the gap is large + variable:

| SB20 erg target | SB20 reported | Assioma (real) | ratio |
|---|---|---|---|
| 200 W (dedicated hold) | ~200 W | ~260 W (Garmin) | 1.30× |
| 100 / 150 W (sweep) | 113 / 153 W | 152 / 248 W | 1.34× / 1.62× |
| 200 W (later) | ~200 W | **~380 W (Garmin)** | **~1.9×** |

So "200 W" on the SB20 erg was a **~260–380 W effort at the pedals**. This breaks the premise that erg
targets == real watts, and it questions **which meter the SB20 even uses**.

## Phase 1 RESULT (2026-06-21) — dense FIT reconciliation: single-sided REFUTED; ~1.3× flat under-read

Reconciled the owner's Garmin `.FIT` (dense Assioma over ANT+, *with L/R balance*) against the SB20 erg
captures (FIT UTC +10 h → machine local, confirmed by FIT lap 2 @ 11:03:50 bracketing the 3-way sweep).
Per stable hold, **Assioma total / SB20**:

| run / target | SB20 | Assioma (FIT) | ratio | L-balance | SB20 ÷ Assioma-LEFT |
|---|---|---|---|---|---|
| **erg200 / 200 W** (n=66, cleanest) | 175 W | 229 W | **1.31×** | 46 % L | 1.65 |
| erg3way / 100 W | 113 W | 156 W | 1.38× | 47 % L | 1.54 |
| erg3way / 150 W (n=20, looser) | 153 W | 248 W | 1.62× | 45 % L | 1.37 |
| erg3way / 200 W (SB20 release-tail-contaminated) | 143 W | 190 W | 1.33× | 48 % L | 1.56 |
| *ergC / 100 W (idle/coast — FIT started 09:51, after the real holds; discard)* | 37 W | 38 W | 1.04× | 44 % | — |

- **Single-sided is REFUTED.** L/R balance is **~46 % left (roughly even)** and the SB20 reads **~1.5× the
  Assioma's *left leg*** (not ≈ it), so it is **not** reading one leg. A clean ~2× would require SB20 ≈ one
  leg; it doesn't hold. (Stages-L doubled would give ~0.9×, single ~2.2× — neither matches either.)
- **It's a ~1.3× under-read** — SB20 ≈ **70–75 % of Assioma total**, roughly flat 100–200 W. Best estimate
  from the cleanest/longest hold (erg200, n=66): **1.31×**. Shorter erg3way holds spread 1.3–1.6× (short
  windows + ±1 s alignment + the 200 W hold's release-tail contamination).
- **The live "~380 vs 200 ≈ 1.9×" was a transient spot-read**, not the steady ratio — the value of dense
  data over eyeballing. Steady gap ≈ **30 %**, not 2×.
- **Mechanism still open:** ~0.73× total is an odd factor — not single/dual-sided, and it *conflicts* with
  the prior "Stages reads ~5–13 % high vs Assioma" (so the SB20 likely isn't simply echoing a Stages
  stream). Most consistent with the **SB20's own meter (or its erg-control source) reading ~30 % low** —
  but that's for Phase 2 to pin. *(Method: `analyze_fit_reconcile` over the committed `.FIT` +
  `G-sb20-ftms-erg*` captures; ad-hoc, not committed — reproducible from the committed data.)*

## ~~Hypothesis (owner — strong): single-sided reading~~ — ⚠️ REFUTED by Phase 1 (above); kept for the record
A **~2×** gap is the fingerprint of one meter measuring a **single leg** while the other measures **true
total**. The Assioma DUO reports dual-sided total (Garmin ~380 W). The SB20's ~200 W ≈ **one leg**. Most
likely: the SB20 runs erg off the **real Stages LEFT crank, single-sided** — **not** the ESP/Assioma spoof.
Support: SB20 IBD (~200) ≠ ESP `src_power_w` (~380, a ~1:1 passthrough of the Assioma) → the SB20 is **not
ESP-fed**; the **variable** 1.3–1.9× ratio = **L/R imbalance** (a single-left source swings with leg
dominance). **Tension to verify, don't assume:** prior `decisions.md` had Stages reading ~5–13 % *high* vs
Assioma — but that was the *combined* `62144` stream, not single-L.

## The investigation
### Phase 1 — desk reconcile (cheap; do first; needs the Garmin `.FIT`)
The owner hit **lap** at each erg run, so the `.FIT` (dense Assioma, ANT+) aligns to the SB20 captures
(`G-sb20-ftms-erg-…0949`, `…erg200-104341`, `…erg3way-110555`) on the 100/150/200 step fingerprint + the lap
marks. Compute the SB20-vs-Assioma ratio over each *stable* hold → is it a **flat scale** or a **curve**,
and how much variance is L/R imbalance? The SQLite layer from `feat/sqlite-analysis-layer`
(`13_build_sqlite.py`) is built for exactly this time-aligned join.

### Phase 2 — simultaneous multi-device capture (on-bike; definitive)
Capture **all meters at once, both transports**, during steady holds:
- **SB20 FTMS** (`0x2AD2` Indoor Bike Data) — what the erg controls to / reports.
- **Stages cranks** — `62144` (L/combined) **and** `4963` (R), ANT+ **and** BLE CPS — single vs combined vs dual.
- **Assioma** — `17039` (L) **and** `22428` (R), ANT+ **and** BLE — true L/R + total.
Reconcile on one clock. Settles: (a) single-vs-dual-sided per meter, (b) **which meter the SB20 erg uses**,
(c) the true SB20-erg-watts → real-watts correction.
**Cheap diagnostic:** pull the real L-crank battery (the G2 setup) — if the SB20 keeps getting power it's
ESP-fed; if it drops, it was on the real Stages crank.

## Prerequisites (pre-stage at the desk — don't discover these on the bike)
- The owner's **Garmin `.FIT`** from session 4.
- **ANT+ permission fix** — the in-session ANT+ capture died `[Errno 13]` (MODE-0666 udev rule present but
  WSL has no systemd to apply it). Enable WSL systemd (`/etc/wsl.conf` → `[boot] systemd=true` +
  `wsl --shutdown`), or `sudo udevadm control --reload-rules && sudo udevadm trigger`, or run as root.
  Verify with a libusb claim test. (See `sessions/PLAYBOOK.md`.)
- A tool that subscribes/pairs **multiple meters at once** — `07_capture_multi.py` already does paired ANT+
  (`--meter LABEL:ANTID` ×N); extend it (or pair with a BLE multi-subscribe) for the both-transport view.

## Why it matters
If the SB20 ergs off a single-sided Stages crank, the shifter-erg feature (and any erg use) targets
half-ish, imbalance-skewed watts. The project premise — feed the SB20 accurate dual-sided Assioma power via
the spoof — **may not even be in effect**. Resolving this decides whether the spoof must be the SB20's
*only* crank (real cranks unpaired) for erg to be honest.
