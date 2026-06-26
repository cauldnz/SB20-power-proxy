# SB20 hardware & behaviour reference (community prior art — PedalSmart.blog)

**Status: secondary-source reference (2026-06-27).** A systematic harvest of the **PedalSmart.blog**
(Craig's DIY SB20 blog — the best community SB20 resource we've found,
[06-prior-art-and-references.md](../../06-prior-art-and-references.md)). Clean-room: **understanding only,
nothing copied.**

> ⚠️ **This is a COMMUNITY source, not our measurements.** It's an owner's reverse-engineering + repair
> notes, not a Stages spec. Treat every claim as a **hypothesis to confirm against our own captures** —
> the **measured bytes in [`captures/`](captures/) + [`decisions.md`](decisions.md) win on any conflict.**
> Where a claim already lines up with our captures, that's noted. Source: ~20 SB20 posts read 2026-06-27;
> the rest of the blog is mechanical maintenance (belt/bearings/BB/tyres) — not protocol/electronics.

## 1. Internals / electronics

- **Bike (Upper PCB):** a **Nordic nRF52832 SoC** does the wireless (BLE + ANT+, shared antenna). It reads
  the **shifter buttons** (virtual gears) → tells the Lower PCB to change resistance; takes resistance
  commands from apps; relays PM + ride data. **LED: red = powered, blue = connected to an app.**
- **Each crank** has the **same nRF52832**, a **CR2032**, a **strain gauge** (torque via deflection) and an
  **accelerometer** (crank angle → cadence). Power = torque × cadence, sent **~once/second**.
- **Lower PCB:** controls the **eddy-current brake** — an aluminium flywheel shell spinning around
  electromagnets; **24 V** from the external supply is modulated to set field strength = resistance.
- **No Stages firmware updates since ~late 2023** (Stages Cycling wound down — `what-happened-to-stages-cycling`).
  ⇒ no upstream fixes coming; the device behaviour is frozen.

## 2. Wireless topology & pairing  *(several items reconcile our measured reality)*

- **Internal default is ANT+:** Right crank → Left crank → Bike, all **ANT+**; the **Left crank
  consolidates R+L** into one message. Bike → apps (Zwift) is **BLE FTMS**.
- ⭐ **"Pair with Bluetooth" (Stages app → Devices → Power Meters) switches the crank↔bike link to BLE.**
  This is almost certainly **why our BLE crank spoof is accepted**: with it on, the SB20 reads its crank
  over BLE, so our BLE peripheral can be that crank. *(Confirm we are in this mode on the bench.)*
- **FTMS connection limit = 2** concurrent. Budget connections: spoof-read + Zwift + Stages app can exceed
  it. *(Plausible contributor to the session-9 pairing flakiness — worth checking.)*
- **Dual-broadcast (ANT+ *and* BLE at once) doubles radio load → dropouts;** the blog's fix is BLE-only
  (drop ANT+ dongles) + "use the SB20 (not the left crank) as the Zwift power source" to cut update rate.
- **Data is richer direct from the left crank** (force + crank-angle / pedalling dynamics) and **stripped to
  power+cadence+L:R when taken via the bike.** Our spoof presents *as the crank*, so a head unit may look
  for those extra fields — our CPS `0x2F` framing covers the standard ones; pedalling-dynamics extras are
  not something we emit.

## 3. Erg behaviour  *(the most important finding for our value-prop)*

- ⭐⭐ **The SB20 only enables Erg mode if its Stages cranks are "working."** A bare third-party meter →
  **free-ride/sim only, NO erg** ("when Zwift requests erg, the SB20 stays in free-ride"); it does *not*
  matter how Zwift is configured. **⇒ our crank spoof is precisely what unlocks third-party erg** — by
  making the SB20 believe its crank works. This is the mechanism behind our whole approach (and our
  sessions 2–9 confirm the SB20 ergs off the spoof). **Corollary risk:** if a spoof isn't accepted *as a
  working crank*, erg silently won't engage (you'd get free-ride only).
- **Crank-based power ⇒ smoothing lag:** the SB20 filters noisy 1 Hz crank data, so a rapid cadence/
  resistance change can take **up to ~5 s** to settle the erg target; while "hunting" (power swings
  high→low→high) resistance holds steady. Relevant to our FTMS erg expectations — the lag is inherent.

## 4. Power, torque, crank length & zero-reset

