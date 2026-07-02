# 🚴 Session 10 — spin-bike tryout of the new phone UI (the C3-OLED bike computer)

**Status: 🟢 READY** · tracked in [`sessions/README.md`](README.md). Run via [`PLAYBOOK.md`](PLAYBOOK.md)
(record actuals inline, ⏱ from the start, retro at the end).

**Goal:** ride the **C3-OLED bike board** on the spin bike and drive the whole redesigned **phone web
UI** (Ride · Setup · Calibrate · Workout · Settings) end-to-end — the shippable ride path. The
**S3-Touch head-unit is NOT part of this session** (its on-hardware bring-up is blocked — separate
handoff below).

**~15–20 min.** Everything here is desk-verified; the bike only proves the pairing + live ride.

## Which hardware
- **C3-OLED board** (the shipping beta board), flashed to **current `main`** (`esp32c3-oled-live` —
  reads a real meter + WiFi + OLED + the new web UI). It keeps its NVS (WiFi creds + spoof identity),
  so it rejoins WiFi and advertises its crank on power-up. *(On the dev machine it was COM9; the OLED
  shows the IP once joined.)*
- Your **power meter** (Assioma) on the spin bike, **phone**, and the **SB20** (or the spin bike's head).

> ℹ️ **The S3-Touch head-unit is a parallel track — now bench-working, but still not part of THIS ride.**
> As of 2026-07-03 the board **boots + renders the full UI + touch works** on the **pioarduino (IDF 5.5)**
> platform (`esp32s3-pio` env; all 5 screens captured live off the panel; BLE spoof up). It is **not yet
> ridden on the bike** — the C3-OLED + phone remains the proven ride path for this session. S3 write-up:
> [`advanced-board-s3-touch.md`](../code/findings/advanced-board-s3-touch.md) §"Bring-up RESOLVED".

## Pre-flight (off the bike — the gate; don't pedal until green)
1. Power the C3-OLED board. The OLED shows **Connecting → its IP** (it rejoins your saved WiFi).
   *If it shows the setup portal instead, join `SB20-Setup` on your phone and pick your network.*
2. On your phone open **`http://sb20proxy.local/`** (or the IP the OLED shows). The **Ride** screen
   loads (dark, IN→OUT title, big POWER, sparkline, Cadence | Balance, bottom Ride/Setup/More nav).
3. **Setup** tab → confirm your **Assioma** appears in the scan; tap it → **Use** (it may reboot to
   apply). Back on **Ride**, pedal — **power updates live** and the IN→OUT title shows your meter → the
   spoofed crank identity.

## The ride
4. **Pair the SB20** to the spoofed crank exactly as before (Stages app → the crank id the board
   advertises; battery-out on the real crank if needed — session 8/9 rules). Confirm the SB20 shows
   your (corrected) watts and, if you use it, erg reacts.
5. **Walk the UI while pedalling** (tap through, narrate what looks right/wrong):
   - **Ride** — tap the title → the IN/OUT detail cards (name · W · rpm · RSSI/uptime) drop down.
   - **More** (`/more`) — Mode / Identity / Source / Firmware read correctly; rows link out.
   - **Workout** — tap a preset (e.g. **4×8 Threshold**) → the profile chart draws → **Start** runs
     the clock, **TARGET** + live "now" power update, **Pause/Resume/Skip/Stop** behave, and the
     loaded workout **survives a power-cycle**. *(Erg is not driven yet — display/clock only; §14
     phase 4.)*
   - **Calibrate** (`/calibrate`) — opens; a full corrector calibration is the separate track-bike
     session 5.
6. **Workout import (optional, from a laptop on the same WiFi):**
   `python code/scripts/import_workout.py <some.zwo> --ftp <yourFTP> --post http://sb20proxy.local`
   → confirm the imported workout shows on the **Workout** screen. (Or paste the printed JSON into the
   screen's "Paste a workout" box.)

## ✅ Pass / record
- **Ride:** live power tracks on the phone Ride screen; SB20 pairs + shows corrected watts. ← *primary*
- **UI:** all five screens render + behave on the actual phone; preset workout runs + persists.
- **Import:** a real `.zwo` loads onto the device and shows correctly.
- Note anything that looked wrong on the real phone (font sizes, truncation, taps) — those are
  desk-fixable (the render/parse is host-tested) → a follow-up PR.

## 🧪 Stretch — try the S3-Touch head-unit on the bike (EXPERIMENTAL, optional)
The **S3-Touch board (COM16)** is now flashed with **`esp32s3-pio-live`** and, at the bench, **boots,
advertises `Stages 62144`, and its 172×320 touch UI works** (all 5 screens + tap nav verified). It's
configured to read your **Assioma** (`source=ASSIOMA`). What is **NOT yet tested:** the live Assioma
connection (no meter at the desk) and on-bike SB20 pairing — so treat this as an experiment, **not** the
proven path (the C3 + phone above is that).
- **To try it:** power the S3 near the bike. On the panel's **Ride** screen, pedal — if it connects to the
  Assioma, POWER should track and the IN→OUT title shows Assioma → Stages. Pair the SB20 to `Stages 62144`
  exactly as with the C3 (session 8/9 rules). Walk the 5 screens by tapping (**Ride/Setup/More**, then
  **More → Workout / Calibrate**).
- **The physical panel is the ground truth** — the bench serial-screenshot tool corrupts the 220 KB frame
  under BLE-scan contention (oversized BMP), so trust your eyes, not a bench capture.
- **Reflash / recover:** `python code/scripts/flash_s3.py --env esp32s3-pio-live --port COM16`
  (or `--env esp32s3-pio` for the mock-data demo build). Boots in ~3 s; advert confirms it's up.
- **If it misbehaves**, no problem — fall back to the C3 + phone. Note what the panel showed (power not
  tracking? screen glitch? touch dead?) → a desk follow-up (render/parse is host-tested).

## Retro (fill in at the end — [`PLAYBOOK.md`](PLAYBOOK.md) §4)
- **Went well:**
- **Went wrong / slow / confusing (+ root cause):**
- **Changes before next session (process / run-sheet / tooling):**
- **Next gate:** ~~unblock the S3-Touch head-unit~~ **DONE 2026-07-03** — S3 boots + full touch UI works on
  the pioarduino platform (bench-verified). Next S3 gate: an `esp32s3-pio-live` build reading the real
  Assioma, then ride it on the bike as a head-unit alternative to the phone.
