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
git checkout main && git pull            # current truth (origin/main ~ f129e65 or later)
```

> **Since this doc was first written, two things landed (overnight 2026-06-21):**
> **(1)** The **whole FTMS stack is now built spec-ahead** (codec + erg client + shifter-erg + firmware
> seams; bench-proven over real BLE — see `code/findings/ftms-protocol.md`). So **§C is now the
> *validation* gate** — it confirms the SB20 actually ergs off our Set-Target-Power and supplies the real
> frames to pin the spec-built codec. (The §C run itself is unchanged: `capture_ftms.py --erg`.)
> **(2)** A new **§D calibration grid** (optional, lowest priority, needs the **ANT+ stick**) was added to
> the session-04 doc.

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
- **§D — Stages↔Assioma calibration grid** (OPTIONAL, ~25 min, lowest priority) — only if legs + time +
  the **ANT+ stick** is plugged in. Drives the `calgrid` workout + a paired ANT+ capture → the fit.

**§C and §B connect to the SB20 directly (Stages app disconnected) and need NO ESP** — so they can run
first, before any flashing. **Only G2 needs the ESP**, flashed with the latest firmware (below).

## 3 · Flash the board — needed for G2 (its last flash predates the A1 fix)

The desk last flashed the board **before** the A1 Enhanced-Offset (`0x10`) fix landed, so **re-flash from
current `main` before G2** (not needed for §C/§B):

```powershell
cd firmware ; .\flash.ps1            # build + OTA esp32c3-oled-live -> sb20proxy.local (RSSI pre-flight + retries)
```
Confirm alive: `curl http://sb20proxy.local/` → `source:searching`, low uptime. Open a rolling `/log`:
`while($true){(iwr http://sb20proxy.local/log).Content;sleep 3}`.

> **OTA flaky (weak signal, <−72 dBm)? Use USB — but via `flash_c3.py`, NOT `flash.ps1 -Mode usb`.**
> We found (2026-06-21) that PlatformIO's bundled esptool 4.5.1 has the ESP32-C3 USB-JTAG "No serial data
> received" bug, so `pio`-based USB upload **fails** on these boards. The reliable hang-free path is
> esptool 4.11 direct, wrapped by **`flash_c3.py`** (build with pio first):
> ```powershell
> cd firmware ; python -m platformio run -e esp32c3-oled-live
> python ..\code\scripts\flash_c3.py --env esp32c3-oled-live --port COM<N> --verify-ble "Stages 62144"
> ```
> (`COM<N>` = the board's port — check Device Manager / `pio device list`. See memory `esp32-c3-flashing`.)

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
