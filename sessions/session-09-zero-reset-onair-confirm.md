# 🚴 Session 9 — Zero-reset → Assioma: on-air confirm

**Status: 🟢 READY (planned 2026-06-25)** · tracked in [`sessions/README.md`](README.md). Run via
[`PLAYBOOK.md`](PLAYBOOK.md) (record actuals inline, ⏱ timestamp, retro at the end).

**Goal:** confirm the zero-reset→Assioma feature (PR #138) works **on air** — when the Stages app's
**calibrate / zero-reset** runs against our spoof, the firmware should forward a real **Start Offset
Compensation (`0x0C`)** to the Assioma so the meter genuinely zeroes (not just a cosmetic UI completion).
This is the **one remaining gate** after the canonical reflash (`decisions.md` 2026-06-25 pm). ~10 min;
opportunistic — can ride along with [session 5](session-05-meter-calibration-capture.md).

## What's already done (desk — don't redo)
- Board is on the **shippable firmware** (lockdown + 442 fix + zero-reset), identity **`Stages 62145`**,
  push-OTA authenticated (`ota_secret.h`). Reflash + OTA path already validated (`decisions.md` 2026-06-25 pm).
- The zero-reset forward is **host-tested + builds**; only the live SB20→ESP→Assioma behaviour is unproven.

## Bring / set up
- The **SB20** + the **real Assioma** (the source meter — it must be **awake**: pedal a few strokes).
- A **phone with the Stages app**. Proxy board powered near the bike (on WiFi).
- (No real Stages cranks needed for the feature itself, but R=`4963` must be **findable** for the SB20 to
  pair — see session 8: a phantom right id fails pairing.)

## Pre-flight (desk — the gate)
```powershell
.\tools\doctor.ps1 -BoardIp 192.168.1.165     # toolchain + board reachable + OTA host-IP candidates
curl http://sb20proxy.local/status            # 200, source:searching
```
Open a rolling `/log` (it's readable on this build). **NB the locked-down firmware:** `POST /setup/save`
has a same-origin (CSRF) check — do identity changes from the board's web UI, or a `curl` with matching
`Origin`/`Referer` headers; OTA needs the `-a <password>` from `ota_secret.h`.

## The gate — does the app's calibrate zero the Assioma?
1. **Pair the SB20 to our ESP:** Stages app → crank ids **L=`62145`** (the ESP) + **R=`4963`** (real
   right, must be on air). Watch `/log` for `[srv] connect …` + `[prop fe02] write bfda1853`.
2. **Pedal** a few strokes to wake the Assioma; confirm power+cadence flow (`/status` `src_power_w` tracks).
3. **Stop, hold the cranks still, run the app's calibrate / zero-reset.** Watch `/log` for the sequence:
   - `[cp] write 10` — the app's zero-reset lands on our CP
   - `[cp] offset-comp -> forwarding zero to source meter`
   - `[meter] zero-reset -> source CP 0x0C: sent`
   - `[meter] zero-reset source result 200c01…` — **the Assioma's reply** (this is the proof it zeroed)

**✅ Pass:** `/log` shows the forwarded `0x0C` **and** the Assioma's `20 0c 01 …` result; the app calibrate
still completes. → promote to `decisions.md` (the zero-reset is now functional, not cosmetic). **❌ Fail:**
paste `/log` — if the source has no writable CP, or the result never comes, note it; the fire-and-forget
write may need a retry or the Assioma may have been loaded/moving during the zero.

## Restore
- App → L=`62144` / R=`4963` for normal riding. ESP may stay on `62145` (safe), or restore to `62144`
  (`POST /setup/save name=ASSIOMA&single=1&spoof_name=Stages 62144&spoof_serial=11821518` — needs the
  same-origin header on the locked-down build) once the experiment is fully done.

## Opportunistic bonus (if time — the rig's already set up)
- **Debug the phantom-R pairing failure** (`forward-plan.md` §12): while at the bike, run a couple of
  crank-id variants in the app and watch `/log` — **L=`62145` / R=`4964`** (absent → reproduce the fail;
  does the ESP see *any* connect attempt?), then **L=`<absent id>` / R=`4963`** (does the bike connect to a
  present right crank alone?). Nails whether the SB20 needs *both* ids findable (→ a sole-source workaround
  = the ESP advertising a 2nd phantom-right peripheral) or the app is the gatekeeper. Restore L=`62145` /
  R=`4963` afterwards for the zero-reset gate above.

## Retro
- Went well:
- Went wrong / slow / confusing (+ root cause):
- Planned vs actual:
- Changes to make before next session:
- Next gate + desk work that must precede it:
