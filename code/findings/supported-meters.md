# Supported power meters — what works, what we screen for

**Status: the canonical meter-compatibility reference (2026-06-23).** The "which meter can I use, and
will it work" answer for [`beta-program.md`](beta-program.md) screening and tester support. Grounded in
the real CPS codec ([`code/src/sb20proxy/ble/cps.py`](../src/sb20proxy/ble/cps.py) ·
[`firmware/lib/proxy/Cps.h`](../../firmware/lib/proxy/Cps.h)) and the committed captures in
[`captures/`](captures/) — **not** vendor marketing. Real-data-first: a meter is only **Verified** once
we hold its real frames.

> **Product scope: power-meter PEDALS.** This product is *power-meter pedals becoming the crank
> replacement* — the SB20 reads your pedals instead of its (inaccurate / dead / dying) Stages crank. So
> the supported source is a **power-meter pedal set**; testers must have one. Crank-arm and spider
> meters are **out of scope** for this product (a separate, experimental *meter-to-meter corrector* mode
> handles a non-pedal source — see [`meter-to-meter-proxy.md`](meter-to-meter-proxy.md) — but that is
> not the SB20 pedal-replacement product or the pre-beta).
>
> **The hard technical requirement: the pedals must broadcast over *Bluetooth LE* as a standard Cycling
> Power Service (`0x1818`) device.** We read BLE, not ANT+. Most power pedals dual-broadcast (BLE + ANT+)
> — fine, we use the BLE side and your watch/head-unit keeps the ANT+ side. **ANT+-only** pedals cannot
> be used. Pedals + Bluetooth are the two screening musts.

## What the proxy actually requires

The proxy is a BLE *central* that subscribes to the source meter's Cycling Power Measurement
(`0x2A63`) and re-broadcasts corrected power as the spoofed Stages crank. For a meter to work it needs:

1. **BLE Cycling Power Service `0x1818`** with a notifying **Measurement `0x2A63`** characteristic
   (the SIG-standard surface — what Zwift/Garmin read). ✅ Nearly all BLE power meters expose this.
