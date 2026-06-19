# 🚴 Bike session 4 — ground the Enhanced-Offset (0x10) format + FTMS erg + brake-lever probe

**Status: 🟡 PLANNED** · tracked in [`sessions/README.md`](sessions/README.md). Run via
[`sessions/PLAYBOOK.md`](sessions/PLAYBOOK.md) — one step at a time, record actuals inline, retro at the end.
**⏱ Timestamp from the start** (note `HH:MM` at the session start and each section — per the playbook, so
planned-vs-actual is recorded, not reconstructed).

Prepared 2026-06-19 (desk) after session 3. **~45–55 min** (G1 ~10 · G2 ~10 · C ~10 · B ~10 · restore ~5).
Three gates + a probe, **front-loaded** (the high-information captures first, while the rig is fresh):

- **G1 — capture the REAL crank's `0x10` Enhanced-Offset reply** (grounds the A1 desk fix; the one byte
  sequence the spec can't give us). ⭐ do this first.
- **G2 — re-test our spoof's `0x10`** after flashing the A1 desk fix (does the Stages app's calibrate
  now COMPLETE instead of spinning?).
- **C — FTMS erg-acceptance** ⭐ — does the SB20 erg off a **third-party** Set-Target-Power? The go/no-go
  for the *shifter-buttons-adjust-erg-watts* feature (`code/findings/shifter-erg-control.md`).
- **B — brake-lever / silent-channel shifter probe** (do the brake levers fire on `be61`/`beb0` or FTMS
  Status `0x2ADA`? — the session-3 open thread).

> ⚠️ **Realistic-time note (session-3 lesson):** "verify/retest" steps (G2) can become *investigation* if
> they fail — budget for it, don't assume quick. The time budget above already pads them.

> **Why G1 matters:** session 3 proved our old `0x10` reply (`20 10 01 00 00`, the 5-byte `0x0C` shape)
> leaves the app spinning. The desk fix sends the **spec-correct Enhanced shape**
> `20 10 01 <offset s16> <mfgCompanyId u16> <mfgData…>` (see `Cps.h encodeEnhancedOffsetCompResponse`),
> but the **exact company-id + any trailing manufacturer data are unknown** and were never passively
> sniffable. G1 actively elicits them from the real crank by *writing* `0x10` to it and logging its reply.

## Bring (pre-flight — per the playbook checklist)
A **fresh CR2032** + coin/screwdriver (the real L crank `62144` has read **12–14%** before — and **G1
needs it awake**, so check/replace it first; reinsert for the restore). Phone with the **Stages Cycling**
app. This chat open.

## Restore values — WRITE DOWN before changing any pairing
**Stages `62144` (L) : `4963` (R)** · crank length **165 mm** · ANT+ zero-offset **L 903 / R 951**.
Spoof reads `ASSIOMA`, advertises `Stages 62144`. SB20 itself = `E4:AA:5A:D6:0E:D4`.

---

## 0 · Pre-flight (desk-verified; bike just flashes)
The board needs the **A1 desk-fix firmware** (this branch / PR — the spec-correct `0x10` Enhanced reply).
From `firmware/`:
```powershell
cd firmware ; .\flash.ps1            # build + OTA esp32c3-oled-live -> sb20proxy.local (retries)
```
Confirm: `curl http://sb20proxy.local/` → `source:searching`, low uptime. Open a rolling `/log`:
```powershell
while ($true) { (iwr http://sb20proxy.local/log -UseBasicParsing).Content; sleep 3 }
```

## G1 · Capture the REAL crank's 0x10 Enhanced-Offset reply ⭐ (do FIRST)
This writes `0x10` to the **real** Stages L crank and logs its indication — the bytes that ground
`SPOOF_MFG_COMPANY_ID` (+ any manufacturer data). Native-Windows PowerShell:

1. **Power the ESP spoof OFF** (unplug it) so the only `Stages 62144` advertising is the **real** crank,
   and **leave the L-crank battery IN** (we need the real crank awake). Keep the cranks **still** (it's a
   zero-reset). *(Fallback if it grabs the wrong device: target the real crank by `--address` — its addr
   is in `G-crank62144-ble-zero-20260615-070353.jsonl`.)*
