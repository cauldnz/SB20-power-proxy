# 04 — Architecture (v0)

This is the proposed software architecture **as of pre-Phase-0**. Treat as a working draft. Revise after Phase 0 reveals what the SB20 actually requires.

## High-level system diagram

```
                       ┌─────────────────────┐
                       │ Assioma DUO (ANT+)  │
                       └──────────┬──────────┘
                                  │ ANT+ slave subscription
                                  ▼
                       ┌─────────────────────┐
                       │ AssiomaSource       │  (PowerSource impl)
                       │ - listens, decodes  │
                       │ - normalises units  │
                       └──────────┬──────────┘
                                  │ in-process call (PowerReading event)
                                  ▼
                       ┌─────────────────────┐
                       │ ProxyCore           │
                       │ - receives readings │
                       │ - smoothing? no.    │
                       │ - dispatches        │
                       └──────────┬──────────┘
                                  │
                                  ▼
                       ┌─────────────────────┐
                       │ StagesAntTarget     │  (PowerTarget impl)
                       │ - ANT+ master       │
                       │ - emits 0x10/0x50.. │
                       │ - handles 0x01 cal  │
                       └──────────┬──────────┘
                                  │ ANT+ master broadcast
                                  ▼
                       ┌─────────────────────┐
                       │ Stages SB20 internal│
                       │ - sees a "crank"    │
                       │ - runs erg loop     │
                       └─────────────────────┘
```

## Module layout

```
src/sb20proxy/
├── __init__.py
├── core.py                     # ProxyCore — reading dispatch, lifecycle
├── reading.py                  # PowerReading dataclass; canonical event schema
├── sources/
│   ├── __init__.py
│   ├── base.py                 # PowerSource ABC
│   ├── assioma.py              # AssiomaAntSource (Phase 2)
│   ├── replay.py               # ReplayFileSource (Phase 1; emits captured stream)
│   └── generic_ant.py          # generic ANT+ Bike Power slave (post-v1)
├── targets/
│   ├── __init__.py
│   ├── base.py                 # PowerTarget ABC
│   ├── stages_ant.py           # StagesAntTarget — the master spoofer
│   └── stages_ble.py           # StagesBleTarget — alternative path (post-v1)
├── ant/
│   ├── __init__.py
│   ├── pages.py                # ANT+ Bike Power page encoders/decoders
│   ├── common_pages.py         # 0x50/0x51/0x52/0x01 helpers
│   └── channel.py              # thin wrapper around openant master channel
└── cli.py                      # entry points: capture, replay, run
scripts/                         # standalone runnable scripts during early phases
findings/                       # capture artefacts and reports — committed
```

The intention: source vs target is the key seam. The proxy doesn't care which power meter feeds it, and (eventually) doesn't care whether the target is the SB20's ANT+ inputs or some other consumer. Phase 1 swaps `ReplayFileSource` in; Phase 2 swaps `AssiomaAntSource` in.

## Core abstractions

### `PowerReading` (canonical event)

```python
@dataclass(frozen=True)
class PowerReading:
    timestamp: float            # monotonic seconds
    power_w: int                # instantaneous power, watts
    cadence_rpm: int | None     # may be unknown
    crank_event_count: int | None  # if available (improves cadence accuracy)
    accumulated_power: int | None  # cumulative torque-equivalent
    left_balance: int | None    # 0..100, percentage
    source_id: str              # for logging — "assioma:12345"
```

This is the lingua franca between sources and targets. Sources convert their native protocol into this; targets convert this into their native protocol.

### `PowerSource` ABC

```python
class PowerSource(ABC):
    @abstractmethod
    async def start(self) -> None: ...
    @abstractmethod
    async def stop(self) -> None: ...
    @abstractmethod
    def on_reading(self, callback: Callable[[PowerReading], None]) -> None: ...
```

