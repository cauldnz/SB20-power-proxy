# 🚴 Calibration Ride Card — session 2

**Two objectives, one capture:**
1. **Close open-question #7** — does the SB20 rescale crank power? (Decides whether
   "feed Assioma → erg targets land on Assioma watts" is literally true, or needs a
   one-number bike correction.)
2. **Map the Stages↔Assioma offset surface** and find empirically how few cells a
   real calibration needs (research / optional — see `decisions.md`; the proxy
   doesn't strictly need this, but it's good science and a blog result).

Everything is captured in **one same-clock multi-source recording** — Stages crank +
Assioma + the bike's own FE-C output, all on one stick. **No watch this time.**

**Time on bike: ~22 min.** Claude drives the capture and calls every cell in chat
(the model that worked last time — you just ride). Keep this chat open.

---

## Bike setup (important)

- **⚠️ CRANK LENGTH — do this first, it's a direct multiplicative confound.**
  Both meters compute power as F × ω × L, each using its *own* configured crank
  length, and the SB20 cranks are adjustable (165–175 mm). The Stages/Assioma
  ratio ≈ ratio of the two configured lengths, so a mismatch fakes up to several
  percent of "offset". Make all three agree **before** riding:
  1. Note the **physical hole** the pedals are actually in (mm).
  2. **SB20 app** → crank length = that value.
  3. **Favero Assioma app** → crank length = the *same* value.
  (Tell Claude the three values — a current mismatch may explain part of day-1's
  1.085 ratio.)
- Put the bike in **LEVEL / resistance mode, NOT erg.** We want *you* freely
  choosing power and cadence; erg would fight the cadence targets.
- Make sure your **power and cadence are visible** (bike app / head unit) — you'll
  hold a target cadence and adjust resistance to hit a power.
- Cranks fresh-ish batteries (the crank read ~2.6 V last time — fine, but a fresh
  CR2032 removes a variable).

## Pre-flight (Claude does this before you clip in — ~60 s)

Per `code/findings/wsl-capture-runbook.md`:
1. `usbipd attach` the stick; confirm `lsusb` sees `0fcf` in Ubuntu-24.04.
2. Confirm the device node is `0666` (chmod once if not — clipboard command).
3. **Find the bike's FE-C id:** `openant scan` (look for a FitnessEquipment /
   `FEC ####` device) — or just use `--fec-id 0` (wildcard) in the capture.
4. 15 s throwaway capture to confirm all three sources broadcast, then stop.

## The capture (Claude launches, detached)

```bash
# device ids: Stages crank 62144, Assioma 17039, bike FE-C wildcard (0)
python code/scripts/07_capture_multi.py \
    --stages-id 62144 --assioma-id 17039 --fec-id 0 \
    --duration 1500 \
    --output code/findings/captures/CAL-multi-$(date +%Y%m%d-%H%M%S).jsonl
```
(launched via the robust procedure in the runbook so the stick can't get stuck)

---

## The ride

### Warm-up — 3 min, easy spin
Settle in. This also lets Claude confirm all three streams (Stages / Assioma /
bike-FEC) are flowing before the real work.

### Grid — hold each cell ~60–75 s, steady. Organised by cadence.

You hold the **cadence**; Claude calls a **resistance level** to land roughly the
target power, and watches your live numbers. Exact watts don't matter — spanning
the space and holding steady does. Brief soft-pedal between cells is fine.

| Cadence | Cell A (easy) | Cell B (mod) | Cell C (hard) |
|---|---|---|---|
| **~60 rpm** (grind) | ~150 W | ~250 W | ~330 W |
| **~80 rpm** | ~150 W | ~250 W | ~330 W |
| **~100 rpm** (spin) | ~150 W | ~250 W | ~330 W |

That's 9 cells. The same ~150/250/330 W at three cadences is exactly the contrast
that reveals whether the offset is torque-shaped — and lets `08_analyze_grid.py`
tell us if a future calibration can be a short cadence/torque sweep instead of a
full grid.

### Sprints — the high-power corner (your 800–1000 W+ range)
Big gear, **~12 s all-out**, then ~90 s easy recovery. **×4.**
- This probes the extreme top corner the grid can't reach, and stress-tests the
  decoders (12-bit FE-C power field, high crank torque values).
- Don't worry about cadence here — just maximum watts. Claude logs the peak each time.

### Cool-down — 2 min easy, then stop. Done.

---

## After the ride (Claude runs)

```bash
python code/scripts/08_analyze_grid.py --input 'code/findings/captures/CAL-multi-*.jsonl'
```
Outputs: the **#7 verdict** (bike-FEC vs crank — pass-through or a factor),
the **ratio surface** (power × cadence), **which dimension drives the offset**
(cadence / power / torque R²), and **grid-design guidance** (how few cells we
actually need). Then commit the capture + write up.

---

## If something goes wrong
- Stick stuck / "Resource busy" / wrong-state → runbook §3/§2 (kill exact PID via
  `fuser`, relaunch). Claude handles it; you keep spinning.
- FE-C source shows zero records → the wildcard didn't lock; scan for the `FEC ####`
  id and relaunch with `--fec-id <that>`. (Stages + Assioma will still be recording.)
- Anything confusing → just say what you see; Claude's watching the data live.
