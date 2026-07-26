# SB20 Power Proxy

**Read a power meter → correct it → re-broadcast it so a consumer accepts it as its own.**

The primary form is an **ESP32-C3 BLE device carried on the bike**. It is dual-role: a BLE *central* that
subscribes to a real power meter's Cycling Power Service, and a BLE *peripheral* that re-presents the
corrected power. Its first job is to impersonate a [Stages SB20](https://stagescycling.com/) smart bike's
native crank so the bike's erg loop runs off a third-party meter (Favero Assioma) — the SB20 accepts only
its own crank, so the spoof has to be byte-faithful.

> **Status: working firmware, proven on the bike.** The crank spoof rides — including the calibration /
> zero-reset handshake the SB20 demands — and there is a second product mode (meter-to-meter corrector),
> a web setup + dashboard UI, OTA updates, an LCD head-unit, and a beta tester programme.
> **Two firmware targets** ship: ESP32-C3 ([`firmware/`](firmware/)) and nRF52840 ([`firmware-nrf/`](firmware-nrf/)).
> Python desk tooling ([`code/`](code/)) does capture, calibration fitting, replay and the original ANT+ proxy.

## Start here

> ### ⭐ [`PROJECT-MAP.md`](PROJECT-MAP.md) — read this before planning or building anything.
> It is the inventory of **what already exists** — every shipped capability and every doc. This README is
> only the front door; the map is the territory, and CI keeps it from going stale.

| If you are… | Read |
|---|---|
| **Planning or building anything** | [`PROJECT-MAP.md`](PROJECT-MAP.md) — find the capability before you build it |
| **New to smart bikes / power meters / BLE fitness protocols** | [`code/findings/domain-primer.md`](code/findings/domain-primer.md) — concepts + verified spec facts |
| **Looking for what was decided, measured or refuted** | [`code/findings/decisions.md`](code/findings/decisions.md) — the append-only log (the source of truth) |
| **Working on a subsystem** | [`code/findings/README.md`](code/findings/README.md) — the index of every findings doc and the tooling it governs |
| **Running an on-bike session** | [`sessions/PLAYBOOK.md`](sessions/PLAYBOOK.md), tracked in [`sessions/README.md`](sessions/README.md) |
| **Doing desk development** | [`DEV-PLAYBOOK.md`](DEV-PLAYBOOK.md) · conventions in [`CLAUDE.md`](CLAUDE.md) |
| **A beta tester (or running the beta)** | [`beta/ONBOARDING.md`](beta/ONBOARDING.md) · [`USERS-PLAYBOOK.md`](USERS-PLAYBOOK.md) |
| **Identifying a physical board** | [`BOARDS.md`](BOARDS.md) |

## Why this exists

Stages Cycling ceased operations in 2024; its brand and assets were later acquired by Giant Group (via
subsidiary SPIA Cycling), which now offers a discretionary, time-limited "Rider Support Program" for
existing owners. Long-term availability of replacement SB20 crank power meters remains uncertain. The SB20
is otherwise excellent, but its onboard L/R crank meters use proprietary CR2032-powered electronics with an
aging spares supply. If those cranks fail and can't be replaced, the bike loses erg-mode resistance control
and becomes a dumb spin bike.

Two distinct motivations:

1. **Resilience / backstop.** A software path that keeps the SB20 fully functional if (when) the native
   cranks fail, independent of vendor support.
2. **Power-source consistency.** Many owners ride the same Assioma pedals outdoors and on the SB20, but the
   SB20's *erg control* still runs off the Stages cranks — which often read several percent different. You
   set a 350 W erg target, the bike drives you to 350 W *by its own cranks*, and your Assiomas (your
   training reference) record ~320 W. Driving the erg loop from your standard meter makes indoor targets
   directly comparable to outdoor efforts.

## Two product modes, one core

1. **SB20 crank spoof** — *must* impersonate the Stages L crank (`Stages 62144`, byte-faithful `0x2F`
   framing, answer the control point), because the SB20 only accepts its own crank. This feeds the bike's
   internal erg loop from a third-party meter.
2. **Meter-to-meter corrector** — read one meter, re-broadcast on another's scale under our **own**
   identity (no spoof needed; head units accept any standard CPS meter).
   See [`code/findings/meter-to-meter-proxy.md`](code/findings/meter-to-meter-proxy.md).

Existing app-side bridges (TrainerRoad PowerMatch, [QZ/qdomyos-zwift](https://github.com/cagnulein/qdomyos-zwift))
operate at the app level — they change what apps *see*, but not what the bike's internal control loop
*uses*. **This project sits one layer down.** Apps then see corrected power automatically, because the bike
re-broadcasts whatever its internal computer believes the power to be.

## Repository layout

```
.
├── PROJECT-MAP.md      ← ⭐ the capability + doc inventory (start here)
├── CLAUDE.md           ← engineering conventions and invariants
├── DEV-PLAYBOOK.md     ← the desk dev loop · USERS-PLAYBOOK.md ← working with testers
├── BOARDS.md           ← physical board inventory (MACs, VID:PIDs, which build)
│
├── firmware/           ← ESP32-C3 / CYD / S3 (PlatformIO) — the primary product
│   ├── lib/proxy/      ←   the PURE, host-tested core (ProxyCore, Cps, Correction, Config…)
│   ├── src/            ←   the hardware seam (ble/, net/, disp/) — only this needs a board
│   └── test/           ←   host unit tests: `pio test -e native`
├── firmware-nrf/       ← nRF52840 (XIAO Sense) target — shares lib/proxy via lib_extra_dirs
│
├── code/               ← Python desk tooling + the original ANT+ proxy
│   ├── src/sb20proxy/  ←   sources/ targets/ core.py ble/ ant/ calibration.py ride/ twins/
│   ├── scripts/        ←   capture / fit / replay / proxy entry points
│   ├── tests/          ←   the hermetic suite: `pytest -q`
│   └── findings/       ←   ⭐ protocol facts, decisions.md, and the committed captures
│
├── web/                ← the setup/dashboard SPA (generated into the firmware as WebSpa.h)
├── sessions/           ← on-bike session plans + actuals, and the session ledger
├── beta/               ← tester onboarding, ride protocol, release & OTA runbook
├── tools/              ← dev-env provisioning + doctor.ps1 (the build/flash/capture pre-flight)
└── design/, docs/, ui-schema/
```

## Quick start

**Firmware** (from `firmware/`, PlatformIO):

```bash
pio test -e native                   # host unit tests for the pure core (no board)
pio run -e esp32c3-oled-live-ota     # compile — the pre-flash gate
./flash.ps1                          # OTA flash (RSSI pre-flight, auto-retry, reboot verify)
```

Then observe a running board at `http://sb20proxy.local/` — with `/status`, `/log` (serial-over-HTTP, the
main live instrument), `/stats`, and `/setup` to pick the meter source over WiFi.

**Python** (from `code/`):

```bash
python3 -m venv .venv && source .venv/bin/activate
pip install -e ".[dev,analysis,ble]"
pytest -q                            # the hermetic suite (no hardware, no network)
python scripts/03_static_replay.py --radio loopback --input findings/captures/<cap>.jsonl
```

That last command is the **keystone hardware-free check**: it replays a real on-bike capture through the
proxy and an in-process twin. "PASS" means the spoof path works end-to-end.

## How this project works

A few invariants explain most of the codebase. The full set is in [`CLAUDE.md`](CLAUDE.md).

- **Real-data-first.** No codec or correction is built ahead of the on-bike capture that grounds it.
  Fixtures come from the real committed captures in `code/findings/captures/`, never invented bytes.
- **JSONL captures are the canonical lossless record** — never edited; summaries are derived from them.
- **A pure core behind a hardware seam.** `firmware/lib/proxy/` compiles and unit-tests with no radio and
  is shared by both firmware targets; only `firmware/src/` needs a board. Anything desk-testable ships
  with tests in the same commit.
- **`decisions.md` is append-only** — every numeric value chosen, hypothesis refuted, and "it works now".
- **MIT, clean-room.** GPL prior art is read to *understand*, never copied.

## Historical documents

The numbered root docs (`01-…`–`12-…`), [`START-HERE.md`](START-HERE.md), [`HANDOFF.md`](HANDOFF.md) and
[`CLAUDE-CODE-PROMPT.md`](CLAUDE-CODE-PROMPT.md) are the **pre-pivot brief**, written before the captures
and before the firmware existed. The ride and session cards at the root (`RIDE-CARD.md`,
`CALIBRATION-RIDE-CARD.md`, `BIKE-SESSION-*.md`, `NEXT-BIKE-SESSION.md`, `HANDOFF-NEXT-SESSION.md`) are
point-in-time operational cards, superseded by [`sessions/`](sessions/README.md).

All of them stay at the root **on purpose**: append-only `decisions.md` links them, so moving them would
break the historical record. Each carries a banner at the top saying so. They are useful background — but
where they disagree with `PROJECT-MAP.md` or `code/findings/`, **those win.**

## License

MIT — see [`LICENSE`](LICENSE). Several prior-art projects (notably `qdomyos-zwift`) are GPL-3.0; this
project is intentionally permissive so other SB20 owners can adopt it without copyleft constraints. Do not
copy GPL-3.0 code into this repo; reimplement clean-room from understanding the protocol, not the source.

## Acknowledgements

- The maintainers of [openant](https://github.com/Tigge/openant) — the Python ANT+ library the desk tooling
  is built on.
- [dhague/vpower](https://github.com/dhague/vpower) — closest architectural prior art for "rebroadcast as an
  ANT+ Bike Power master from Python".
- [cagnulein/qdomyos-zwift](https://github.com/cagnulein/qdomyos-zwift) — the most substantial open-source
  project in the indoor-fitness bridging space; studied for patterns and conventions even though we don't
  share code (license-incompatible).
- The SB20 owner community who keeps these bikes alive as vendor support winds down.
