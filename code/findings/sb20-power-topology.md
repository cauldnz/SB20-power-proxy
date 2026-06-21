# SB20 power-topology — does erg run off the right meter, and is "200 W" really 200 W?

**Status: 🔴 OPEN — the headline follow-up from bike session 4 (2026-06-21).** Designed at the desk; needs
the owner's Garmin `.FIT` + one more on-bike capture to resolve. Companion to
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

## Hypothesis (owner — strong): single-sided reading
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
