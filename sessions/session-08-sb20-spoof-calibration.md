# 🚴 Session 8 — SB20 spoof calibration handshake (G1 + G2)

**Status: 🟢 READY** · tracked in [`sessions/README.md`](README.md). Run via [`PLAYBOOK.md`](PLAYBOOK.md)
(record actuals inline, **⏱ timestamp from the start**, retro at the end). Carries forward the **G1/G2**
items deferred from [session 4](session-04-enhanced-offset-and-brake-levers.md) (the board's WiFi/OTA
was down that day — it isn't now).

**Goal:** close the last gap in the SB20 spoof — the **Stages app's zero-reset / calibrate**. Our
spoof answers the Cycling Power control point, but the app's *Enhanced* Offset Compensation (`0x10`)
reply carries a manufacturer company-id we've never captured (`Config::SPOOF_MFG_COMPANY_ID` is a
flagged `0x0000` placeholder). **G1** captures the *real* crank's `0x10` reply to ground it; **G2**
tests whether our spoof's calibrate now **completes** in the app (it spun before — `decisions.md`
session 3/4). This is independent of the corrector ride (session 5).

**~25–35 min** (G1 ~10 · G2 ~10 · iterate buffer ~10). G1 **first** — it grounds the fix G2 verifies.

## What's already done (desk — verify, don't redo)
- **The proxy board is flashed with current firmware** (COM9 / `sb20proxy.local` / `192.168.1.165`),
  which already emits the **spec-correct Enhanced `0x10`** reply (`encodeEnhancedOffsetCompResponse`,
  `Config::SPOOF_MFG_COMPANY_ID` — placeholder until G1). So **G2 can run immediately**; a reflash is
  only needed if G1 yields new bytes to plug in.
- Board health: `python code/scripts/route_smoke.py --ip sb20proxy.local --no-post` → all routes PASS.

## Bring / set up
- The **SB20** + the **real Stages L crank `62144`** + a **fresh CR2032** (the real crank has read
  12–14% low on an old cell; G1 needs it awake, and a flat cell can corrupt the offset).
- A **phone with the Stages app**. The **proxy board** powered near the bike (on WiFi).
- Coin/small screwdriver for the crank battery door.

## Pre-flight (desk, ~2 min — the gate)
```bash
curl http://sb20proxy.local/status      # 200 + low "ms" (fresh boot), source:searching
```
Open a rolling `/log` (the live instrument) in a second terminal:
```bash
while true; do curl -s http://sb20proxy.local/log; sleep 3; done
```

## G1 · Capture the REAL crank's 0x10 Enhanced-Offset reply ⭐ (do FIRST)
Writes `0x10` to the **real** crank and logs its indication — the bytes that ground
`SPOOF_MFG_COMPANY_ID` (+ any manufacturer data).
1. **Power the ESP spoof OFF** (unplug) so the only `Stages 62144` on the air is the **real** crank;
   **leave the real L-crank battery IN** (fresh cell). Keep the cranks **still** (it's a zero-reset).
2. Run (native-Windows venv):
   ```bash
   code/.venv/Scripts/python.exe code/scripts/06_capture_ble.py \
     --name "Stages 62144" --duration 120 \
     --control-point enhanced-offset-compensation \
     --output code/findings/captures/G-crank62144-ble-enhanced-0x10-$(date +%Y%m%d-%H%M).jsonl
   ```
   *(If it grabs the wrong device, target the real crank by `--address <addr>` — its address is in
   `findings/captures/G-crank62144-ble-zero-20260615-070353.jsonl`.)*

**✅ Pass:** the JSONL has a `ble_cp_indication` whose `raw_hex` starts `2010 01…` — the real Enhanced
reply. **Commit the JSONL**, tell me the bytes; I ground `SPOOF_MFG_COMPANY_ID` (+ mfgData) in them,
update the golden test, and we reflash for G2. **If the write is rejected / needs bonding:** note it —
the spec-structure reply we already ship may satisfy the app on its own (test in G2 regardless).

## G2 · Re-test our spoof's 0x10 — does the app's calibrate complete? (the payoff)
1. Re-power the ESP; **pull the real L-crank battery** so the SB20 pairs to the **ESP** (`Stages 62144`).
   SB20 → Stages app → Pair (Bluetooth) → pedal a few strokes (wake the chain) → confirm power + cadence.
2. Stages app → **calibrate / zero-reset.** Watch `/log` for `[cp] write 10` then our reply.

**✅ Pass:** the calibrate UI **COMPLETES** (no longer spins), the link holds, `/log` shows our
`20 10 01 …` Enhanced reply. → promote to `decisions.md` (the A1 zero-reset is GROUNDED). **❌ Still
spins:** paste `/log` + what we tried; if G1 captured real bytes, I plug them into `Config` + reflash
(`flash_c3.py --env esp32c3-oled-live --port COM9`, ~3 min) and we retry — that's the iterate loop.

## ✅ Pass / record
- G1: real `0x10` bytes captured + committed (or "write rejected / needs bonding" noted).
- G2: the Stages app calibrate **completes** against our spoof (or the exact failure + `/log`).
- → `decisions.md`: the grounded `SPOOF_MFG_COMPANY_ID` and whether the app accepts our zero-reset.

## Notes / scope
- **§D (ANT+ power-topology grid)** from session 4 is **out of scope** here unless the ANT+ stick is up
  and there's time — keep this ride tight on G1/G2.
- Realistic-time (session-3 lesson): G2 is a *verify* step that can become *investigation* — the
  iterate buffer + the desk reflash loop are budgeted for exactly that.

## Retro (fill in at the end — see [`PLAYBOOK.md`](PLAYBOOK.md) §4)
- **Went well:**
- **Went wrong / slow / confusing (+ root cause):**
- **Planned vs actual (timestamps):**
- **Changes to make before next session (process / run-sheet / tooling):**
- **Next gate + desk work that must precede it:**
