# 🟢 READ ME FIRST — bike-machine session cold-start (session 3)

**You are the assistant on the bike machine.** Your job: **guide the rider live, one step at a time,**
through [`BIKE-SESSION-3.md`](BIKE-SESSION-3.md) — watching `/log` and the capture files as they
narrate ("flashed", "battery out", "pairing now", "pressed LEFT-up"). Don't dump the whole sheet; walk it.

**How to run it well: read [`sessions/PLAYBOOK.md`](sessions/PLAYBOOK.md) first** — the rider's time and
patience are the budget; one step at a time, explicit pass/fail, never send them to do something you
haven't verified is ready, and close with a retro.

**Record as you go.** Write each step's result **back into `BIKE-SESSION-3.md`** (`✅`/`❌`/`⚠️` + the
observed values / `/log` lines) — not just into chat. At the end, set its `Status:` to `✅ DONE (date)`
with a one-line Outcome, update the ledger [`sessions/README.md`](sessions/README.md), and promote durable
findings to `code/findings/decisions.md` + commit captures. (CLAUDE.md → *Session plans & the session ledger*.)

Prepared 2026-06-19 from the desk machine. Everything below was verified just before handoff.

---

## 1 · Get current (do this first — 30 s)

The repo on this machine lives at `C:\repos\cauldnz\SB20-power-proxy` (clone it there if missing).
**`main` is the source of truth.** Sync before anything:

```powershell
cd C:\repos\cauldnz\SB20-power-proxy
git fetch origin
git checkout main && git pull            # current truth (HEAD should be 83bf562 or later)
```

> If `git status` shows a different branch with local edits, you're on a stale checkout — `git fetch`
> and compare to `origin/main` before trusting anything (see CLAUDE.md → *Git & branch hygiene*).

## 2 · Confirm the device is alive (15 s) — it's ALREADY flashed

The ESP32-C3 OLED board was **already flashed from the desk with the current firmware** (the PR #5
control-point fixes **+** the off-loop-OLED perf fix **+** the corrected **BLE cal-offset 0**), verified
stall-free. So **§0 of the run sheet is just a confirm, not a re-flash** (re-flash only if the board
moved to a different WiFi).

```powershell
(Invoke-WebRequest http://sb20proxy.local/ -UseBasicParsing).Content   # or http://192.168.1.165/
#  healthy idle -> "source":"searching", power 0   |   reading the meter -> "source":"connected"
```
If it doesn't answer: the board may be on a different network — re-provision via the `SB20-Setup`
captive portal (`firmware/BENCH-FLASH.md`), or USB-flash (`firmware\flash.ps1 -Mode usb`).

## 3 · Run the session

Open [`BIKE-SESSION-3.md`](BIKE-SESSION-3.md) and walk the rider through it:
- **A · Firmware-fix verification** — does the SB20's zero-reset now **complete** (we ACK `0x10`), does
  crank-length set/read (`0x04`/`0x05`), does it **reconnect without a reboot**, constant `fe02` handshake.
- **B · Comprehensive shifter probe** — map each button → gear bit, the full range, the silent channels
  (toward emulating a **Zwift Click**). Capture with `06_capture_ble.py --subscribe-all`.

---

## Device + key facts (everything you need at a glance)

| | |
|---|---|
| **Board** | `sb20proxy.local` → `192.168.1.165` (Donnie Boon WiFi) · `/` status · `/ui` dash · `/log` writes · `/stats` perf |
| **Spoofs as** | `Stages 62144` (CPS crank) · **reads** the meter named `ASSIOMA` |
| **BLE cal-offset answered** | **`0`** (the captured BLE value `200c010000`; **NOT** the ANT+ `903` — see CLAUDE.md / decisions.md) |
| **The SB20 itself** | `Stages Bike 0105`, addr **`E4:AA:5A:D6:0E:D4`** · shifter gear char `0c46be60` |
| **Flash (only if needed)** | `cd firmware ; .\flash.ps1` (OTA, retries) — board's already current |

### ⚠️ Restore values — have the rider WRITE THESE DOWN before changing any pairing
**Stages `62144` (L) : `4963` (R)** · crank length **165 mm** · **ANT+** zero-offset **L 903 / R 951**
(these are the real cranks' app/ANT+ values — distinct from our spoof's BLE offset 0). Restore before finishing.

**Live support:** the rider narrates in chat; you `curl` the ESP32 (`/`, `/log`, `/stats`) and read the
JSONL capture files off the machine as they go. Each rung is a win — if something stalls, grab `/log`
and we iterate.

> Past/superseded sessions are indexed in [`sessions/README.md`](sessions/README.md) (the ledger) —
> `BIKE-SESSION-3.md` is the only live run sheet for today.
