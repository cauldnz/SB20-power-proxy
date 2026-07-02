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

> ⚠️ **The S3-Touch head-unit is a parallel track and is NOT ready.** Its firmware + full UI are built
> and host-tested, but the board won't boot the image yet. Do **not** rely on it for this ride. Next
> step to unblock it: [`advanced-board-s3-touch.md`](../code/findings/advanced-board-s3-touch.md)
> §"Bring-up status" (a UART on GPIO43 for the panic, or the pioarduino/IDF-5 platform).

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

## Retro (fill in at the end — [`PLAYBOOK.md`](PLAYBOOK.md) §4)
- **Went well:**
- **Went wrong / slow / confusing (+ root cause):**
- **Changes before next session (process / run-sheet / tooling):**
- **Next gate:** unblock the S3-Touch head-unit (UART-on-GPIO43 panic capture) so the touch UI can
  replace the phone.
