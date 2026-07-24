# Session 12 — laptop handoff (paste this into a fresh session on the bike laptop)

**Purpose:** everything a brand-new Claude Code session on the **bike laptop** needs to run session 12.
The plan itself is [`session-12-erg-workout-validation.md`](session-12-erg-workout-validation.md); this
doc is the *cold-start + the paste-able opening prompt*.

---

## A. Paste this as your first message on the laptop

> You are running an on-bike session for the SB20-power-proxy project. Read, in order:
> `CLAUDE.md`, `sessions/PLAYBOOK.md`, then the session plan
> `sessions/session-12-erg-workout-validation.md` (and its restore-list + pairing recipe). Then run
> **G0 (board alive + current)** at the desk before I get on the bike.
>
> Ground truth you must respect:
> - **I (the rider) do the hardware and the pedals. You run every tool.** Never assume I'm at the bike —
>   I will tell you explicitly "AT THE BIKE".
> - Sync git first (`git fetch origin`); **PR #281 (the CYD boot-loop fix) must be merged before you
>   flash a CYD as the head-unit** — but the ride board is the **C3-OLED**, which the bug never touched,
>   so it is not a blocker for the ride itself.
> - Use `code\.venv\Scripts\python.exe` for every Python tool (system Python lacks `sb20proxy`).
> - Confirm the toolchain with `tools\doctor.ps1` before anything; a Python upgrade has orphaned
>   PlatformIO here before (`tools\provision-dev-env.ps1` repairs it).
> - Standing rule: start the **dual-radio capture** (nRF sniffer + ANT+, whole session) and prove both
>   are *actually* capturing a few seconds of live air **before** I'm at the bike — not just doctor-green.
>
> Walk me through the gates **one at a time**, record each result **back into the session doc**
> (`✅`/`❌`/`⚠️` + observed `/log` lines and bytes), timestamp from the start, and close with the retro.
> Start now with G0 and tell me what you need me to do first.

## B. What's already been de-risked at the desk (2026-07-25) — don't redo it

- **All suites green** on `main`: Python 471, ESP32 host 245, nRF host 40; `esp32c3-wifi`,
  `xiao-sense`, `feather-nrf52840` all link; CI green.
- **nRF sniffer proven live** on the desk (`sniffer on COM13 @ 1000000 baud`, 24 advertisers). The
  capture path is `code/scripts/sniff_ble.py` (Nordic SnifferAPI over serial — **not** Npcap).
- **Two tool bugs found + fixed before you touch them:** `sniff_ble.py --help` (UnicodeEncodeError on a
  Windows console, `52a4aca`); `import_workout.py` needs the venv (`ModuleNotFoundError` on system
  Python).
- **CYD (COM17) boot-loop found + fixed** (Watty Birds frame as a 150 KB global ctor OOM on the
  no-PSRAM CYD) — **PR #281**. Merge it before using a CYD as a display. The C3 ride path is unaffected.
- **The qz OBC-listener contribution is green on the fork's Linux-desktop CI** — see
  [`code/findings/qz-upstream-contribution.md`](../code/findings/qz-upstream-contribution.md). The
  upstream PR is **not** open; opening it needs the owner's explicit go (stretch S3 only).

## C. What could NOT be pre-staged here (do it at the bike, front-loaded)

- **The bike board (`192.168.1.165`) was off/away** — could not be pre-flashed. **G0** recovers it:
  `GET /status` → if stale, OTA `esp32c3-oled-live-ota` (RSSI better than ≈ −72 dBm, pass explicit
  `-I <lan-ip>`); USB fallback `flash_c3.py --env esp32c3-oled-live-ota --port COM9`. A fresh boot needs
  **~25 s** before HTTP rebinds — don't call it dead early.
- **No ANT+ stick on the desk machine** — if you bring one, its first capture is *investigation*, not
  verification; don't let it block the ride.

## D. The three must-do gates (full detail in the plan)

1. **G0 — board alive + current** (desk, before the bike): `/status` = `Stages 62145`, mode spoof,
   `/stats` free-heap > 100 KB, clean `reset_reason`.
2. **G1 — pair + corrected power**: pair **L=`62145` (our spoof)** + **R=`4963` (your real right crank,
   left crank powered)** in the Stages app — **no battery pull**; SB20 head-unit shows tracking watts.
3. **G2 — erg go/no-go** (front-loaded, ~5 min): `capture_ftms.py --erg` at `E4:AA:5A:D6:0E:D4`; does
   resistance step *and* the control point answer? PASS → **G3** runs erg-driven; a clean no-answer is a
   valid result → ride **G3** manual. Either way the **40-min `4×8 Threshold`** soaks the whole stack.

## E. Restore-list — write down before touching any pairing

> Real pair **`Stages 62144` (L) : `4963` (R)** · crank length **165 mm** · ANT+ zero-offset **L 903 /
> R 951**. (The spoof's **BLE** zero-offset is **0** — a different representation of the same
> calibration; don't cross-fix them.)

---

*After the ride: fill in the plan's §9 Actual + §10 Retro, flip its Status to ✅ DONE, update
[`sessions/README.md`](README.md), promote findings to
[`decisions.md`](../code/findings/decisions.md), commit captures, and fold retro lessons into
[`PLAYBOOK.md`](PLAYBOOK.md).*
