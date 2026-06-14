# Stages Cycling app — UI screenshots (background / research)

Screenshots of the **Stages Cycling** companion app talking to the SB20
(shown as *Stages Bike 0105*), captured 2026-06-14. Background / research
material documenting the app's UI and what the bike exposes — not a protocol
capture. The canonical lossless records are still the JSONL/FIT files in
`../../captures/`.

Two capture sessions are filed here:
- **Earlier set** (`01`–`04`, afternoon, crank length showed 165 mm) — the
  POWER METERS tab + Zero Reset.
- **Fuller set** (descriptively-named files, ~07:53–07:55, crank length now
  **172.5 mm**) — a full tour of every tab and the SETUPS pages.

## Device identity (from the INFO tab)

- Name: **Stages Bike 0105**, model **StagesBike SB20**
- Serial number: **H0512210105**
- Firmware: **1.12.4+3792**
- Ride screen max target power: **1000 W**
- Auto-connect: on

## The screenshots

### Power meters (POWER METERS tab)
| File | What it shows |
|------|---------------|
| `power-meters-tab-172mm.png` | Meter **`Stages 62144 : 4963`** = *Connected*, **Crank length 172.5 mm** (note: corrected up from the 165 mm seen in the earlier set), *Pair with Bluetooth* toggle (off), ZERO RESET. |
| `power-meters-edit-ant-ids.png` | The edit dialog behind that row: **Left ANT ID `62144`**, **Right ANT ID `4963`** — confirms the meter "62144 : 4963" is literally the two ANT device IDs, and that **both are user-editable**. |
| `01-power-meters-tab-connected.png` | Earlier set — same tab, crank length **165 mm**, connected (green link). |
| `02-zero-reset-dialog.png` | Earlier set — Zero Reset result: **Left 903 / Right 951**. |
| `03-power-meters-tab-connected.png` | Earlier set — duplicate of 01 (later timestamp). |
| `04-power-meters-tab-disconnected.png` | Earlier set — same tab in the **disconnected** state (broken link icon). |

### Other tabs
| File | What it shows |
|------|---------------|
| `info-tab.png` | INFO tab — the device identity facts listed above. |
| `data-tab.png` | DATA tab — live **Power 0 W**, **Cadence 31 rpm**, **Balance 100:0** (single-sided, left only — consistent with Stages docs that the left crank combines + rebroadcasts). |
| `setups-tab.png` | SETUPS tab — three saved setups: **Zwift Dream**, **Power Tuning**, **Power Erg Mode** (+ add). |

### Ride screen — resistance control modes
The ride screen's control selector switches how the bike's resistance is driven:
| File | Mode | What it shows |
|------|------|---------------|
| `ride-target-power-mode.png` | **Target power (ERG)** | Lightning icon selected; resistance held to a wattage target (here **146 W**, ± to adjust). This is the bike's native ERG mode. |
| `ride-grade-mode.png` | **% Grade (sim)** | Mountain icon selected; resistance follows a gradient (here **0.0 %**). |
| `ride-external-mode.png` | **External** | Chain/link icon selected, labelled *External* — resistance handed to an **external controller** (e.g. a training app over FE-C/FTMS). |

### Devices / pairing
| File | What it shows |
|------|---------------|
| `devices-list.png` | App device list: **`56954-1` Heart Rate Monitor** (not connected) and **Stages Bike 0105 / StagesBike SB20** (connected, green link). |
| `add-device.png` | Add-device categories: Power meter, Stages Dash GPS computer, Stages smart bike, Studio bike, Heart rate. |

### SETUPS → Zwift Dream (virtual shifting)
| File | What it shows |
|------|---------------|
| `setup-zwift-dream-gearing.png` | Gradient scale factor **106 %**, Equipment weight **8 kg**, shift feedback toggles, Gear setup *Dream drive*, Big shift **4 gears**, Total gears **50**. |
| `setup-zwift-dream-buttons-1.png` | Button configuration *Custom* — lever button map; note **Left 3 / Right 3 = External**. |
| `setup-zwift-dream-buttons-2.png` | Satellite-button map (A/B/C/D) for shift up/down, plus a **Brake** toggle. |

## Why these matter (key findings)

1. **The meter IDs are literally editable Left/Right ANT IDs.**
   The edit dialog (`power-meters-edit-ant-ids.png`) shows **Left ANT ID 62144 /
   Right ANT ID 4963** as free-text fields. This both *answers* the earlier open
   question ("what is 62144 : 4963?" → the two ANT device numbers) and confirms a
   spoof can target chosen IDs — opening the door to **keeping the real Stages
   meters' batteries in** while injecting on different/own IDs. (Owner action:
   print physical stickers with the IDs so they aren't lost.)

2. **The bike has a native ERG ("Target power") mode and an "External" mode.**
   The ride screen exposes Target-power / %-Grade / External control. The core
   project goal — "Stages ERG runs correctly using the Assiomas as the source" —
   lives in this Target-power loop; the proxy replaces the crank watts that loop
   reads. The **External** mode (and the *Power Erg Mode* / *Power Tuning* SETUPS)
   are worth probing for whether resistance can be driven externally without a
   power-meter spoof at all.

3. **BLE can be the power-meter source, not just ANT+.** (As in the earlier set.)
   The *Pair with Bluetooth* toggle means the SB20↔meter link isn't ANT+-only →
   a **pure-BLE proxy on a cheap ESP32** may be viable (see
   `06-prior-art-and-references.md`: mau-lima/ESP32-Bike-Powermeter, QZ `QZ_ESP32/`).

4. **Re-pointing the bike at the Assioma's native ANT+ IDs did NOT work.**
   (From the earlier set / owner report.) ID-matching alone is insufficient — we
   need a full spoof (correct manufacturer ID + page formats + the whole Bike
   Power contract), not just the device IDs. Reinforces Phase-0-capture-first.

5. **Crank length was corrected 165 → 172.5 mm** between the two sets — consistent
   with the crank-length finding in `../../decisions.md` (165 was the wrong default;
   172.5 matches the physical pedal holes and the Favero config).

## Device facts worth keeping

- Power meter ANT IDs: **Left 62144 / Right 4963**
- Crank length (current): **172.5 mm**
- Zero-offset (earlier session, drifts per calibration — not fixed): **L 903 / R 951**
- SB20 serial **H0512210105**, firmware **1.12.4+3792**, max target power **1000 W**
- HR strap seen: **56954-1**

## Open questions to resolve with hardware

- Does the *Pair with Bluetooth* toggle let the bike pair to *any* CPS
  peripheral, or only Stages-branded? (Determines ESP32-BLE viability.)
- Can the **External** ride mode (or *Power Erg Mode* SETUP) drive resistance
  from an external app over FE-C/FTMS — and does ERG then close on *that*
  source rather than the crank?
- Is the manufacturer-ID requirement (finding 4) on the ANT+ side, the BLE
  side, or both?
