# 🚴 Session 5 — paired XCadey + Assioma capture (meter-to-meter calibration)

**Status: 🟡 PLANNED** · tracked in [`sessions/README.md`](sessions/README.md). Run via
[`sessions/PLAYBOOK.md`](sessions/PLAYBOOK.md) — record actuals inline, **⏱ timestamp from the start**,
retro at the end. *(Independent of session 4 — different bike/kit; do whichever is set up.)*

**Goal:** capture the **XCadey** (track-bike spider) and the **Assioma** (reference pedals) **at the same
time** so we can fit the correction that lets the proxy rebroadcast XCadey power on the Assioma scale
(`code/findings/meter-to-meter-proxy.md`). This is a **data-capture ride**, not an agent-guided live test —
the protocol below is what makes a *good* fit; send me the JSONL after.

**~35–45 min** (set-up ~10 · structured efforts ~20 · easy spin ~10). Easiest on a **trainer** (track bike
+ both meters, laptop + ANT+ stick stationary) — the velodrome makes capture awkward.

## Bring / set up
- Track bike with **both** meters fitted and awake: the **XCadey spider** *and* the **Assioma pedals**.
- Laptop + the **ANT+ USB stick** (this capture is ANT+, on one stick — both meters broadcast ANT+).
- **Both meters' ANT+ device ids** written down (from each meter's app, or a quick scan). I need these.

## Pre-flight (desk, ~2 min — per the playbook)
```bash
# WSL: attach the stick (doesn't survive reboot) — see code/findings/wsl-capture-runbook.md
usbipd list && usbipd attach --wsl --busid <BUSID>
cd code && source .venv/bin/activate
python -c "import openant; print('openant ok')"          # sanity
python scripts/07_capture_multi.py --meter test:1 --output /tmp/x.jsonl --duration 0.1 || true  # arg-path smoke
```

## The capture
Start it (run detached if you like — `run_capture.sh`), then ride:
```bash
python code/scripts/07_capture_multi.py \
  --meter xcadey:<XCADEY_ANT_ID> --meter assioma:<ASSIOMA_ANT_ID> \
  --duration 2400 \
  --output code/findings/captures/CAL-xcadey-vs-assioma-$(date +%Y%m%d-%H%M).jsonl
```
**Wake both meters (pedal) if no broadcasts appear in ~30 s.** Confirm both `source` labels are landing
(`xcadey` *and* `assioma`) before you commit to the efforts.

### Ride protocol — cover the power × cadence space (this is what fits well)
The fit is only as good as the range it sees. Aim for **steady blocks** so each (XCadey, Assioma) pair is
clean, and **vary cadence at the same power** so we can see whether the error is cadence-dependent (it was
for Stages↔Assioma — ~13% @60 rpm vs ~5% @100 rpm; XCadey may differ):

1. **Warm up** ~5 min easy.
2. **Steady blocks, ~60–90 s each**, across your range — e.g. **~120 / 160 / 200 / 240 / 280 W** (adjust
   to your numbers). **At each power, do it at two cadences** — e.g. **~75 rpm** then **~95 rpm**. Hold
   each block steady; narrate/note the target so I can line up the blocks. *(This grid is the high-value
   part — front-load it while fresh.)*
3. **A few short hard efforts** (10–20 s) to extend the top of the curve, if comfortable.
4. **Easy spin** to finish; some natural/unstructured riding is fine too (more real-world points).

## ✅ Pass / after
- The JSONL has **both** meters' `instantaneous_power_w` (+ cadence) across a good range. **Commit it** to
  `code/findings/captures/` — then I run `09_fit_calibration.py --target xcadey --ref assioma` +
  `08_analyze_grid.py` + `12_compare_fit.py`, and tell you whether power-only fits or it wants cadence.
- If a meter never appears: check its ANT+ id, that it's awake, and that nothing else holds the stick
  (`run_capture.sh` releases it; see `wsl-capture-runbook.md`).

## Retro (fill in at the end — see [`sessions/PLAYBOOK.md`](sessions/PLAYBOOK.md) §4)
- **Went well:**
- **Went wrong / slow / confusing (+ root cause):**
- **Planned vs actual (timestamps):**
- **Changes to make before next session (process / run-sheet / tooling):**
- **Next gate + desk work that must precede it:**
