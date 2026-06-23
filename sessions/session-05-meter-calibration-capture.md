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
