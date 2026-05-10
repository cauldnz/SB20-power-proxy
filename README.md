# SB20 Power Proxy

A man-in-the-middle ANT+ proxy that lets a [Stages SB20 smart bike](https://stagescycling.com/) consume power data from a Favero Assioma (or any standard ANT+) power meter as if it were the bike's native Stages cranks — preserving erg-mode resistance control and providing a backstop when the original Stages cranks fail.

> **Status: pre-implementation.** This repository is currently a planning and briefing package, plus diagnostic-capture tooling. The proxy itself does not yet exist. Phase 0 (diagnostic capture) is the immediate work item.

## Why this exists

Stages Cycling went bankrupt. The SB20 is otherwise an excellent piece of equipment, but its onboard L/R crank power meters use proprietary CR2032-powered electronics with a limited spares supply. When (not if) those cranks fail, the bike loses erg-mode resistance control and becomes a dumb spin bike. This project builds a software replacement for those cranks using any standard ANT+ power meter as the actual sensor — Favero Assioma DUO pedals as the reference implementation.

The result is a small Python application (target: Raspberry Pi or laptop with one or two ANT+ USB sticks) that:
- Subscribes to your real power meter as a slave
- Re-broadcasts the same data as a master, in a form the SB20 accepts as its own crank
- Lets the SB20's internal control loop — including erg mode — work unchanged from the bike's perspective

## Where to start

| If you are… | Read |
|---|---|
| **The project owner / first-time user** | [`START-HERE.md`](START-HERE.md) — Windows + WSL setup, Phase 0 capture sessions step-by-step, troubleshooting |
| **An engineer (or Claude Code) picking up the codebase** | [`HANDOFF.md`](HANDOFF.md) — your role, first task, and what's not in this package |
| **Just curious about the design** | [`01-project-brief.md`](01-project-brief.md) and then [`03-central-hypothesis-and-phase-zero.md`](03-central-hypothesis-and-phase-zero.md) |
| **An SB20 owner with the same problem** | [`START-HERE.md`](START-HERE.md), substituting your own ANT+ device IDs |

## Repository layout

```
.
├── README.md                       ← this file (project front page)
├── START-HERE.md                   ← user-facing quick-start (Windows + WSL)
├── HANDOFF.md                      ← engineer-/Claude-Code-facing brief
├── CLAUDE.md                       ← Claude Code conventions and pointers
├── CLAUDE-CODE-PROMPT.md           ← ready-to-paste opening prompt for Claude Code
├── CHANGELOG.md                    ← package revision history
├── LICENSE                         ← MIT
│
├── 01-project-brief.md             ← goals, use cases, success criteria
├── 02-technical-context.md         ← what we know about the systems
├── 03-central-hypothesis-and-phase-zero.md   ← the key uncertainty + how to resolve it
├── 04-architecture.md              ← software architecture proposal
├── 05-implementation-phases.md     ← phased delivery plan
├── 06-prior-art-and-references.md  ← annotated reading list
├── 07-hardware-and-environment.md  ← what to buy, dev setup, Windows + WSL
├── 08-risks-and-gotchas.md         ← things that will hurt
├── 09-exploring-captures.md        ← the capture analysis workflow
├── 10-relationship-to-QZ.md        ← whether this should eventually live in QZ
│
└── code/
    ├── pyproject.toml              ← openant + optional [dev], [ble], [analysis]
    ├── scripts/
    │   ├── 01_capture_stages.py    ← Phase 0: forensic ANT+ capture (any Bike Power device)
    │   ├── 02_capture_assioma.py   ← thin wrapper around 01
    │   ├── 03_ingest_jsonl_to_influx.py   ← JSONL → InfluxDB
    │   ├── 04_summarize_capture.py        ← JSONL → markdown summary
    │   └── 05_diff_captures.py            ← two JSONLs → side-by-side markdown diff
    ├── src/sb20proxy/              ← library code (mostly stubs pre-Phase-1)
    │   ├── reading.py              ← the PowerReading dataclass — the seam between
    │   │                             input sources and output targets
    │   ├── sources/                ← input side (where power data comes from)
    │   └── targets/                ← output side (where it gets re-broadcast to)
    ├── docker/                     ← InfluxDB + Grafana docker-compose stack
    ├── grafana/                    ← starter Phase-0 dashboard
    └── findings/                   ← committed history: captures + analyses + decisions
```

## Architecture, in one paragraph

The Stages SB20 has two L/R crank power meters that broadcast on ANT+ and BLE. The bike's internal computer pairs with the cranks (the left one combines L+R and broadcasts the combined value), and uses that combined power as the input to its erg-mode resistance control loop. It also re-broadcasts the same number to external apps (Zwift, TrainerRoad) over its own ANT+ FE-C and BLE FTMS interfaces. Existing app-side bridges (TrainerRoad PowerMatch, [QZ/qdomyos-zwift](https://github.com/cagnulein/qdomyos-zwift)) operate at the app level — they change what apps see, but not what the bike's internal control loop uses. **This project sits one layer down**: it impersonates the L crank to the bike, so the bike's internal control loop runs on Assioma-derived power. Apps then see Assioma-derived power too, automatically, because the bike re-broadcasts whatever its internal computer thinks the power is. See [`02-technical-context.md`](02-technical-context.md) for the full picture.

## Relationship to broader research

This project sits inside a wider fitness-sensor integration effort. There is a parent `Research_Content` document (in the project's reference materials, not in this repo) that surveys ANT+/BLE protocols, Python and Rust libraries, Raspberry Pi hardware paths, browser/WASM delivery, and target devices including the SB20, Tacx Neo, and Concept2 PM5. **That document is foundational context** for engineering work — at minimum read its "Protocols Primer", "Python Libraries", and "Target Devices → Stages SB20" sections.

## Important framing

This project has a high-risk, high-reward early phase. Most of [`04-architecture.md`](04-architecture.md) and [`05-implementation-phases.md`](05-implementation-phases.md) becomes wrong if Phase 0 reveals that the SB20's pairing flow is more bespoke than expected. **Do not write proxy code before Phase 0 captures are committed and analysed.** Capture first; design after. See [`03-central-hypothesis-and-phase-zero.md`](03-central-hypothesis-and-phase-zero.md).

## License

MIT — see [`LICENSE`](LICENSE). Note that several prior-art projects (notably `qdomyos-zwift`) are GPL-3.0; this project is intentionally permissive so other SB20 owners can adopt it without copyleft constraints. Do not copy GPL-3.0 code into this repo; reimplement clean-room from understanding the protocol, not the source.

## Acknowledgements

- The maintainers of [openant](https://github.com/Tigge/openant) — the Python ANT+ library this project is built on.
- [dhague/vpower](https://github.com/dhague/vpower) — closest architectural prior art for "rebroadcast as an ANT+ Bike Power master from Python".
- [cagnulein/qdomyos-zwift](https://github.com/cagnulein/qdomyos-zwift) — the most substantial open-source project in the indoor-fitness bridging space; studied for patterns and conventions even though we don't share code (license-incompatible).
- The SB20 owner community who keeps these bikes alive after Stages's bankruptcy.
