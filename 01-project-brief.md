# 01 — Project Brief

> ⛔ **SUPERSEDED — historical.** Part of the **pre-pivot brief**, written before the on-bike captures
> and before the firmware existed. Kept for provenance. For the current state read
> **[`PROJECT-MAP.md`](PROJECT-MAP.md)** (what already exists) and
> **[`code/findings/decisions.md`](code/findings/decisions.md)** (what was decided and measured).
> Where this doc disagrees with those, **they win.**

## What we're building

A software bridge that lets a Stages SB20 smart bike consume real-time power data from a third-party ANT+ power meter (primary target: Favero Assioma DUO pedals) **as if** that data came from the bike's native onboard Stages crank power meters. The bridge runs on a small computer (Raspberry Pi or laptop) with one or two ANT+ USB sticks. Functionally, it impersonates the Stages cranks on-air so that the SB20's internal control loop — including erg-mode resistance — works unchanged from the bike's perspective.

## Why

1. Stages Cycling ceased operations in 2024; its assets were acquired by Giant Group (SPIA Cycling), which offers a discretionary, time-limited support program for existing owners. Long-term availability of replacement SB20 cranks is uncertain, and the existing units use aging, proprietary CR2032-powered electronics.
2. The SB20 is otherwise a high-quality bike (quiet, solid, electronically controlled flywheel) worth keeping in service.
3. The owner already has and trusts Favero Assioma pedals as their reference power source — used both indoors and outdoors. But while the Assiomas can *record* indoor rides (to a watch/head unit), the SB20's *erg control loop* still runs off the Stages cranks, which read several percent differently. This makes power-zone target sessions imprecise: an erg target of 350 W (driven by the Stages cranks) might correspond to only ~320 W on the Assiomas, the meter the owner actually trains against. Driving erg control from the Assiomas makes indoor targets directly comparable to outdoor efforts.
4. There is no commercial product solving this. Existing workarounds (TrainerRoad PowerMatch, QZ/qdomyos-zwift) substitute the *training app's* view of power but do not change what the *bike itself* uses for erg control. They are app-level bridges, not bike-level ones.

## Use cases (in priority order)

### Use case 1 — Consistent erg control from a standard power meter (current priority)

The owner wants the SB20 to use Assioma data as its primary power source — not just for *display* in connected apps, but for the bike's own **erg/resistance control loop**. The payoff:
- Erg-mode targets are driven by Assioma-derived power, so a 350 W target produces 350 W *as measured by the meter the owner actually trains with* — not by the Stages cranks, which read several percent differently.
- Power-zone and structured target sessions indoors become directly comparable to outdoor efforts recorded on the same Assiomas.
- All training apps connected to the SB20 (Zwift via FE-C, the Stages app, etc.) report Assioma-derived numbers, so there is a single source of truth.

The Stages cranks may still be physically present and functional during this phase, but their data is not used for control.

### Use case 2 — Failure-mode backstop

If the Stages cranks fail (battery contact issues, electronics failure, loss of pairing), the proxy keeps the SB20 fully functional. This is the "insurance" use case — a value the owner gets even before any Stages hardware actually breaks, made more pertinent by uncertain long-term spares availability after the brand's change of ownership.

### Use case 3 — Distributable to other SB20 owners

A clean Github repository with installation instructions, ideally a Raspberry Pi image, that another SB20 owner can pick up and use with minimal technical work. The intended audience is the broader SB20 community — both those facing crank-availability risk and those wanting consistent power between their outdoor meter and indoor erg control.

### Use case 4 — Foundation for future projects

The architecture should make it easy to:
- Swap the input source (any ANT+ or BLE power meter, not just Assioma)
- Spoof other devices in the future (other smart trainers / bikes with similar lock-in problems)
- Layer additional features (data logging, multi-source averaging, etc.)

A specific anticipated future direction: **Peloton bridging** — feeding power data from a chosen source into a Peloton bike's resistance control loop, the same shape of problem as the SB20 spoof but applied to a different closed system. The `cagnulein/qdomyos-zwift` (QZ) project already implements bidirectional Peloton support (read metrics + send auto-resistance) and is the obvious reference for that future work. Designing this project's source/target abstraction with that future in mind will pay off.

## Success criteria

### Phase 0 (diagnostic) — done when:
- We have raw and decoded captures of all ANT+ traffic between the SB20 and its native Stages cranks during idle, pairing, zero-reset, and pedalling
- We have an isolated capture of the Assioma's broadcast for direct comparison
- A written `phase-0-report.md` documents what the SB20 actually requires from a "Stages crank" and what differs from a generic ANT+ power meter
- Failure-mode question answered: precisely what happens (and at which protocol step) when the SB20 is given an Assioma's ANT+ ID instead of a Stages ID

