# 3D-printable case — ESP32-S3-Touch-LCD-1.47 (SB20 head-unit)

A two-part case for the Waveshare **ESP32-S3-Touch-LCD-1.47** board (the S3 head-unit, `esp32s3-pio*`
firmware). Designed in Fusion 360; parametric generator + STLs here.

![assembled](render-assembled.png)
![exploded](render-exploded.png)
![underside](render-underside.png)

## How it mounts (v3 — built around the real hardware)
The board carries **4 mm female M2 brass standoffs** and **8 mm rear header pins** on its **back** (the
non-screen side), so:
- The board sits **back-down** in the tray. Its 4 mm standoffs land on **four printed 4 mm pillars**, so
  the board floats **8 mm** off the floor — the rear headers hang in the cavity between the pillars,
  **fully enclosed**.
- An **M2 button-head screw goes in from OUTSIDE the case back** (counterbored, flush), up through the
  pillar, into each female standoff. ~**M2 × 8 mm**, ×4.
- The **bezel** press-fits into the front and frames the display (two-level window — underside recess
  clears the raised glass, top opening exposes just the active area).

Because the 8 mm headers are enclosed, the case is ~**13.5 mm** thick. If you'd rather it were slimmer,
either clip the headers and drop `header_clear`, or ask for a back-cutout variant.

## Parts
- **`SB20_S3_case_back.stl`** — the tray (pillars + screw counterbores + header cavity + USB-C notch +
  side button slots).
- **`SB20_S3_case_bezel.stl`** — the front frame.

## What's grounded vs. what to verify
| Value | Source | Confidence |
|---|---|---|
| Display active area **17.39 × 32.35 mm**, module 19.39 × 36.28 × 1.46 | LBS147TC-IF15 datasheet | **exact** |
| PCB ≈ **24.5 × 39 × 1.6 mm** | Waveshare drawing | good — caliper |
| 4× M2 holes — top 17.78 / bottom 17.00 apart, 25.40 vertical | Waveshare drawing | good — confirm XY |
| Rear headers **8 mm**, 4 mm female M2 standoffs, both on the **back** | owner-measured | **given** |
| **Header footprint / position** (must not clash with the corner pillars) | — | **verify from a back photo** |
| **Button (BOOT/RESET) positions** | not dimensioned | **guess — must verify** |

So before printing: from a **back-side photo** confirm the M2 hole XY (`hx_top/hx_bot/hy`), that the
**header block doesn't sit where a pillar is**, and the **button positions** (`btn1_y/btn2_y/btn_on`).
Then edit `P{}` in [`generate_case.py`](generate_case.py) and re-run.

## Regenerate
Fusion 360 → **Utilities ▸ Scripts and Add-Ins ▸ Scripts ▸ +** → add [`generate_case.py`](generate_case.py)
→ Run. Builds `Case_Back` + `Bezel_Front` **and auto-exports both STLs** here.

## Print
- **PLA or PETG**, 0.2 mm layers, 3 perimeters, 15–20 % infill.
- **Tray** open-side up (no supports — USB + button openings are open slots; the screw counterbores are on
  the bottom face and print fine).
- **Bezel** window-side **down** (no supports).
- Tune `fit`/`bez_fit` (0.1 mm steps) for the board fit; `pillar_od`/`m2_clear` for the standoff + screw fit.

## Still to do
- **Verify against a back-side photo** (header footprint vs pillars, hole XY, button edge).
- No bike-mount yet (bar clamp / strap slots / quarter-turn) — easy to add to the tray back.
- Print the **bezel first** as a cheap fit-check against the board.