2. **A flag layout our codec decodes.** We fully handle Measurement flag bits **0–5**: pedal balance
   (bit 0) · balance-reference (bit 1) · accumulated torque (bit 2) · torque-source (bit 3) · wheel-rev
   (bit 4) · crank-rev (bit 5). That covers every pedal/crank meter we've seen. Bits **6–12** (extreme
   force/torque/angles, dead-spot angles, accumulated energy) are **not decoded yet** — no meter we've
   captured sets them, and if one does, its `/diag` frames tell us exactly what to add (see
   [Adding a new meter](#adding-a-new-meter-the-tester-loop)).
3. **Reports total/summed power, or is a known single-sided meter.** A left-only pedal/crank that
   reports just its own leg needs the **single-sided ×2** toggle (config UI) so we double it to a
   whole-bike estimate. A dual or already-summed meter must **not** be doubled. See
   [Single-sided meters](#single-sided-meters--the-2-question).

## Compatibility tiers

### ✅ Verified — we hold real frames + golden-vector tests

| Meter | What we have | Flags | Notes |
|---|---|---|---|
| **Favero Assioma** (Uno / Duo) | recon [`captures/G-assioma17039-ble-20260615-065730.jsonl`](captures/G-assioma17039-ble-20260615-065730.jsonl) + zero-reset; balance frame [`captures/ASSIOMA-ble-cps-20260622.jsonl`](captures/ASSIOMA-ble-cps-20260622.jsonl); golden vectors in [`test_ble_cps.py`](../tests/test_ble_cps.py) | `0x0023` (balance + balance-ref-left + crank-rev) | The project's reference meter. Single-pedal **Uno** is left-only → needs **×2**; **Duo** reports total → no doubling. Zero-reset (offset compensation) captured and handled. |
| **Stages SPM2 L crank** (e.g. `Stages 62144`) | [`captures/G-crank62144-ble-20260615-065556.jsonl`](captures/G-crank62144-ble-20260615-065556.jsonl) + zero-reset; the spoof identity itself | `0x002F` (balance + ref-left + torque + torque-source + crank-rev) | This is also the crank we *impersonate*. Left-only → ×2 if used as a *source*. BLE zero-reset offset `0` (not the ANT+ `903` — see [[old-branch-superseded]]). |

These two are decode-pinned by committed golden vectors; their flag layouts are exercised on every CI run.

### 🟢 Expected to work — power pedals, standard BLE CPS, pending a tester capture to promote to Verified

Mainstream **power-meter pedals** expose the exact `0x1818`/`0x2A63` surface above. We have **no committed
frames yet**, so we list them as *expected* and want one `/diag` capture each to promote them (and lock
golden vectors). Recruiting a spread of these pedal brands is a primary beta goal.

| Pedals | Sidedness | Watch-outs |
|---|---|---|
| **Favero Assioma Duo-Shi** (pedal-axle) | dual / total | Same firmware family as Assioma — high confidence. |
| **Garmin Rally** (RS / RK / XC) | single (left-only SKUs) → ×2; dual → no | Garmin's CPS is standard; single-sided SKUs common — confirm sidedness when screening. |
| **Wahoo Powrlink Zero** | single or dual | Standard CPS. Single → ×2. |
| **SRM EXAKT** | dual / total | Standard CPS; total. |
| **Magene PES P505** | dual / total | Standard CPS; confirm on first capture. |

### ❌ Out of scope for this product — crank-arm & spider meters

The SB20 product is pedals → crank replacement, so a **crank-arm** meter (4iiii, Stages, Shimano) or a
**spider/chainring** meter (Quarq/SRAM, Power2Max) is **not** what we recruit or support here — even
though the firmware *could* read its BLE CPS. (Reading a non-pedal meter and rebroadcasting it corrected
is the separate, experimental **meter-to-meter corrector** mode — [`meter-to-meter-proxy.md`](meter-to-meter-proxy.md)
— not the SB20 pedal-replacement product.) Decline these at screening, kindly, with the reason.

> "Expected" ≠ tested. We don't claim a meter works until we hold its frames — that's the whole point
> of the tester loop. Screen for these, ship, capture, promote.

### ⚠️ Conditional / needs work

| Case | Status |
|---|---|
| Meter that sets **Measurement bits 6–12** (extreme force/torque/angles, dead-spot, energy) | Decode-extends on demand. Frames would currently fail to parse past the crank fields; a `/diag` capture gives us the exact bytes to add the field(s). No meter seen so far does this. |
| **Trainer / smart bike as a power source** (FTMS Indoor Bike Data, not CPS) | Different protocol (`0x1826`/`0x2AD2`). Tracked separately in [`ftms-protocol.md`](ftms-protocol.md) / the FTMS plan — gated on a real capture. Not a CPS meter. |

### ❌ Not supported

| Case | Why |
|---|---|
| **ANT+-only meters** (no BLE) | We read BLE. No BLE CPS = nothing to subscribe to. The most common rejection at screening. |
| **Proprietary-BLE meters that don't expose CPS `0x1818`** | Rare, but some older/odd meters hide power behind a vendor service with no standard CPS. If `0x1818` isn't advertised, we can't read it without reverse-engineering that service. |
| Meters reporting power **only** via a manufacturer's app/cloud | No local BLE broadcast to read. |

## Single-sided meters — the ×2 question

A **left-only** pedal or crank-arm meter measures one leg and (by convention) reports that leg's power;
a head unit doubles it for a whole-bike estimate assuming ~50/50 balance. Our proxy does the same when
the **single-sided ×2** toggle is on (config UI → source section). Get this wrong and power reads half
or double:

- **Left-only** (Assioma Uno, 4iiii left-arm, single-sided Stages/Rally/Powrlink) → **×2 ON**.
- **Dual / spider / already-summed** (Assioma Duo, Quarq, Power2Max, dual Rally/Powrlink) → **×2 OFF**.

The dashboard's **Balance L/R** stat is the live check: a true dual meter shows a real split (e.g.
48/52); a doubled single-sided meter shows a flat 50/50 (we synthesize it). When in doubt, screen for it
and set it from the meter model.

## Screening checklist (the meter half of recruitment)

Use alongside [`beta-program.md`](beta-program.md) §Screening. For each candidate tester:

1. **Brand + exact model?** → look it up in the tiers above.
2. **Does it broadcast Bluetooth LE** (not ANT+ only)? → the hard gate. If unsure: it pairs to a phone
   app over Bluetooth, or Zwift sees it as a Bluetooth "Power Source" → yes.
3. **Single-sided or dual/total?** → sets the ×2 toggle we pre-configure before shipping.
4. **Verified or Expected tier?** → Expected-tier testers are the high-value ones (each promotes a meter
   + grows the golden-vector library). Aim for a spread of brands.
5. **Crank-rescue path?** → if the goal is rescuing a dead Stages crank, the *source* is whatever working
   meter they own (often an Assioma/Rally) and the above all applies; the spoof side is unchanged.

## Adding a new meter (the tester loop)

When a tester's meter isn't recognised or power looks wrong, this is **not** a dead end — it's how the
library grows:

1. Tester opens the board's **`/diag`** page and sends us the text (config + status + raw CPS frames).
2. We run `python code/scripts/parse_diag.py <their-diag.txt> --fixture` — it decodes the frames with the
   same codec, says whether we already handle the meter, and emits a golden-vector stub.
3. If it decodes cleanly → add the frames as golden vectors (promote the meter to **Verified**) and we're
   done. If a frame fails → the failure names the unhandled flag/field; extend
   `decode_cps_measurement` / `Cps.h` to cover it, add the golden vector, **OTA** the fix.

Every meter that lands this way moves a row from 🟢 Expected to ✅ Verified and ships to everyone via OTA.
That round-trip — real frames in, support out — is the core of the beta.