`AssiomaAntSource` opens an ANT+ slave channel scoped to the configured Assioma's device ID and emits a `PowerReading` per received page 0x10 (or 0x12 — whichever Assioma actually broadcasts; Phase 0 confirms).

For the receive side, openant's high-level `openant.devices.power_meter.PowerMeter` class does most of this work for us — the lower-level approach in the Phase 0 capture script is intentional (forensic-grade logging of every byte) but the proxy's `AssiomaAntSource` should use the high-level class so we get standard page parsing, manufacturer/product page handling, and battery status for free. Reference snippet from the parent research:

```python
from openant.devices.power_meter import PowerMeter
from openant.devices import ANTPLUS_NETWORK_KEY
from openant.easy.node import Node

node = Node()
node.set_network_key(0x00, ANTPLUS_NETWORK_KEY)
pm = PowerMeter(node, device_id=0)  # 0 = wildcard
pm.on_device_data = lambda page, name, data: ...
node.start()
```

### `PowerTarget` ABC

```python
class PowerTarget(ABC):
    @abstractmethod
    async def start(self) -> None: ...
    @abstractmethod
    async def stop(self) -> None: ...
    @abstractmethod
    def push_reading(self, r: PowerReading) -> None: ...
```

`StagesAntTarget` runs an ANT+ master channel with channel parameters chosen to mimic a Stages crank (specifics TBD by Phase 0). It maintains:
- An accumulating event counter
- A page-rotation schedule (mostly 0x10, with 0x50/0x51/0x52 interleaved per spec)
- A small state machine for handling incoming acknowledged messages, principally page 0x01 calibration requests

`push_reading` updates the master's "what to broadcast next" state. The master itself runs on its own broadcast cadence regardless of how often `push_reading` is called.

### `ProxyCore`

Bookkeeping and lifecycle. Simple class that owns one source and one target, wires source events to target updates, and reports stats (last-reading-age, dropouts, calibration events).

## Concurrency model

`asyncio` throughout. openant uses background threads for USB I/O; bridging into asyncio is straightforward — wrap callbacks with `loop.call_soon_threadsafe(queue.put_nowait, reading)`. Avoid any heavy processing in the openant callback thread.

The master broadcast cadence is driven by openant itself (the ANT+ stick generates "send next message" events at the channel period). Our job is to keep the "next message" buffer up to date.

## Configuration

A single TOML file:

```toml
[source]
type = "assioma_ant"
device_id = 12345

[target]
type = "stages_ant"
spoof_device_id = 67890     # the ANT+ ID we present as
channel_period = 8182       # 4 Hz; revisit after Phase 0
manufacturer_id = "stages"  # or "favero", "generic" — see Phase 0
hw_revision = 1
sw_revision = 1
serial_number = 67890

[hardware]
ant_stick_for_source = "auto"   # by VID/PID
ant_stick_for_target = "auto"   # may need separate stick — see below

[runtime]
log_level = "info"
findings_dir = "./findings"
record_to = "./findings/proxy-runs"  # rolling JSONL of input/output for debugging
```

## One stick or two?

Open question and probably stick-dependent.

- One stick can run multiple ANT+ channels simultaneously (typically up to 8). So in principle, one stick can both subscribe to the Assioma (slave) and broadcast as Stages (master). 
- In practice, a master channel and a slave channel on the same stick share airtime; performance can suffer. The Garmin/Dynastream sticks are well-tested for this; cheaper clones may not be.
- For initial development and Phase 0 captures, **two sticks is recommended** — one for capture/snoop, one for transmit. After Phase 2 stabilises, test one-stick mode.

## Calibration handling

The most uncertain part of the design. Until Phase 0 is done, the plan is:

1. When the `StagesAntTarget` receives a page 0x01 calibration request from the SB20, it immediately enqueues a page 0x01 response with success ID 0xAC and a plausible offset value (e.g., 0x0500 — to be tuned).
2. **Optionally**: forward the calibration request to the underlying Assioma via its own control point and wait for the real response. This is more correct but adds latency and another failure mode. v1 should fake it; v2 can pass through.
3. Track the SB20's calibration-result display: if it shows the offset we returned, we know the response was accepted. This becomes a quick smoke test during integration.

