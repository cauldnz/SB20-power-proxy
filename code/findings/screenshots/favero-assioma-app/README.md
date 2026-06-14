# Favero Assioma app — UI screenshots

Screenshots of the **Favero Assioma** app talking to the owner's Assioma DUO
pedals, captured 2026-06-14 ~08:00–08:02. App version **4.0.52 (593)**.
Background / research — canonical records are the JSONL/FIT captures in
`../../captures/`.

The Assioma is the proxy's **power source**, so its channel/ID layout and the
"Unified vs Dual channel" mode are directly load-bearing for the source side.

## Hardware inventory (two DUO pairs)

The owner has **two pairs** of Assioma DUO; only one has been used so far.
From `saved-devices.png` and `device-pedals.png`:

| Pair | Left pedal | Right pedal | Notes |
|------|-----------|-------------|-------|
| **A (in use)** | ANT+ ID **17039**, S/N 17039.013.118 | ANT+ ID **22428**, S/N 22428.113.119 | both Active, firmware **06.24** |
| B (spare) | ANT+ ID **29064** | (not shown) | saved, not connected |

The pair is identified in the saved-devices list by its **left pedal ID**
(17039). `17039` is the same Assioma ID referenced in `../../decisions.md` as
the source used in the day-1 capture.

## The screenshots

| File | What it shows |
|------|---------------|
| `saved-devices.png` | "My devices" — two Assioma DUO entries: **ANT+ ID 29064** (saved, disconnected) and **ANT+ ID 17039** (connected). ADD / CONNECT. |
| `device-pedals.png` | Device tab — L pedal **17039** (S/N 17039.013.118) and R pedal **22428** (S/N 22428.113.119), both Active, firmware **06.24**. |
| `settings-menu.png` | Settings tab — Crank length, Calibration, Compatibility with other apps, Automatic standby, Travel mode, Power scale factor, Switch from double to single, Firmware update, Advanced settings. |
| `compatibility-channel-mode.png` | **Compatibility with other apps** — *Unified channel L* vs *Dual L/R channel* (currently **Unified channel L**). See insight below. |
| `power-scale-factor.png` | Power scale factor — L **0.0 %**, R **0.0 %** (factory-calibrated; only change on Favero support advice). |
| `static-weight-test.png` | Static weight test — optional accuracy check; only if constant over/under-estimation is suspected. |
| `about-app.png` | About — Favero Assioma app **v4.0.52 (593)**. |

## Insights for the proxy

1. **"Unified channel L" is the mode that matches the SB20's expectation —
   and the mode we should mirror in the spoof.** Per the app's own description:
   *Unified channel L* sends **both pedals' data combined from the left pedal**
   (only L is paired); *Dual L/R channel* sends each pedal independently (both
   must be paired). The SB20's Stages left crank likewise combines + rebroadcasts
   L+R. So the clean source→target mapping is:

   > Assioma **L (17039)** in *Unified channel L* → combined Assioma watts →
   > spoof as Stages **left crank (62144)**.

   This means the proxy needs to read **one** ANT+ channel (Assioma L 17039,
   combined power), not two. Confirms the single-source design in `decisions.md`.

2. **Power scale factor is 0 % / factory** — the Assioma watts are taken as
   ground truth (consistent with the project's "Assiomas are the trustworthy
   reference" stance). No source-side correction to model.

3. **Two pairs available** → useful for redundancy / experiments (e.g. one pair
   as source while testing, the other as an independent reference), and the spare
   IDs (29064 / its right pedal) are worth recording before they're forgotten.

4. **Crank length is set independently in the Favero app** — keep it equal to the
   physical pedal-hole length (172.5 mm per the crank-length thread in
   `decisions.md`) so the source watts we feed are accurate. Once spoofing, only
   the Favero crank-length config matters on the source side.

## Device facts worth keeping

- Assioma pair A (in use): **L ANT+ 17039** (S/N 17039.013.118) / **R ANT+ 22428** (S/N 22428.113.119), firmware **06.24**
- Assioma pair B (spare): **L ANT+ 29064**
- Channel mode: **Unified channel L** (combined power on the left ID)
- Power scale factor: **0.0 % / 0.0 %** (factory)
- Favero app version **4.0.52 (593)**
