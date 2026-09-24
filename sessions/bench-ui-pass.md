# Bench UI pass — every screen and card, no rider

**Status: PLANNED (2026-09-24)** · tracked in [`README.md`](README.md) · drives from
[`../docs/ui-feature-map.md`](../docs/ui-feature-map.md) §4 (the test map) · issue #345.
**Where:** the desk, with the bench camera, the serial console, a phone on the bench WiFi, `fake_meter.py`,
the FTMS trainer sim board and `crank_reader.py`. **Who:** the agent runs every step; the owner is
needed only for the QR-scan and phone-in-hand checks. **Budget:** two to three desk hours per board.

> Purpose: burn down every "never seen on a panel" cell of the map before session 14 spends rider
> time on it, and confirm or refute the defects the map found by reading (#346, #347, the Guition
> layout). A bench row that fails becomes an issue; a bench row that passes moves the map's Verified
> column to today's date.

## 0. Rig (assert before starting)

- [ ] Board under test on the bench WiFi, `/status` reachable by hostname; build SHA noted.
- [ ] Unique spoof identity per the fleet table (`docs/system-reference.md` §2); no other board powered.
- [ ] Bench camera framed on the panel (`BOARDS.md` recipe); a `SCREEN` dump decodes.
- [ ] `fake_meter.py` streaming (log shows `subs=1` once the board attaches); the FTMS trainer sim on
      its own board, name known.
- [ ] A phone joined to the bench WiFi with `/app` open; browser console visible (USB debugging or a
      desktop browser standing in).
- [ ] `route_baseline.py capture` taken as the "before" oracle.

## 1. Order of work (per board: Guition first, then CYD, then the C3 for the OLED rows)

| Step | Map rows | What | Evidence to record |
|---|---|---|---|
| 1 | geometry | Walk Ride → Setup → More → Workout → Calibrate → Compare → (portal) with `TAP` and the camera | one frame per screen; note clipping, dead bands, unreadable text |
| 2 | F13–F15, F19 | Ride screen live with the fake meter ramping | frame at 100 W and 300 W; `/app` hero side by side |
| 3 | F04, F05, F10 | Setup: rescan, pick the fake meter, pick the trainer sim, Save → reboot | `/status` before/after; `STATE` |
| 4 | F20–F23, F16, F17 | Load a preset, start, pause, resume, skip, stop, change; trainer sim log | sim log excerpt; frames of the console states; `/app` Workout card |
| 5 | F41, F38, F42 | More: brightness cycle (four taps), Firmware row, touch-cal (CYD) | frames; confirm or refute #346 |
| 6 | F29, F33 | Calibrate button; Compare screen | confirm the no-op; Compare renders the stub verdict |
| 7 | F01–F03 | Portal: forget, QR scan with the owner's phone, rejoin; WiFi-off | phone screenshot of the join; `/status` after |
| 8 | F18 | `/app` on a phone for 15 minutes with the board rebooted once | does the screen sleep; does the page recover |
| 9 | F06–F09, F30, F34 | `/app` Settings: filter, ×2, mode, identity, curve round-trip, buttons card | `/status`, `crank_reader.py --scan`, `obc_reader.py` |
| 10 | F37 | `route_baseline.py diff` against the "before" capture | 0/57 or the exact differences |

## 2. Actual — fill in as it runs

| Board | Step | Result | Evidence | Issue filed |
|---|---|---|---|---|
| Guition | | | | |
| CYD | | | | |
| C3-OLED | | | | |

## 3. Close-out

Update the map's Verified column for every row that passed; file an issue per failure (link it in the
map's ⚠ cell); move confirmed rows of #346/#347 to "confirmed on hardware"; record the durable
findings in `decisions.md`; then set this doc's status to DONE with the date and hand the bike rows
to session 14.
