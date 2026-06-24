# 🟢 READ ME FIRST — bike-machine session cold-start

**You are the assistant on the bike machine.** One ride is **🟢 READY** for this visit:

- **[Session 5 — meter-to-meter calibration ride (XCadey → reads like Assioma)](sessions/session-05-meter-calibration-capture.md)** —
  on the **track bike** (both meters fitted). Drive the on-device `/calibrate` wizard so the XCadey reads
  like the Assioma, then leave the proxy rebroadcasting the corrected XCadey for the Garmin. ~20–30 min.

> **Session 8 (SB20 spoof calibrate handshake) is ✅ DONE (2026-06-25).** G1 captured the real crank's `0x10`
> (Manufacturer **Company ID 442** + mfg-data that encodes the L/R offsets), G2 confirmed the Stages app's
> calibrate now **completes** against our spoof (`decisions.md` 2026-06-25). **Two desk follow-ups before the
> SB20 is "shippable":** (a) **reflash the board to current `main` (security lockdown) + the 442 fix** — it's
> currently on a temporary *pre-lockdown+fix* build — then re-confirm the calibrate on the locked-down
> firmware and restore the `62144` identity; (b) the 442 fix rides in on branch `session/08-onbike-20260625`
> (merge the PR). Until then, don't re-run session 8.

They're **independent** — do one, both, or whichever bike is ready. **Guide the rider live, one step at a
time,** out of the chosen session doc; watch `/log` + the capture files as they narrate. Don't dump the
whole sheet; walk it.

**How to run it well: read [`sessions/PLAYBOOK.md`](sessions/PLAYBOOK.md) first** — the rider's time and
patience are the budget; one step at a time, explicit pass/fail, never send them to do something you
haven't verified is ready, **⏱ timestamp from the start**, record every mid-session instruction change, and
close with a retro.

**Record as you go.** Write each step's result **back into the session doc** (`✅`/`❌`/`⚠️` + observed
bytes / values / `/log` lines), not just chat. At the end set its `Status:` to `✅ DONE (date)` with a
one-line Outcome, update the ledger [`sessions/README.md`](sessions/README.md), promote durable findings to
`code/findings/decisions.md`, commit captures — **and fold the retro's lessons into the playbook**
(CLAUDE.md → *Session plans & the session ledger*).

Prepared for **session 5** (desk-derisked 2026-06-23). ⚠️ **Build toolchain (learned the hard way, session
8):** this bike laptop's PlatformIO was orphaned by a Python 3.14 upgrade — it was re-provisioned into a venv
at **`C:\repos\cauldnz\pio-venv`** (PlatformIO 6.1.19). **`python -m platformio` no longer works** (system
Py 3.14 has no platformio); use the venv's `platformio.exe`. Verify it runs **and that an OTA build actually
flashes** *before* the rider is at the bike (§2b). The SB20 board currently runs a temporary
*pre-lockdown+fix* build pending the canonical desk reflash (see the session-8 note above).

---

## 1 · Get current (do this first — 30 s)

Repo on this machine: `C:\repos\cauldnz\SB20-power-proxy` (clone there if missing). **`main` is the source
of truth.** Sync before anything:

```powershell
cd C:\repos\cauldnz\SB20-power-proxy
git fetch origin
git checkout main && git pull            # current truth
```

> If `git status` shows a different branch with local edits, you're on a stale checkout — `git fetch`
> and compare to `origin/main` first (CLAUDE.md → *Git & branch hygiene*; concurrent sessions share this repo).

## 2 · Pre-flight the board (the gate — don't pedal until green)

