# 🟢 READ ME FIRST — bike-machine session cold-start (session 4)

**You are the assistant on the bike machine.** Your job: **guide the rider live, one step at a time,**
through [`sessions/session-04-enhanced-offset-and-brake-levers.md`](sessions/session-04-enhanced-offset-and-brake-levers.md)
— watching `/log` and the capture files as they narrate. Don't dump the whole sheet; walk it.

**How to run it well: read [`sessions/PLAYBOOK.md`](sessions/PLAYBOOK.md) first** — the rider's time and
patience are the budget; one step at a time, explicit pass/fail, never send them to do something you
haven't verified is ready, **timestamp from the start**, record every mid-session instruction change, and
close with a retro.

**Record as you go.** Write each step's result **back into the session doc** (`✅`/`❌`/`⚠️` + observed
bytes / values / `/log` lines), not just chat. At the end set its `Status:` to `✅ DONE (date)` with a
one-line Outcome, update the ledger [`sessions/README.md`](sessions/README.md), promote durable findings to
`code/findings/decisions.md`, commit captures — **and fold the retro's lessons into the playbook**
(CLAUDE.md → *Session plans & the session ledger*).

Prepared 2026-06-19/20 from the desk machine.

---

## 1 · Get current (do this first — 30 s)

Repo on this machine: `C:\repos\cauldnz\SB20-power-proxy` (clone there if missing). **`main` is the source
of truth.** Sync before anything:

```powershell
cd C:\repos\cauldnz\SB20-power-proxy
git fetch origin
git checkout main && git pull            # current truth (HEAD should be 8155420 or later)
```

> If `git status` shows a different branch with local edits, you're on a stale checkout — `git fetch`
> and compare to `origin/main` first (CLAUDE.md → *Git & branch hygiene*).

## 2 · Session 4 — what it is, and the priority order

Three gates + a probe (full plan + commands in the session-04 doc). **Front-load by value:**

- **§C — FTMS erg-acceptance** ⭐ — does the SB20 erg off a **third-party** Set-Target-Power (run the bike
  in **External** mode)? The **go/no-go for the next feature** (shifter→erg). **Do this first.**
- **§B — shifter probe** — brake-lever buttons, the silent channels, the hidden **buttons 4/5** (are
  `1≡4`/`2≡5` separable over BLE?), and the chord/double-tap/hold gestures.
- **G1 / G2 — the A1 `0x10` fix** — capture the **real crank's** Enhanced-Offset reply (G1), then **retest
  our spoof's** `0x10` (G2). Protocol-completeness, **secondary**.

**§C and §B connect to the SB20 directly (Stages app disconnected) and need NO ESP** — so they can run
first, before any flashing. **Only G2 needs the ESP**, flashed with the latest firmware (below).

## 3 · Flash the board — needed for G2 (its last flash predates the A1 fix)

The desk last flashed the board **before** the A1 Enhanced-Offset (`0x10`) fix landed, so **re-flash from
current `main` before G2** (not needed for §C/§B):

```powershell
cd firmware ; .\flash.ps1            # build + OTA esp32c3-oled-live -> sb20proxy.local (RSSI pre-flight + retries)
```
Confirm alive: `curl http://sb20proxy.local/` → `source:searching`, low uptime. OTA flaky? It's the
signal (<−72 dBm) — move the board nearer the AP, or `.\flash.ps1 -Mode usb`. Open a rolling `/log`:
`while($true){(iwr http://sb20proxy.local/log).Content;sleep 3}`.

---

## Device + key facts (at a glance)

| | |
|---|---|
| **Board** | `sb20proxy.local` → `192.168.1.165` (Donnie Boon WiFi; mDNS is the safe bet) · `/` `/ui` `/log` `/stats` |
| **Spoofs as** | `Stages 62144` (CPS crank) · **reads** the meter named `ASSIOMA` |
| **BLE cal-offset** | `0` (captured `200c010000`; **not** the ANT+ `903`). A1: our `0x10` Enhanced reply now sends the spec shape `20 10 01 <offset> <mfgId>` — **G1 captures the real crank's `0x10`** to ground the mfg-id |
| **The SB20 itself** | `Stages Bike 0105`, addr **`E4:AA:5A:D6:0E:D4`** · shifter char `0c46be60` · FTMS `0x1826` |

### ⚠️ Restore values — have the rider WRITE THESE DOWN before changing any pairing
**Stages `62144` (L) : `4963` (R)** · crank length **165 mm** · **ANT+** zero-offset **L 903 / R 951**
(real cranks' app/ANT+ values — distinct from our spoof's BLE offset 0). Restore before finishing. Fresh
**CR2032** for the real L crank (it's read 12–14%; G1 needs it awake).

**Live support:** the rider narrates in chat; you `curl` the ESP32 (`/`, `/log`, `/stats`) and read the
JSONL captures off the machine as they go.

> All sessions are indexed in [`sessions/README.md`](sessions/README.md). Session 4 is the SB20 run sheet;
> session 5 (track-bike meter calibration) is a separate, independent ride. Older `BIKE-SESSION-*.md` are
> historical.
