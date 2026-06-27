# 🚴 Session 5 — meter-to-meter calibration ride (XCadey → reads like Assioma)

**Status: 🟢 READY** · tracked in [`sessions/README.md`](README.md). Run via [`PLAYBOOK.md`](PLAYBOOK.md)
(record actuals inline, **⏱ timestamp from the start**, retro at the end). Opportunistic — runs whenever
the track bike (both meters fitted) is set up.

**Goal:** calibrate on the device so the **XCadey** crank reads like the **Assioma** pedals, then leave
the proxy rebroadcasting the corrected XCadey under its own name for the **Garmin** to pair to. This is
now a **single on-bike `/calibrate` session** — the on-device wizard replaces the old ANT+
capture→desk-fit→deploy plan (see [`meter-to-meter-proxy.md`](../code/findings/meter-to-meter-proxy.md)).

**~20–30 min** (set-up ~10 · sweep ~10 · confirm ~5). Easiest on a **trainer** (track bike + both meters,
your phone for the wizard).

## What's already verified (desk — so the bike is only for what needs it)
- The corrector **run path** is bench-proven (`fake_meter` → corrected rebroadcast as a clean CPS meter).
- The wizard **renders + its routes work over WiFi** (`/calibrate`, scan, the candidate list).
- A **ride-blocking form-POST bug was found + fixed** (PR #107) — the wizard's Start/Save now actually
  save. Both boards are on current firmware.
- **The one thing the bike proves:** two meters connected **at once** (coex on the C3) + a real fit. ⤵

## Bring / set up
- Track bike with **both** meters fitted + awake: the **XCadey** (DUT, to correct) *and* the **Assioma**
  (reference). Pedal to wake them.
- A **proxy board** (COM10 or COM9 — both on current firmware) powered on the bike.
- Your **phone** (joins the board's WiFi / your network to drive the wizard) and the **Garmin**.

## Pre-flight (off the bike — the hard gate; don't pedal until green)
1. Power the board; open **`http://sb20proxy.local/`** (or its IP) on your phone. Dashboard loads.
2. Tap **Calibrate a meter** → `/calibrate`. Tap **Scan**; **confirm BOTH meters appear** (the XCadey and
   the Assioma — pedal them if not). *If only one shows, the other isn't advertising — wake it; don't start.*
3. Set **XCadey = DUT**, **Assioma = Ref** (the per-row buttons), then **Connect both & start**. The board
   reboots; reopen `/calibrate` — it should say **Collecting**, **both connected**. ← *the coex gate.*
   *(If it can't hold both connections — wedges / reboots / heap falls — stop and tell me; the fallback is
   capture-then-fit, the pure fit core is unchanged. Watch `/status` `heap` if unsure.)*

## The ride — a power sweep (this is what makes a good fit)
Ride **easy → hard** so the coverage bands fill; the wizard lights each band as you cover it:
1. Warm up ~3 min easy.
2. **Steady-ish blocks ~60–90 s** across your range — roughly **~120 / 160 / 200 / 240 / 280 W** (your
   numbers). A minute each is plenty; the bands just need samples. **Finish** enables once enough bands
   are covered.
3. A couple of **short hard efforts** (10–20 s) to fill the top, if comfortable.

## Finish → save → confirm
4. Tap **Finish & fit**. Review: the **residual** should be a few watts and the curve monotonic. Name the
   device (default "SB20 Corrector", or e.g. "XCadey-corrected"), **Save** → reboots into corrector mode.
5. **Take the Assioma off** (or just stop pedaling it). Pair the **Garmin** to the corrector name; ride —
   the watts should track what the Assioma *would* read. (Optional: re-mount the Assioma once to spot-check
   agreement at a couple of powers — that's the real proof the correction is right.)
6. If anything's off, `/diag` captures config + frames; recalibrate from `/calibrate` any time.

## ✅ Pass / record
- **Coex:** the board held **both** meters connected through the sweep without wedging (heap stable). ← *primary*
- **Fit:** residual small; the saved corrector rebroadcasts corrected XCadey power the Garmin reads.
- Note the fitted **curve breakpoints + residual** (from the Fitted screen) and whether the Garmin agreed
  with the Assioma on the spot-check. → promote to `decisions.md`.

## Opportunistic add-on — the new phone web UI + workout import (no extra bike time)
While the board + phone are up, smoke-test the redesigned web UI and the workout importer (both
desk-built; this is their first on-device run). None of this needs the track bike specifically — any
board on a trainer works.

1. **The five screens render + are live on the phone:** open `http://sb20proxy.local/` and walk the
   bottom nav — **Ride** (IN→OUT title, big power, sparkline, tap-title detail), **Setup** (`/setup`),
   **More** (`/more`: Mode / Identity / Source / Workout / Calibrate / Send a report / Firmware), and
   from More → **Calibrate** and **Workout**. Power should update live on Ride as you pedal.
2. **Workout screen — built-in preset:** More → **Workout** → tap **4×8 Threshold** (or any preset).
   Confirm the profile chart draws, **Start** runs the clock, the **TARGET** + "now" power update,
   **Pause/Resume/Skip** behave, and the loaded workout **survives a power-cycle** (reboot → still
   loaded). *Erg is NOT driven yet — display/clock only (the FTMS erg write is §14 phase 4).*
3. **Workout import (`.zwo` → device):** from the desk machine, convert + push a real Zwift workout:
   ```bash
   python code/scripts/import_workout.py <some.zwo> --ftp <yourFTP> --post http://sb20proxy.local
   ```
   Confirm the Workout screen shows the imported name + profile (or paste the printed JSON into the
   screen's "Paste a workout" box if the device isn't reachable from that machine). If you have a Garmin
   `.fit` workout, try it too and **eyeball the targets vs the source app** — FIT power decode is
   best-effort until validated against a real file (record any mismatch → `decisions.md`).

**Record:** which screens looked right/wrong on the actual phone, whether the preset ran + persisted,
and whether the imported workout's targets matched the source. Bugs here are desk-fixable (pure render
+ importers are host-tested) — note them for a follow-up PR.

## Fallback (if on-device coex won't hold)
The old desk path still works: capture both meters together and fit on the desk —
`07_capture_multi.py` → `09_fit_calibration.py --target xcadey --ref assioma` (the on-device fit is
parity-tested against it). The pure fit is identical either way; only *where it runs* changes.

## Retro (fill in at the end — see [`PLAYBOOK.md`](PLAYBOOK.md) §4)
- **Went well:**
- **Went wrong / slow / confusing (+ root cause):**
- **Planned vs actual (timestamps):**
- **Changes to make before next session (process / run-sheet / tooling):**
- **Next gate + desk work that must precede it:**
