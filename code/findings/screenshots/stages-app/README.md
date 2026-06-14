# Stages app — UI screenshots (background / research)

Screenshots of the **Stages Cycling** companion app talking to the SB20
(shown as *Stages Bike 0105*), captured 2026-06-14. These are background /
research material documenting the app's UI and what the bike exposes about
its power meters — not a protocol capture. The canonical lossless records are
still the JSONL/FIT files in `../../captures/`.

## The screenshots

| File | Screen | What it shows |
|------|--------|---------------|
| `01-power-meters-tab-connected.png` | POWER METERS tab | Steady state: power meter listed as **`Stages 62144 : 4963`** = *Connected*, **Crank length 165 mm**, a **Pair with Bluetooth** toggle (off), and a **ZERO RESET** button. Header shows a green link icon (connected). |
| `02-zero-reset-dialog.png` | Zero Reset dialog | Result of tapping ZERO RESET — reports per-side zero-offset values: **Left: 903, Right: 951**. |
| `03-power-meters-tab-connected.png` | POWER METERS tab | Same as 01 (later timestamp); confirms the steady-state layout. |
| `04-power-meters-tab-disconnected.png` | POWER METERS tab | Same screen but with a **broken/unlinked** icon in the header — the disconnected state. Useful to compare connected vs disconnected affordances. |

## Why these matter (key findings)

1. **The bike can use Bluetooth instead of ANT+ for the power meter.**
   The *Pair with Bluetooth* toggle ("Use the power meter Bluetooth
   connection with the Stages Bike") means the SB20→power-meter link is not
   ANT+-only. This opens a **pure-BLE proxy path**: if the bike will accept a
   BLE Cycling Power peripheral as its power source, we could build the proxy
   on a cheap **ESP32** (BLE-only, no ANT+ stick / no Pi). This is exactly the
   ESP32 BLE-CPS route noted in `06-prior-art-and-references.md`
   (mau-lima/ESP32-Bike-Powermeter as a minimal skeleton; QZ's `QZ_ESP32/` for
   the heavier reference). Needs hardware validation: does the bike's BLE
   pairing accept an arbitrary CPS peripheral, or only Stages-branded ones?

2. **The bike stores the IDs of the power meters it listens to.**
   The meter is identified in-app as `Stages 62144 : 4963`. Because the bike
   is told *which* IDs to listen for, a spoof can present those (or other)
   IDs. Implication: we may be able to **keep the real Stages meters'
   batteries installed** and still inject spoofed data, by choosing IDs the
   bike is configured to accept. Worth experimenting with different IDs once
   the spoof is real.

3. **Just re-pointing the bike at the Assioma's ANT+ IDs did NOT work.**
   Owner already tried configuring the bike to listen to the Assioma's native
   ANT+ device IDs — the bike did not accept it. So ID-matching alone is
   insufficient: we need a **proper spoof** that reproduces the correct
   **manufacturer ID, page formats, and the rest of the Bike Power profile
   contract**, not merely the channel/device IDs. This reinforces the Phase 0
   "capture the real Stages broadcast first" discipline
   (`03-central-hypothesis-and-phase-zero.md`).

4. **Record the IDs so we don't lose them.** Values seen here:
   - Power meter: **`Stages 62144 : 4963`**
   - Crank length: **165 mm**
   - Zero-offset (this session): **Left 903 / Right 951**

   Owner's note: print physical stickers with the ID(s) so they aren't lost.
   (Zero-offset values drift per calibration — don't treat 903/951 as fixed.)

## Open questions to resolve with hardware

- Does the BLE toggle let the bike pair to *any* CPS peripheral, or does it
  filter on Stages identity? (Determines whether the ESP32-BLE path is viable.)
- What exactly is `62144 : 4963` — ANT+ device number, a Stages serial, or a
  composite? Cross-reference against a Phase 0 ANT+ capture's channel ID.
- Is the manufacturer-ID requirement (finding 3) on the ANT+ side, the BLE
  side, or both?
