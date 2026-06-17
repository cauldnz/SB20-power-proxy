# 🚴 Bike session 3 — verify the firmware fixes + comprehensively probe the shifters

Prepared 2026-06-18 after session 2 (which **proved power-acceptance** — Assioma → ESP → SB20, power +
cadence, confirmed crank-free). Two goals:

- **A. Verify the control-point / reconnect firmware fixes** (PR #5) — the things that broke in session 2.
- **B. Comprehensively map the shifter-over-BLE protocol** (owner's explicit ask).

**Flash first:** OTA the latest `ble-crank-fidelity` build (build with WSL pio, flash with Windows curl):
`curl -F firmware=@firmware/.pio/build/esp32c3-oled-live-ota/firmware.bin http://192.168.1.165/update`

## Before you start
- Fresh CR2032 in the real LEFT crank (`62144`) if you'll restore to it afterwards.
- ESP powered near the bike, on WiFi: `curl http://sb20proxy.local/`.
- Keep the Assioma awake. With **meter-pinning** the ESP should now lock the Assioma only — `/log`
  should show it connect to `ASSIOMA…` and **not** bounce to a `Stages …` crank.

## A. Firmware-fix verification (the session-2 failures — now fixed)
Pull the L-crank battery so the ESP is the only `Stages 62144`; pair the SB20 to it; pedal → confirm
power + cadence (regression check). Then, watching `/log` (`while($true){(iwr http://sb20proxy.local/log).Content;sleep 3}`):

1. **Zero-reset COMPLETES.** Stages app → calibrate / zero-reset. Expect it to **succeed** now (we ACK
   `0x10` Enhanced Offset Compensation with a synthetic success) instead of spinning. `/log` shows
   `[cp] write 10` then our response, and **no disconnect**.
2. **Crank length sets + reads back.** Set crank length (e.g. 170 mm) → expect it to **stick** and the
   app to read it back (we handle `0x04` set + `0x05` request). `/log` shows `[cp] write 04 …`.
3. **Reconnect without a reboot.** Force a disconnect (toggle the app's pairing, or walk the ESP out of
   range and back) → the ESP should **re-advertise and the SB20 reconnect on its own**, no ESP
   power-cycle. `/log` shows `[srv] disconnect …` then a fresh `[srv] connect …` with no reboot between.
4. **Constant handshake holds.** Confirm `[prop fe02] write bfda1853` still appears on each connect.

If any step still fails, save `/log` and paste it — we iterate the responder (reflash ~5 min).

## B. Comprehensive shifter probe (owner's ask)
Connect to the **SB20 itself** + subscribe to everything, then press **methodically**. Do this with the
**Stages app disconnected** from the SB20 to avoid contention:

```powershell
C:\repos\cauldnz\SB20-power-proxy\code\.venv-win\Scripts\python.exe `
  C:\repos\cauldnz\SB20-power-proxy\code\scripts\06_capture_ble.py `
  --address E4:AA:5A:D6:0E:D4 --subscribe-all --duration 300 `
  --output "C:\repos\cauldnz\SB20-power-proxy\code\findings\captures\SHIFTER-probe-3-$(Get-Date -Format yyyyMMdd-HHmm).jsonl"
```
Then, **narrating each press** so we can correlate timestamps:
1. **One button at a time, isolated.** LEFT-up ×3 (pause), LEFT-down ×3 (pause), RIGHT-up ×3 (pause),
   RIGHT-down ×3 — maps each button → which way the gear bit walks.
2. **Walk the full range.** Shift all the way one way, then all the way back — find the **total gear
   count** (bits past `0x20`? does the field widen past one byte?) and whether it **clamps or wraps**.
3. **The 3rd button per side.** Press it alone — what does it do? (No haptic in session 2.)
4. **Watch the silent channels** `0c46be61` and `0c46beb0` — does anything ever wake them?
5. **(Optional, cautious)** later we may WRITE to `0c46beb1` to *inject* a shift — read-only mapping
   first; only try writing if there's time and we've mapped the read side.

Send me the JSONL — I'll finish `findings/shifter-ble-protocol.md`.

## 🔁 Restore
Reinsert **both** crank batteries → re-pair the SB20 to **`62144` (L) : `4963` (R)**, **165 mm**,
offsets **903 / 951**, back to normal mode; pedal once to confirm the real cranks read.
