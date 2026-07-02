# 3D-printable case — ESP32-S3-Touch-LCD-1.47 (SB20 head-unit)

A two-part snap/press-fit case for the Waveshare **ESP32-S3-Touch-LCD-1.47** board (the S3 head-unit,
`esp32s3-pio*` firmware). Designed in Fusion 360; parametric generator + STLs here.

![assembled](render-assembled.png)
![exploded](render-exploded.png)

## Parts
- **`SB20_S3_case_back.stl`** — the tray: a shelf the PCB rests on, a cavity behind it for back-side
  components, and an **open-top USB-C notch** on the bottom edge (a slot, so it prints without bridging).
- **`SB20_S3_case_bezel.stl`** — the front frame: press-fits into the tray and retains the board. It's a
  **two-level window** — a shallow recess on the underside clears the raised display glass, and a smaller
  opening on top exposes just the **active area (17.39 × 32.35 mm, exact from the LCD datasheet)**.

## ⚠️ Calibrate before you print (important)
The **display window is exact**, but the **board outline is an estimate** (26 × 48 × 1.6 mm) — I couldn't
measure your physical board remotely. **Measure your PCB with calipers** and set these in
[`generate_case.py`](generate_case.py) `P{}`, then regenerate:

| Param | What to measure | Default |
|---|---|---|
| `board_w` / `board_l` | PCB width × length (mm) | 26 × 48 |
| `board_t` | PCB thickness | 1.6 |
| `lcd_off_y` | display centre offset from the board centre, along its length (+ = away from USB) | 0 |
| `usb_w` / `usb_h` | USB-C opening | 10 × 4.2 |
| `fit` / `bez_fit` | clearance around the PCB / bezel slide-fit — loosen if tight | 0.35 / 0.30 |

A quick sanity check: the tray's inner pocket is `board_w+2·fit` × `board_l+2·fit`; if that doesn't match
your board, the numbers above are off.

## Regenerate
Fusion 360 → **Utilities ▸ Scripts and Add-Ins ▸ Scripts ▸ + ▸** add [`generate_case.py`](generate_case.py) → Run.
It builds `Case_Back` + `Bezel_Front` in a new document. Export each body to STL (right-click the body →
**Save As Mesh**, binary, High refinement), or re-slice the ones here if your board matches the defaults.

## Print
- **PLA or PETG**, 0.2 mm layers, 3 perimeters, 15–20 % infill.
- **Tray:** open-side up (no supports — the USB notch is an open slot).
- **Bezel:** window-side **down** on the bed for a crisp screen opening (no supports).
- If the bezel is tight in the tray, bump `bez_fit` by 0.1 mm; if the board rattles, reduce `fit`.

## Not included / next
- No mounting features yet (bar mount / lanyard loop / strap slots) — easy to add to the tray back.
- No button pass-throughs (BOOT/RESET) — add if you need them accessible.
- Fit + display alignment are unverified on a real print (dimensions estimated). Print the **bezel first**
  as a cheap fit-check against the board before committing to the tray.
