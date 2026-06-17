# 🚴 Bike session 2 — does the SB20 read the faithful spoof? + capture its handshake

Prepared overnight 2026-06-18. The ESP32-C3 OLED board is flashed with the **byte-faithful** crank
firmware (PR #5 / branch `ble-crank-fidelity`) and now **logs everything the SB20 writes to us** —
the one thing no sniffer can see. Two goals tonight, in order:

1. **Does the SB20 now display power?** (the fidelity payoff — last time it paired but showed 0 W).
2. **Capture the SB20's interactive protocol** via `/log` (control-point/zero-reset, proprietary
   `fe02`, connect/disconnect) — this is Session G Part B, the spec to finish the firmware.

## Before you start
- **Fresh CR2032 in the real LEFT crank** (`62144`) — it read ~12%.
- Power the ESP near the bike. Confirm it's on WiFi + reachable: `curl http://sb20proxy.local/`
  (or the IP). You should see `source:searching`, low uptime.
- Keep the **Assioma awake** (pedal a few strokes) — `curl /` should show `source:connected` and
  `src_power_w` tracking. `/log` will show `[meter] cps flags=... cadence=yes/no ...` — note whether
  cadence is present (answers the open Assioma-cadence question).

## The test
1. **Pull the real LEFT crank battery** (duplicate-advertiser: the real `62144` also advertises
   "Stages 62144"). Leave the **right crank `4963`** in.
2. SB20 → Stages app → **Pair with Bluetooth** → pair to **Stages 62144** (now only the ESP).
3. **Pedal.** Does the SB20 now show the **relayed Assioma watts**? Check the **OLED** (IP, then
   `230W 85rpm` on row 3 — power **and** cadence) and `/ui`. Compare to the Assioma.
4. **Watch `/log` the whole time** — in another terminal:
   `while ($true){ (iwr http://sb20proxy.local/log).Content; sleep 3 }` (or just re-`curl /log`).
   You're looking for:
   - `[srv] connect from ...` (the SB20 connected) / `[srv] disconnect reason=N` (drops → bonding?)
   - `[cp] write <hex>` — the SB20's control-point writes (zero-reset / erg / calibration handshake)
   - `[prop fe02] write <hex>` — any proprietary Stages writes
5. In the app, trigger a **zero-reset** and set an **erg target** (200→250) — watch `/log` for what
   the SB20 writes at each step.

## After
- **Save the `/log` dump** to a file and run it through the parser for a clean spec:
  `python -c "from sb20proxy.logparse import parse_log; print(parse_log(open('log.txt').read()).render())"`
  → meter frame spec + the decoded handshake. Paste the dump into chat and I'll decode + propose the
  firmware fix for any control-point op the SB20 expects.
- 🔁 **Restore:** reinsert the L-crank battery, re-pair the SB20 to **`62144` (L) : `4963` (R)**,
  165 mm, offsets **903/951**, back to normal mode.

## Outcomes (each is a win — it's exploratory)
- **SB20 shows power** → the fidelity fix worked; the product reads + relays on the real bike. 🎉
- **Still 0 W, but `/log` shows the SB20's writes** → that's the spec we were missing; we decode it
  and iterate the firmware (reflash ~5 min; or the PC fast-iterate rig if you rename the PC's
  Bluetooth — see `code/scripts/PC-CRANK.md`).
- **Nothing in `/log` beyond connect** → the SB20 reads passively; the gap is elsewhere (frame/feature)
  and we compare the captured real-crank GATT again.

## Bonus (only if there's time) — shifter-button BLE probe

A 2-minute data-grab for a future idea: does the SB20 emit BLE packets when you press the **shifter
buttons**? Connect to the **SB20 itself** (not the crank) and capture while pressing:

```powershell
code\.venv-win\Scripts\python.exe code\scripts\06_capture_ble.py `
  --address E4:AA:5A:D6:0E:D4 --duration 120 `
  --output code\findings\captures\SHIFTER-probe-$(Get-Date -Format yyyyMMdd-HHmm).jsonl
```
(`E4:AA:5A:D6:0E:D4` = `Stages Bike 0105` from the 2026-06-17 scan; services FTMS `0x1826` + CSC
`0x1816`.) Once it connects + is logging, **press each shifter button a few times** (left up/down,
right up/down), pausing between, and narrate which you pressed. We're looking for a notification or
a characteristic that changes on a press. Send me the JSONL — if presses show up, that's a thread to
pull (see `code/findings/forward-plan.md` §8). No worries if nothing appears; it's exploratory.
