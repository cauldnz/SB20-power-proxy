# Stages Power Meter app (StagesPower) — UI screenshots

Screenshots of the **StagesPower** app (the app for the *crank power meters*,
distinct from the *Stages Cycling* bike app in `../stages-app/`) talking to the
SB20's two Stages crank meters, captured 2026-06-14 ~07:57–07:58. App version
**4.10.1 (build 353)**. Background / research — the canonical records are the
JSONL/FIT captures in `../../captures/`.

This app is important because **these two crank meters are exactly what the
proxy must spoof** — so their full parameter set is the spec we're cloning.

## The screenshots

| File | What it shows |
|------|---------------|
| `splash-connect.png` | App splash — *Connect with Bluetooth*; version 4.10.1 (353). |
| `discovering-devices.png` | Device discovery — finds **Stages Bike 0105**, **Stages 62144**, **Stages 4963**. Note: "2 devices must be selected to link a LR crankset." |
| `devices-selected.png` | Same list with **62144** and **4963** both selected (the L/R pair). |
| `connected-firmware-prompt.png` | Both connected; a Firmware Update prompt (newer available). |
| `connected-power-cadence.png` | Live readout — **POWER 152 W**, **CADENCE 48 rpm** ("Must ride bike to display…"). |
| `device-details-calibration.png` | **The full parameter dump** (see table below) — serials, ANT IDs, crank length, slopes, DPOT/DAC, PCB orientation, device type, linked IDs, gyroscope. |
| `firmware-update-dialog.png` | Per-meter firmware update — *Update 4963 (latest 1.8.5)* / *Update 62144 (latest 1.8.5)*. |
| `zero-reset.png` | Zero-reset / calibration — "Position crank pointing straight down"; shows per-meter ADC + temperature; *Perform Zero Reset*. |

## The Stages crank meters — full parameter set (the spoof target)

From `device-details-calibration.png` (Left = 62144, Right = 4963):

| Parameter | Left | Right |
|-----------|------|-------|
| ANT+ ID | **62144** | **4963** |
| BLE / Serial number | **11821518** | **20421194** |
| Crank length | **165.0 mm** | **165.0 mm** |
| Slope | 0.04796 | 0.04782 |
| Temp slope | −0.6741 | −0.8662 |
| DPOT / DAC | 122 | 92 |
| PCB orientation | 4 | 5 |
| Device type | STAGES SMART | STAGES SMART |
| Firmware | 1.8.2 (latest 1.8.5) | 1.8.2 (latest 1.8.5) |
| Gyroscope | enabled | — |

Linking: **Left ID 62144 ↔ Right ID 4963** form one LR crankset (the left
combines + rebroadcasts, per Stages docs). Live sample: 152 W / 48 rpm.

## Insights for the proxy

1. **This is the device identity to clone.** ANT+ IDs 62144/4963, device type
   "STAGES SMART", and the page contract these emit are precisely what the SB20
   expects. Cross-reference these IDs against the Phase-0 ANT+ capture
   (`62144`, device type 11 / page 0x10) — they line up with `decisions.md`.

2. **Crank length stored in the meter = 165 mm**, but the *Stages Cycling bike
   app* now shows **172.5 mm** (`../stages-app/power-meters-tab-172mm.png`).
   These are two different settings. Open question for the crank-length thread
   in `../../decisions.md`: **which length is authoritative for the power the
   SB20 actually consumes** — the value baked into the crank meter (165) or the
   bike-app value (172.5)? This matters for the day-1 vs session-2 ratio
   prediction. Resolve by capture: change one, watch whether broadcast watts move.

3. **Slope / temp-slope / DPOT are per-meter calibration constants** — we do NOT
   need to reproduce these in a spoof (we inject finished watts, not raw strain),
   but they document why the two real meters read slightly differently, and they
   matter if the meters are ever kept in the loop on alternate IDs.

4. **The meters can run BLE as well as ANT+** (BT serials present, app connects
   over Bluetooth) — another data point for the BLE-proxy path.

## Device facts worth keeping

- Stages crank ANT+ IDs: **L 62144 / R 4963**; BLE/serial **L 11821518 / R 20421194**
- Device type **STAGES SMART**; firmware **1.8.2** (1.8.5 available)
- Crank length (in-meter): **165.0 mm** (cf. bike app 172.5 mm)
- StagesPower app version **4.10.1 (build 353)**
