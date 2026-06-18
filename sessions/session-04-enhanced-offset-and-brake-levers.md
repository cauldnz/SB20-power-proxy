# 🚴 Bike session 4 — ground the Enhanced-Offset (0x10) format + brake-lever shifter probe

**Status: 🟡 PLANNED** · tracked in [`sessions/README.md`](sessions/README.md). Run via
[`sessions/PLAYBOOK.md`](sessions/PLAYBOOK.md) — one step at a time, record actuals inline, retro at the end.

Prepared 2026-06-19 (desk) after session 3. Two gates + a probe, **front-loaded** (the high-information
captures first, while the rig is fresh):

- **G1 — capture the REAL crank's `0x10` Enhanced-Offset reply** (grounds the A1 desk fix; the one byte
  sequence the spec can't give us). ⭐ do this first.
- **G2 — re-test our spoof's `0x10`** after flashing the A1 desk fix (does the Stages app's calibrate
  now COMPLETE instead of spinning?).
- **B — brake-lever / silent-channel shifter probe** (do the brake levers fire on `be61`/`beb0` or FTMS
  Status `0x2ADA`? — the session-3 open thread).

> **Why G1 matters:** session 3 proved our old `0x10` reply (`20 10 01 00 00`, the 5-byte `0x0C` shape)
> leaves the app spinning. The desk fix sends the **spec-correct Enhanced shape**
> `20 10 01 <offset s16> <mfgCompanyId u16> <mfgData…>` (see `Cps.h encodeEnhancedOffsetCompResponse`),
> but the **exact company-id + any trailing manufacturer data are unknown** and were never passively
> sniffable. G1 actively elicits them from the real crank by *writing* `0x10` to it and logging its reply.

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

**✅ Pass:** capture lands. If a brake squeeze fires a char → new thread; if nothing → brakes aren't on
BLE (consistent with the aero-remote hypothesis for the silent channels). Either way, send the JSONL.

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