Both boards were flashed on 2026-06-23, and **no flashing is needed for session 8** (the spoof's `0x10`
reply hasn't changed; the security-lockdown reflash to current `main` is post-session-8). Confirm the
board the rider has powered is alive:

```powershell
curl http://sb20proxy.local/status                       # 200 + low "ms" (fresh boot), source:searching
python code\scripts\route_smoke.py --ip sb20proxy.local --no-post   # all routes PASS (session-8 health gate)
```
Open a rolling `/log` (the live instrument) in a second terminal and keep it visible the whole ride:
```powershell
while($true){(iwr http://sb20proxy.local/log).Content; sleep 3}
```

> **Power-cycle the board before the gate** if it's been running a while — heap drops over long uptime
> (a board up ~20 h showed ~28 KB free, which times out the *unused-by-session-8* `/calibrate` route in
> `route_smoke`; a power-cycle restores ~125 KB). For session 8, only `/status` · `/setup` · `/diag` · `/log`
> matter — `/calibrate` is session 5's.

## 2b · BLE tooling (session 8 G1 needs it — set up OFF the rider's clock)

Session 8's G1 capture (`06_capture_ble.py`) needs a Python venv with **bleak**. If this is a fresh clone
without `code\.venv`, create it before the rider is at the bike (verified working: **bleak 3.0.2**, native
Windows, Python 3.13):
```powershell
python -m venv code\.venv
code\.venv\Scripts\python.exe -m pip install bleak
code\.venv\Scripts\python.exe -c "import asyncio; from bleak import BleakScanner; asyncio.run(BleakScanner.discover(timeout=5))"  # radio works?
```
The scan should see **`Stages 62145`** (our ESP) *and* **`Stages 62144`** (the real crank), disambiguated by
address — that's the G1/G2 pair. (`06_capture_ble.py` is API-verified against bleak 3.0.2.)

> **Only reflash if a session step says to.** ⚠️ **`python -m platformio` is dead on this machine** (Py 3.14
> has no platformio) — use the venv: **`C:\repos\cauldnz\pio-venv\Scripts\platformio.exe`**.
> Build: `…\pio-venv\Scripts\platformio.exe run -e esp32c3-oled-live -d firmware`. Then either:
> - **OTA (preferred, RSSI > −72 dBm):** on this **multi-NIC laptop espota auto-picks the wrong host IP**
>   (`0.0.0.0` → "No response from device") — call espota directly with the **explicit host LAN IP**:
>   ```powershell
>   C:\repos\cauldnz\pio-venv\Scripts\python.exe "$env:USERPROFILE\.platformio\packages\framework-arduinoespressif32\tools\espota.py" -i <board-ip> -I <host-lan-ip> -p 3232 -f firmware\.pio\build\esp32c3-oled-live\firmware.bin -r
>   ```
>   (session 8: board `192.168.1.165`, host `192.168.1.223`; find host IP with `Get-NetIPAddress`). `flash.ps1` works too once `pio` is on PATH.
> - **USB:** `flash_c3.py --env esp32c3-oled-live --port COM9 --verify-ble "Stages 62144"` (NOT
>   `flash.ps1 -Mode usb` — esptool 4.5.1 hits the C3 USB-JTAG "No serial data received" bug). **COM9 = the
>   OLED bike board; COM10 = the spare/no-OLED board** (memory `esp32-c3-flashing`).

---

## Device + key facts (at a glance)

| | |
|---|---|
| **Proxy board** | `sb20proxy.local` → `192.168.1.165` (mDNS is the safe bet) · `/` `/ui` `/log` `/stats` `/status` `/calibrate` · **COM9** = OLED bike board |
| **SB20 spoof (session 8 ✅ DONE)** | calibrate handshake **CLOSED** — real Enhanced `0x10` reply = `20 10 01 00 00 ba 01 04 85 03 b7 03` (**Company ID 442** + mfg-data encoding L901/R951; BLE offset still **0**). App restored to L=`62144`/R=`4963`. Board on a temp *pre-lockdown+fix* build on `Stages 62145`; **desk: reflash `main`+442 fix, restore `62144`.** Own-id spoof needs a **findable** right crank (phantom R fails) → no double-count. |
| **Corrector (session 5)** | on-device `/calibrate`: **XCadey = DUT**, **Assioma = Ref** → fit → rebroadcast corrected XCadey under its own name for the **Garmin**. Run path bench-proven; the bike proves **2-meter coex** + the real fit |
| **The SB20 itself** | `Stages Bike 0105`, addr **`E4:AA:5A:D6:0E:D4`** · shifter char `0c46be60` · FTMS `0x1826` |

### ⚠️ Restore values — have the rider WRITE THESE DOWN before changing any pairing (session 8)
**Stages `62144` (L) : `4963` (R)** · crank length **165 mm** · **ANT+** zero-offset **L 903 / R 951**
(the real cranks' app/ANT+ values — distinct from our spoof's BLE offset 0). Restore before finishing. Fresh
**CR2032** for the real L crank (it's read 12–14% low on an old cell; G1 needs it awake).

**Live support:** the rider narrates in chat; you `curl` the board (`/`, `/log`, `/stats`, `/status`) and
read the JSONL captures off the machine as they go.

> All sessions are indexed in [`sessions/README.md`](sessions/README.md). Sessions 8 + 5 are independent
> rides; older `BIKE-SESSION-*.md` / session 4 are historical (DONE).