2. Run:
   ```powershell
   C:\repos\cauldnz\SB20-power-proxy\code\.venv-win\Scripts\python.exe `
     C:\repos\cauldnz\SB20-power-proxy\code\scripts\06_capture_ble.py `
     --name 'Stages 62144' --duration 120 `
     --control-point enhanced-offset-compensation `
     --output "C:\repos\cauldnz\SB20-power-proxy\code\findings\captures\G-crank62144-ble-enhanced-0x10-$(Get-Date -Format yyyyMMdd-HHmm).jsonl"
   ```
   The script connects, subscribes to the control-point indication, writes `0x10`, and logs the reply raw.

**✅ Pass:** the JSONL has a `ble_cp_indication` whose `raw_hex` starts `2010 01…` — that's the real
Enhanced reply. **Commit the JSONL.** Tell me the bytes; I ground `SPOOF_MFG_COMPANY_ID` (+ mfgData) in
them, update the golden test, and reflash. *(If the write is rejected/needs bonding, note it — the
spec-structure fix from G2 may still satisfy the app on its own.)*

## G2 · Re-test our spoof's 0x10 (the A1 payoff)
1. Reconnect/power the ESP; **pull the real L-crank battery** so the SB20 pairs to the ESP (`Stages 62144`).
   SB20 → Stages app → Pair with Bluetooth → pedal a few strokes (wake the chain), confirm power+cadence.
2. Stages app → **calibrate / zero-reset.** Watch `/log` for `[cp] write 10` then our reply.

**✅ Pass:** the calibrate UI **COMPLETES** (no longer spins), link holds, `/log` shows our `20 10 01 …`
Enhanced reply. **❌ Still spins:** the app wants the real captured bytes from G1 (or bonding) — paste
`/log` + which we tried; desk-iterate the Config values and reflash (~5 min).

## B · Brake-lever / silent-channel shifter probe (~10 min)
Session 3 mapped all 6 shifter buttons (one-hot on `0c46be60`) but the **brake-lever buttons** and the
**silent channels `0c46be61` / `0c46beb0`** are untested. Connect to the **SB20 itself**, Stages app
**disconnected**:
```powershell
C:\repos\cauldnz\SB20-power-proxy\code\.venv-win\Scripts\python.exe `
  C:\repos\cauldnz\SB20-power-proxy\code\scripts\06_capture_ble.py `
  --address E4:AA:5A:D6:0E:D4 --subscribe-all --duration 180 `
  --output "C:\repos\cauldnz\SB20-power-proxy\code\findings\captures\SHIFTER-probe-4-$(Get-Date -Format yyyyMMdd-HHmm).jsonl"