- **Torque = Mass(kg) × 9.81 × crank-length(m).** Static check with a 25 lb (11.34 kg) weight:

  | crank length | torque |
  |---|---|
  | 165 mm | 18.5 Nm |
  | 170 mm | 18.9 Nm |
  | 172.5 mm | 19.2 Nm |
  | 175 mm | 19.5 Nm |

  ⇒ **crank length scales torque/power directly** (165 vs 172.5 ≈ −3.7 %). Matches our crank-length-scaling
  expectation and the "set 172.5→165 to trim power" trick.
- ⭐ **Crank length is stored ON the cranks**, set in **Stages app → Stages Bike → Devices → Power Meter →
  "Crank length"**; the app **reads the cranks' saved config when you open the Power Meter tab**. The app
  pushes *bike* settings on every restart, **but NOT crank settings** (cranks read their own). **This is the
  likely explanation for §11's `--`:** crank length lives on the crank and is exchanged over the Stages
  proprietary path when the Power Meter tab reads it — *not* standard CP `0x04`. Our spoof must answer that
  read with a real value (and accept a set) for the app to show it. *(Grounds the §11 capture plan.)*
- **Crank length is the de-facto user "calibration":** Stages exposes no end-user slope cal; changing crank
  length nudges power **±~5 %** (longer = higher). Some owners report the StagesPower app can apply
  **undocumented offset/slope** changes.
- **Zero-reset = a torque OFFSET, not a calibration.** After a reset "torque should read 0 Nm"; the L/R
  numbers are "torque added/subtracted from each PM reading" (units **Nm**), normally stable (±1 Nm between
  resets). It does **not** calibrate force measurement (factory-only). The crank **auto-zeros when at rest**
  (uses the accelerometer's crank-angle-from-vertical). Affected by pedals, temp/humidity, low battery.
  *(Aligns with our BLE zero-reset offset ≈ 0 and the ANT+ 903 raw-offset reconciliation — decisions.md.)*
- **Battery:** CR2032, Stages-rated 150–200 h (tester saw ~100–120 h reliable). Power keeps working on a
  weak cell (voltage-regulated) **but torque reads marginally LOW near end-of-life** — corroborates our
  "old cell reads ~12–14 % low; use a fresh CR2032" note. Battery % is read over BLE but unreliable < ~90 %.

## 5. Single-crank / crank-rescue  *(extends §12 + the 2026-06-26 single-crank note)*

- Native single-crank mode: **pull the dead crank's battery + select "Stages Bike" as the power source** →
  the **bike doubles the surviving crank** (validates our `singleSidedDouble` ×2).
- **But erg needs a working crank (§3):** a bare third-party meter on a dead-crank bike = **free-ride only**.
  So the crank-rescue *product value* depends on our **spoof** making the SB20 see a working crank — that's
  what restores **erg**, which the doubling trick alone does not.
- Zwift fallback config the blog gives: **Resistance = Stages Bike · Power = 3rd-party · Cadence = 3rd-party**
  (free-ride works; erg does not — until a crank/spoof is present).

## 6. How this maps to our work (verify-against-capture)

| Blog claim | Our doc / next step |
|---|---|
| Erg gated on a working Stages crank | Validates the **spoof's reason to exist**; note in [pre-beta-plan](pre-beta-plan.md). Confirm a spoof that pairs also *enables erg* (sessions already suggest yes). |
| "Pair with Bluetooth" → crank↔bike is BLE | Why our BLE spoof works; **bench-confirm the bike is in this mode**. [phase-0-report](phase-0-report.md). |
| Crank length stored on the crank, read via the Power Meter tab | Direct clue for the **§11 crank-length bridge** capture plan ([forward-plan §11](forward-plan.md)). |
| Crank-length → torque table; ±5 % cal trick | Cross-check against our power-topology / ~11 % finding ([sb20-power-topology](sb20-power-topology.md)). |
| Zero-reset = torque offset (Nm), auto-zero at rest | Corroborates our zero-reset offset semantics ([decisions](decisions.md) 2026-06-17/19). |
| FTMS connection limit = 2 | Possible factor in pairing flakiness — check during the next session. |
| Single-crank doubling = real SB20 behaviour | Validates `singleSidedDouble` ×2 ([supported-meters](supported-meters.md)). |

## 7. Tools surveyed (not useful for RE)

- **GearView** (`introducing-gearview-for-sb20`) and **BattView** (`introducing-battview-battery-scanner`)
  are **iOS dashboard / battery-% apps** by the same author — they *consume* standard BLE (power/cadence/HR,
  battery service), but the posts disclose **no protocol/UUID/RE detail**. Not a source for our codec work.
