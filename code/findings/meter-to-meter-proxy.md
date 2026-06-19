# Meter-to-meter proxy — read XCadey, rebroadcast it like the Assioma

**Status:** plan + Phase 1 built (capture). The rest is **real-data-first** — gated on a paired ride.

## Use case (owner, 2026-06-19)

The **Assioma pedals** are the reference power meter. The **track bike** (velodrome) has an **XCadey
Spider** crank-spider meter and the owner wants to ride it **without** the Assioma pedals (worry: pulling
out at high power on the track and breaking the pedals). So: a small **proxy** (in a pocket / on the bike)
that **reads the XCadey and rebroadcasts it corrected to read like the Assioma**, so all data stays on one
consistent scale. The XCadey has a simple `offset`, but — as we found with Stages↔Assioma — a
**fitted model** is better. The model is built from **paired rides** (XCadey *and* Assioma recorded
together during normal training), then runs on the proxy.

This is the **same read→correct→rebroadcast architecture** as the SB20 proxy, applied to a different pair.

## What already exists (reuse — see the calibration pipeline)

The Stages↔Assioma work left a near-complete, **mostly generic** pipeline:

| Piece | File | Reuse for XCadey |
|---|---|---|
| **Correction model** | `code/src/sb20proxy/calibration.py` (`ScaleOffsetTransform`, `GridTransform`, `CalibrationProfile` JSON) | ✅ generic — `target`/`ref` are labels |
| **Fit** | `code/scripts/09_fit_calibration.py` (`--target/--ref/--mode`) | ✅ `--target xcadey --ref assioma` |
| **Analyse** | `code/scripts/08_analyze_grid.py` | ✅ generic; decides power-only vs needs-more |
| **Validate** | `code/scripts/12_compare_fit.py`, `fitcompare.py` | ✅ generic |
| **Paired capture** | `code/scripts/07_capture_multi.py` | ✅ **generalised in Phase 1** (`--meter LABEL:ANTID`) |
| **Apply (Python)** | `transform.py`, `core.py` `ProxyCore` | ✅ generic |
| **Apply (firmware)** | `firmware/lib/proxy/Correction.h` | ⚠️ linear path wired; **grid-from-config not wired yet** (Phase 4) |

**The model is power-only today** (`corrected = power·factor(power)`, piecewise-linear, or `·scale+offset`).
The Stages↔Assioma error was *cadence*-dependent (~13% high @60 rpm vs ~5% @100 rpm, `decisions.md`
2026-06-14) — but XCadey is a different meter; **let the paired data say** whether power-only suffices or a
2-D power×cadence grid is worth adding (the analyse step shows the residual structure).

## Workflow (capture-before-code)

1. **Capture paired rides** — both meters over ANT+ on one stick (sample-aligned):
   ```bash
   python code/scripts/07_capture_multi.py \
     --meter xcadey:<XCADEY_ANT_ID> --meter assioma:<ASSIOMA_ANT_ID> \
     --duration 1800 \
     --output code/findings/captures/CAL-xcadey-vs-assioma-<date>.jsonl
   ```
   A mix of **structured** (steady efforts across the power/cadence range — best for fitting) and
   **unstructured** track riding. Commit the JSONL (canonical, lossless). *(XCadey + Assioma both broadcast
   ANT+; the correction is power→power so it's protocol-agnostic — fitting on ANT+ applies to a BLE-read
   runtime.)*
2. **Fit** → `09_fit_calibration.py --target xcadey --ref assioma --mode auto` → a `CalibrationProfile`
   JSON. **Analyse** (`08_analyze_grid.py`) + **validate** (`12_compare_fit.py`) on a held-out slice.
3. **Deploy to the proxy** — config: read the **XCadey** (CPS), apply the fitted model, rebroadcast as a
   CPS power meter. Two runtime options (decision below).

## Runtime — an ESP32 firmware variant (decided 2026-06-19)

The ESP32 already does **read CPS → apply `Correction` → rebroadcast CPS**; it's pocket-sized and BLE, so
the runtime is a **firmware build variant**. Decided with the owner:

- **ESP32, BLE-only.** It reads the XCadey over **BLE** (no ANT+ on the device). The correction is
  power→power, so a model fitted from ANT+ captures applies unchanged. Owner to sort a **battery** for the
  pocket unit (a LiPo + charge board for the C3 — the Maker skill can help source it).
- **Broadcast under our OWN identity — not a spoof.** Unlike the SB20 case (where we *must* impersonate
  "Stages 62144", because the SB20 only accepts its own crank), a head unit / training app accepts *any*
  CPS power meter — so the proxy advertises as **its own device**: an honest *corrected rebroadcast*, not
  a pretend-to-be-X. **Product name TBD** 🙂. This makes "advertised identity" a **config axis**:
  SB20-mode spoofs a Stages crank; meter-corrector-mode advertises our own product.
- **Fitted-curve gap.** The firmware only wires the *linear* `scale/offset` from `Config` today; loading a
  fitted **grid** needs a small addition (curve points in `Config`/NVS — `Correction.h` already supports
  the curve, it's just not populated).
- **Device discovery + pairing in the UI (forward requirement, owner).** Ultimately the source meter
  (XCadey here) should be **found and paired from the ESP32 web UI** — a BLE scan → pick-the-meter →
  persist-to-NVS flow — rather than a hardcoded `METER_NAME_FILTER`. It serves both this and the SB20 use
  case; backlogged in `forward-plan.md`.

*(Alt runtime: a Python proxy on a phone/Pi reuses `04_run_proxy.py` directly — heavier to carry, zero
firmware work. Not chosen.)*

## Decisions (resolved 2026-06-19)

- **Runtime:** ✅ ESP32 firmware variant, BLE-only.
- **Identity:** ✅ our own device identity (not a spoof); product name TBD.
- **Model:** ✅ data-driven — power-only first, add cadence only if the analyse step shows residual
  cadence structure.
- **Still needed from the owner:** the **XCadey & Assioma ANT+ device ids** (to run the P2 capture) +
  confirm the XCadey broadcasts ANT+.

## Phases

- **P1 — capture tooling (DONE):** `07_capture_multi.py` generalised to `--meter LABEL:ANTID` (any two
  Bike Power meters), back-compatible with the legacy flags; `parse_meter_spec` host-tested.
- **P2 — paired ride (owner):** capture XCadey + Assioma together; commit the JSONL. ← *nothing downstream
  starts until this exists.*
- **P3 — fit + validate (gated on P2):** run the fit/analyse/compare on the real data; pick the model.
- **P4 — deploy (gated on P3):** wire the fitted model into the proxy (firmware grid-load + read/spoof
  config, or the Python proxy); bench-test against a replayed capture, then ride.