If Phase 0 reveals the SB20 expects a more elaborate handshake (e.g., a custom auto-zero sequence), update the state machine.

## What about FE-C / resistance control?

We do **not** touch FE-C. The SB20 continues to be the FE-C device. External apps continue to send resistance/erg/sim commands to the SB20 directly. The SB20's internal controller closes the loop using whatever it thinks current power is — which is now our proxied number. No change to FE-C behaviour from the bike's perspective.

## What about BLE?

Out of scope for v1. The SB20 supports BLE pairing for the cranks but ANT+ is the default and is more flexible (multi-receiver, no pairing/bonding state). v2 may add a `StagesBleTarget` for owners whose SB20s for whatever reason work better over BLE; Phase 0 captures should record the BLE manufacturer-data advertisement strings so we know what to spoof if we ever go there.

When the BLE target work eventually happens, the strongest reference is `cagnulein/qdomyos-zwift` (QZ) — they have extensive battle-tested BLE FTMS/CPS/CSCS peripheral code in C++/Qt. See `06-prior-art-and-references.md` for licensing implications (GPL-3.0; clean-room reimplementation only, no code copying).

## Open architectural questions

These flow back into Phase 0 / Phase 1 priorities:

1. **Single-sided vs dual-sided**: do we need to spoof one device or two? See `02-technical-context.md` §"Single-sided mode".
2. **Manufacturer ID**: do we present as Stages, as Favero, or as something else? Implications for the `manufacturer_id` config field.
3. **One stick vs two**: see above.
4. **Latency budget**: subscribed Assioma → callback → push_reading → next master broadcast slot. Worst case this is one full broadcast period (250 ms at 4 Hz). Acceptable for erg "feel"? Phase 2 testing answers this.
5. **What if the SB20 expects to be pinged at >4 Hz?**: investigate channel period in Phase 0; we may need to broadcast at 8 Hz (period 4091).

## Long-term language strategy (Phase 5+ consideration)

The parent `Research_Content` document recommends a Python-first/Rust-later evolution path for fitness-sensor work on a Pi:

- **Today**: openant in Python is the most mature ANT+ stack. `ant-rs` works for receive but the master/transmit side is still rough.
- **Tomorrow**: when `ant-rs` stabilises (the maintainer's roadmap suggests 2026-ish), the deployed proxy could collapse into a single Rust process with `bluer` for BLE and `ant-rs` for ANT+.
- **In between**: a hybrid pattern works — `openant` Python subprocess publishing ANT+ events over MQTT or a UNIX socket, with a Rust process subscribing for the business logic. The parent research describes this pattern in detail.

For v1 this project stays pure Python. Don't optimise for the Rust future until Phases 0–3 are working — but keep the source/target abstraction clean so a future port doesn't require rewriting domain logic.

### Portability disciplines (also relevant if we ever upstream into QZ — see `10-relationship-to-QZ.md`)

The same disciplines that keep a Rust port cheap also keep a C++/Qt port (into QZ) cheap. Specifically:

- **Don't bake Python types into the public API of the source/target seam.** No `asyncio.Future` in ABCs; use plain callbacks or simple queues. The current `PowerSource.on_reading(callback)` / `PowerTarget.push_reading(reading)` shape is fine.
- **Document protocol as bytes, not as Python.** The Phase 0 report should describe "what we need to spoof" in terms of channel parameters, page bytes, calibration response shape — not in terms of openant API calls. The bytes port; the API calls don't.
- **Keep configuration declarative (TOML).** A C++ port can read the same TOML; an imperative Python setup can't be ported cleanly.

These are good Python practice anyway. They just happen to also serve the future-port option.
