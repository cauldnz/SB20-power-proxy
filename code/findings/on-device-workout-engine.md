# On-device workout engine — structured workout over a route, deterministic execution

**Status: PLANNED (2026-06-27, owner).** Backlogged at [`forward-plan.md`](forward-plan.md) §14.
No code yet — this is the design + format decision so the build mirrors the existing Python model
rather than forking a new one. Governs the firmware workout executor + the `/workout` routes and the
desk-side FIT/ZWO importers (none of which exist yet).

> Related, already built (reuse — do NOT rebuild):
> - [`ride-director.md`](ride-director.md) — the **Python** steerable session engine
>   (`code/src/sb20proxy/ride/director.py`): `Segment` / `Workout` / `RidePlan` / `Cursor` /
>   `DirectorState`. **This is the reference model the firmware ports.**
> - [`mcp-workout-server.md`](mcp-workout-server.md) — the desk-side compose + drive-over-FTMS tools
>   (`workout/`, `mcp/`). The agent path; this doc is its **on-device** counterpart.
> - [`ftms-protocol.md`](ftms-protocol.md) — FTMS `0x1826` / Set Target Power `0x2AD9`, the erg write
>   the executor uses to drive the SB20.

## The question (owner, 2026-06-27)
"Where does the workout come from? We need a way to set it on the device via a route. Execution is
deterministic, so it needs a structured format. Ideally FIT — but that's binary, so a FIT-derived
JSON/YAML/XML."

## Decision

### 1. Source = a device route + an on-device deterministic executor
Not a stream from Zwift/an app. The device **owns the workout and the clock**:

- **`POST /workout`** — upload a workout → persist to flash (LittleFS/NVS), survives reboot.
- **`GET /workout`** — return it (the LCD profile chart + the phone UI render straight from this).
- **`POST /workout/{start,pause,skip,stop}`** — drive it.
- An on-device executor (a C++ port of `DirectorState`) steps segments on a monotonic clock and writes
  each segment's target watts to the SB20 over **FTMS** (`Ftms.h` + the erg client already exist).

Because the device owns the clock and the erg setpoint, the LCD's Pause/Skip are genuinely ours (not a
read-only mirror of an app). **This also settles the LCD workout-screen's open question: the device is
the erg controller.**

### 2. Format = compact JSON on the device; FIT/ZWO converted on the desk
**On-device canonical = JSON, 1:1 with the Python `Segment`:**

```json
{ "name": "4x8 Threshold", "ftp_w": 285,
  "segments": [
    { "t": 600, "label": "Warm-up",    "pct_ftp": 0.55 },
    { "t": 480, "label": "Interval 1",  "power_w": 250, "cadence_rpm": 90 },
    { "t": 120, "label": "Recovery",    "power_w": 90 }
  ] }
```

Target resolves **`power_w` → `pct_ftp` → `zone`** (the existing `Segment.resolved_power_w`
precedence); `ftp_w` lets `pct_ftp`/`zone` resolve to absolute watts on-device.

**Why JSON, not FIT/YAML/XML on the device:**
- **FIT is binary + fiddly on an MCU** (optional fields, CRC, dev-fields). Keep the device input
  dead-simple and deterministic.
- **ArduinoJson** is mature/compact and already in the firmware deps — no new parser. There's no good
  embedded YAML parser; ZWO-XML parsing on-device is heavier.
- The JSON is **1:1 with the Python `Segment`**, so the firmware executor gets **golden-vector parity
  tests against the Python director** (same discipline as `CalibrationFit`/`Correction`).

**FIT is still supported — converted on the desk**, where the libraries live:
- Importers in `workout/`: **`.zwo`** (Zwift XML, the de-facto %FTP authoring standard) and **`.fit`**
  (via `fitparse`, already a dep from the FIT-correction work) → emit the canonical JSON → `POST /workout`.
- Riders author in Zwift / TrainerRoad / Garmin; we transcode; the device only ever executes the JSON.

## Net
Device speaks JSON over a route, executes deterministically as the FTMS erg controller; FIT/ZWO are
desk-side import formats that compile down to that JSON.

## Phasing (see forward-plan §14 for the backlog entry)
1. **Desk importers** — `workout/` gains `from_zwo()` + `from_fit()` → canonical JSON (host-tested
   against committed sample `.zwo`/`.fit` fixtures); a tiny exporter from the existing `Workout`.
2. **Firmware executor** — port `DirectorState` to a pure `firmware/lib/proxy/WorkoutEngine.h`
   (no radio); golden-vector parity tests vs the Python director.
3. **Routes + persistence** — `WifiLink` `/workout` GET/POST + `{start,pause,skip,stop}`; persist to
   flash; pure render/parse host-tested like `ConfigPage.h`.
4. **Wire to FTMS** — executor's per-segment target → the FTMS Set-Target-Power write; bench-gated
   (coex when stacked on the CPS spoof + WiFi), then on-bike drive.