### Phase 1 (replay) — done when:
- A captured Stages stream can be replayed from the proxy hardware to a freshly-paired SB20
- The SB20 accepts the replay, displays power, and erg mode operates against the replayed data
- This proves the protocol model is correct independent of any live Assioma input

### Phase 2 (live proxy) — done when:
- The proxy receives live Assioma broadcasts and re-emits them in the format the SB20 expects
- Power values displayed in the Stages app and in any FE-C client (e.g., Zwift) match the Assioma's own broadcast within a small tolerance
- Erg mode "feels" responsive (subjective, but target end-to-end latency <250 ms; <100 ms is great)
- The proxy survives a one-hour ride without dropouts, channel desyncs, or visible drift

### Phase 3 (productisation) — done when:
- The input side abstracts behind a `PowerSource` interface so the Assioma is just one implementation among several
- Installation on a Raspberry Pi from a clean OS install is documented as a small number of steps
- A Github README explains the project, the hardware required, and the install procedure for non-expert users
- Optional: a pre-built Pi image is published

## Non-goals

To keep scope honest, the following are explicitly out of scope for v1:

- **Replacing the SB20's own resistance/FE-C control.** The bike's flywheel control loop stays unchanged; we only feed it different power numbers.
- **Reverse-engineering the SB20's internal firmware or USB protocol.** We work entirely over its existing ANT+/BLE interfaces.
- **Building a smartphone app.** Configuration is via JSON file or simple CLI.
- **A web/browser-only delivery.** ANT+ is not browser-accessible. (See `08-risks-and-gotchas.md` for why.)
- **Supporting non-SB20 bikes.** Other Stages models or other vendors' bikes may benefit from the same architecture later, but v1 targets the SB20 specifically.

## Open questions (to be resolved during Phase 0)

> **Status (2026-06-15):** most of these are now ANSWERED — see
> [`code/findings/phase-0-report.md`](code/findings/phase-0-report.md) §1/§5. In brief:
> **#1** single-sided is fine (Assioma Unified-channel-L → spoof one master); **#2**
> the only extra handshake is the calibration request, and its response is captured;
> **#3** period **8182** (4 Hz); **#4** transmission type **5**; **#5** the bike needs the
> full Stages contract (manufacturer_id **69**), not just any ID (proven — Assioma IDs
> failed); **#7 NO — the SB20 is pass-through** (FE-C/crank = 0.997), so a direct
> Assioma feed lands erg targets on Assioma watts, and the #7 "calibration model"
> extension below is **de-scoped** (research-only). #6 (battery-out behaviour) still open.

These shape the architecture and must be answered before significant code is written:

1. Does the SB20 require both L and R crank broadcasts, or will it accept a single-sided source? (Stages docs suggest a "single-sided" mode exists, see `02-technical-context.md` §SB20 architecture, but it's unclear whether this is the bike's preferred mode or a degraded mode.)
2. Does the pairing flow include any handshake beyond standard ANT+ Common Pages (manufacturer ID 0x50, product info 0x51, battery 0x52)? Specifically, does the SB20 issue any non-standard requests?
3. What channel period (transmission rate) do the Stages cranks actually use? Standard is 4 Hz / period 8182; some power meters offer higher rates.
4. What transmission type byte do the Stages cranks use, and does the SB20 validate it?
5. Does the SB20 accept an ANT+ device ID in any range, or does it whitelist specific Stages-issued IDs? (Plausibly: no whitelist; the field is a free 5-digit number.)
6. Does removing the batteries from the actual Stages cranks cause the SB20 to display a fault, or does it gracefully wait for re-broadcast? (Affects the "remove the cranks during testing" approach.)
7. Does the SB20 apply any internal scaling/calibration factor to crank power before using it for erg control and display? (Relevant to use case 1: if the bike scales what the cranks report, feeding it raw Assioma watts may not produce Assioma-accurate erg targets — we may need to characterise and compensate for that factor. Session A vs the bike's own displayed/app-reported power answers this.) **Extension (2026-06-10):** the meter-vs-meter correction itself may be a *function*, not a constant — P = τ·ω, and strain-gauge slope errors live in the torque domain, so the Stages↔Assioma delta likely varies with cadence at fixed power. Anticipated mechanism: a wizard-guided "calibration ride" sweeping a power × cadence grid, with a regression fitted per meter pair (which also becomes the onboarding flow for other SB20 owners — use case 3). See `code/findings/decisions.md` (2026-06-10 calibration-model entry). No model code before dual-meter data exists.
