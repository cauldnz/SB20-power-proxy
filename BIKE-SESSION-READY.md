# 🟢 READ ME FIRST — bike-machine session cold-start

**You are the assistant on the bike machine.** Two rides are **🟢 READY** for this visit — run **whichever
bike is set up first**:

- **[Session 8 — SB20 spoof calibration handshake (G1 + G2)](sessions/session-08-sb20-spoof-calibration.md)** —
  on the **SB20**. Capture the real Stages crank's `0x10` Enhanced-Offset reply (G1) → test whether the
  Stages app's zero-reset now **completes** against our spoof (G2). ~25–35 min.
  **🟡 STAGED 2026-06-24 → read the doc's "RESUME HERE" first:** pivoted to the **own-unique-ID** approach —
  the board is already on **`Stages 62145`** (runtime, no reflash); the plan is app **L=`62145` / R=`4964`
  (phantom), real cranks stay powered, no battery pull**. First gate: does the SB20 connect to our ESP at
  `62145`? Original G1/G2 fold in as bonus/payoff. (Restore POST + values are in the session doc.)
- **[Session 5 — meter-to-meter calibration ride (XCadey → reads like Assioma)](sessions/session-05-meter-calibration-capture.md)** —
  on the **track bike** (both meters fitted). Drive the on-device `/calibrate` wizard so the XCadey reads
  like the Assioma, then leave the proxy rebroadcasting the corrected XCadey for the Garmin. ~20–30 min.

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

Prepared for sessions 8 + 5 (desk-derisked 2026-06-23). Boards run the **2026-06-23 firmware** — fine for
session 8 (the spoof's `0x10` reply is unchanged since); the security-lockdown reflash to current `main`
is **deliberately post-session-8** (owner's call).

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

> **Only reflash if a session step says to** — session 8 G2 reflashes **only** if G1 yields new
> `SPOOF_MFG_COMPANY_ID` bytes to plug in. If you must: build then USB-flash via `flash_c3.py` (NOT
> `flash.ps1 -Mode usb` — pio's bundled esptool 4.5.1 hits the C3 USB-JTAG "No serial data received" bug;
> memory `esp32-c3-flashing`):
> ```powershell
> cd firmware ; python -m platformio run -e esp32c3-oled-live
> python ..\code\scripts\flash_c3.py --env esp32c3-oled-live --port COM9 --verify-ble "Stages 62144"
> ```
> OTA (`firmware\flash.ps1`) also works if RSSI is decent (>−72 dBm). **COM9 = the OLED bike board; COM10 =
> the spare/no-OLED board** (memory `esp32-c3-flashing`).

---

## Device + key facts (at a glance)

| | |
|---|---|
| **Proxy board** | `sb20proxy.local` → `192.168.1.165` (mDNS is the safe bet) · `/` `/ui` `/log` `/stats` `/status` `/calibrate` · **COM9** = OLED bike board |
| **SB20 spoof (session 8)** | **🟡 staged on `Stages 62145`** (2026-06-24 own-unique-ID pivot; was `62144` — restore POST in the session doc) · **reads** `ASSIOMA` · BLE cal-offset **0** (`200c010000`; **not** ANT+ `903`) · Enhanced `0x10` reply spec-shape `20 10 01 <offset> <mfgId>` · plan: app **L=`62145` / R=`4964` phantom** |
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
