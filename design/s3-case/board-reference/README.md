# Board reference — ESP32-S3-Touch-LCD-1.47

The case is designed against Waveshare's official CAD. The full **STEP (3D) + DXF (2D outline)** are in
their `…-2D3D.zip` (too big to vendor here — **14 MB STEP**); download it from:

  https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.47/ESP32-S3-Touch-LCD-1.47-2D3D.zip

The small **2D dimensioned PDF** from that zip is kept here (`*_20250411.pdf`) for quick reference.

**Key geometry extracted from the STEP** (used in `../generate_case.py`):
- PCB **24.06 × 44.01 × 1.2 mm** (`BOARD` body).
- 4× M2 mounting holes / brass H4 standoffs at **(±8.50, ±19.50)** from board centre.
- Display module 19.4 × 36.8, centre offset **−1.2 mm** in Y (toward the USB end).
- Header male pins reach **~9 mm** below the PCB back (both long edges, x ≈ ±8.9, y ∈ [−10.9, 17.0]).
- BOOT/RST tactile switches at **(±9.56, −14.50)** on the **back** face (actuate toward −Z).

To simulate fit yourself: import the STEP into a Fusion assembly and drop the case bodies around it
(scope any cut features to the case body so they don't carve the board — the gotcha I hit first time).