```
Narrate each action: **squeeze LEFT brake ×3, RIGHT brake ×3** (pause between), then any other buttons.
Watch whether `0c46be61`, `0c46beb0`, or **FTMS Status `0x2ADA`** ever fires.

**Also — map the hidden buttons 4 & 5 (the key button-budget test).** There are **5 buttons per side**
(4 & 5 are under the bar tape); session 3 only mapped 1/2/3 (bits `0x01/0x02/0x04` L, `0x08/0x10/0x20` R).
The app *config* ties **1≡4** and **2≡5** (can't set them separately), but **if they emit different BLE
bits we can still separate all five.** So press, narrating each precisely: **LEFT button 1, then LEFT
button 4** — same bit (`0x01`) or a new one? Then **LEFT 2, then LEFT 5**. Repeat on the RIGHT. Result
decides the budget: distinct bits → up to **10** usable signals (set a Profile slot to "external" to free
the pair); same bit → 1≡4 are indistinguishable to us and we're back to the 6 we have.

**Then — input-gesture characterization** (grounds the **control-button** gestures, `shifter-erg-control.md`
— the two **3rd** buttons are reserved for Zwift/ESP/menu control and need gestures; erg uses the main
up/down buttons in erg mode). With the same capture running, **narrate *exactly* what you physically do
for each — a single hold and N taps look the same in the frames, so the narration is the ground truth**
(session 3 had an unrecorded "10 separate clicks" misread later as one hold — don't repeat that):
1. **Chord:** press **LEFT-3rd + RIGHT-3rd at the same time** ×3 — does `0c46be60` show one frame with
   **both bits** (`0x0024`) or two separate events? (decides if a "both buttons" chord is usable).
2. **Double-tap:** **RIGHT-3rd quick double-tap** ×3, narrate "double-tap" — is the gap between the two
   `03/04/08` bursts clean enough to detect vs a single press?
3. **Hold vs taps — the multi-shift question** (UNRESOLVED — session 3 couldn't distinguish them). Do both,
   narrating which: (a) **LEFT-3rd HELD ~2 s** ×2 — say "holding now … released"; (b) **LEFT-3rd TAPPED 5×
   fast** — say "five separate taps". Compare: does a single *hold* emit **repeated `03` commits** (the
   bike auto-repeating, at what rate?) plus a long `01` stream, vs the taps' **one `03` each**? This is the
   "hold-to-ramp" question for the erg feature — get it from a clean, narrated capture, not inference.

**✅ Pass:** brake capture lands + the three gestures are recorded. If a brake squeeze fires a char → new
thread; if nothing → brakes aren't on BLE (consistent with the aero-remote hypothesis). Either way, send
the JSONL — the gesture frames decide the erg button-input design.

---

## C · FTMS erg-acceptance — does the SB20 erg off a THIRD-PARTY Set Target Power? ⭐ (~10 min)

**The go/no-go for the *shifter-buttons-adjust-erg-watts* feature** (owner ask;
`code/findings/shifter-erg-control.md`). The SB20 is a full FTMS machine — the Stages app drives erg by
writing **Set Target Power** to Control Point `0x2AD9`. **Unknown:** will the SB20 accept that op from
*us* (not the app) and actually hold the target? FTMS machines can refuse a secondary controller
(`control-not-permitted`). One capture settles it; nothing gets built until it passes.

**Setup:** connect to the **SB20 itself** (`E4:AA:5A:D6:0E:D4`), **Stages app DISCONNECTED** from the SB20
(FTMS expects one controller). Put the bike in **erg/target-power mode** if there's a manual way; **pedal
steadily throughout** so the logged power can be seen to track (or not). The capture tool does the erg
handshake itself — Request-Control → Start → Set-Target-Power — and logs the SB20's `0x80` responses.

```powershell
C:\repos\cauldnz\SB20-power-proxy\code\.venv-win\Scripts\python.exe `
  C:\repos\cauldnz\SB20-power-proxy\code\scripts\capture_ftms.py `
  --name SB20 --duration 240 --erg --erg-targets 150,200,100 --erg-hold 25 `
  --output "C:\repos\cauldnz\SB20-power-proxy\code\findings\captures\G-sb20-ftms-erg-$(Get-Date -Format yyyyMMdd-HHmm).jsonl"
```

**✅ Pass:** responses come back `80 00 01` (Request-Control success) and `80 05 01` (Set-Target-Power
success), **and the Indoor Bike Data power tracks the 150/200/100 targets** as you pedal → **the
shifter-erg feature is real**; build it grounded in the captured FTMS frames. **❌ Fail:**
`…05` = `control-not-permitted`, or power ignores the targets → the SB20 won't erg off a third party over
BLE (or needs to be the *sole* controller / bonding); tell me the exact response bytes — that decides the
feature and the alternative-app path. **Commit the JSONL** either way (passive Indoor Bike Data + the
Feature/Power-Range reads are useful regardless). *(Fallback if `--name SB20` doesn't match: use
`--address E4:AA:5A:D6:0E:D4`.)*

**Bonus — shift-in-erg behaviour** (decides the erg button allocation; the feature plans to repurpose the
**main up/down buttons** for erg ± *in erg mode*). While pedalling at a held erg target, **press a main
shift button (LEFT-up) a few times** and watch the SB20/Stages app + the Indoor Bike Data power: does the
gear change **do anything** in erg (resistance/power blip, an on-screen gear number), or is it **inert**
(erg overrides)? **Inert → we can repurpose the shift buttons for erg with no app Profile change; does
something → we'd disable shifting via a Profile in erg.** Note what you see.

---

## 🔁 Restore (before you leave)
Reinsert **both** crank batteries → re-pair the SB20 to **`62144` (L) : `4963` (R)**, **165 mm**, ANT+
offsets **903 / 951**, normal mode → pedal once to confirm the real cranks read.

## Retro (fill in at the end — see [`sessions/PLAYBOOK.md`](sessions/PLAYBOOK.md) §4)
- **Went well:**
- **Went wrong / slow / confusing (+ root cause):**
- **Planned vs actual (timestamps):**
- **Changes to make before next session (process / run-sheet / tooling):**
- **Next gate + desk work that must precede it:**
