# U2 — schema → codegen for the Bridge/UI contract (design)

**Status:** DESIGN (2026-07-11) — the concrete spec for slice **U2** in
[`ui-architecture-review.md`](ui-architecture-review.md) §7 (= `architecture-remediation.md` **R2**,
widened from "parity test" to "generate the mirrors"). Owner approved **full implementation**.

## The problem this kills

One logical contract — the Bridge packets + the caps flags + the button-action order — is **hand-copied
into four languages** with zero cross-checks:

| Surface | File | Form |
|---|---|---|
| C++ (nRF, host-tested) | `firmware-nrf/lib/bridge/Proto.h` | binary pack/unpack, the de-facto source of truth |
| JS (Web Bluetooth SPA) | `web/index.html` `parse*/pack*` | hand-coded `DataView` offsets |
| Monkey-C (Garmin) | `BridgeBle.mc` | hand-coded byte offsets |
| C++ JSON (ESP32 HTTP) | `firmware/lib/proxy/WebJson.h` | hand-written JSON strings |

A single offset change in `Proto.h` silently corrupts the **primary user control surface** (the SPA) with
**no CI signal** — `test_spa_sync` only checks the embedded copy is byte-identical to `web/index.html`, not
that the bytes are *right*. `test_wire_format_parity.py` proves the idiom works for ANT pages + the OBC
order; U2 generalises it to the whole Bridge contract and **removes the hand-copying** instead of just
testing it.

## Principle — generate the drift-prone mirrors; keep the hand-tuned oracle

`Proto.h` carries **domain validation** (scale 0.5–2.0×, offset ±100 W, curve factor 0.25–4.0×, rec-rate
∈ {13,26,52,104}) that is *not* pure serialization — it's product logic. So U2 does **not** blindly
regenerate `Proto.h`. Instead:

- **The schema is the single source** of every packet's *layout* (offsets, types, scale factors, bit
  positions, enum values) + the caps flags + the button-action order.
- The generator emits the **layout-only** mirrors that have no business being hand-written:
  `web/bridge-codec.js` (the SPA's parse/pack), the Monkey-C offset table, and the ESP32
  `WebJson.h` field list.
- **Validation stays hand-written** in `Proto.h` as small `validate*()` hooks the generated `unpack*`
  calls — domain logic a schema shouldn't own.
- Everyone is **locked together by committed golden vectors** (`bridge-golden.json`): canonical
  `(packet, field-values) → bytes` cases the C++ test, the Node test, and (doc-checked) Monkey-C all
  assert against. Change the schema → regenerate → the golden vectors change → every side must match or CI
  fails.

Net: the SPA/Monkey-C codecs become **generated artifacts** (can't drift); `Proto.h` stays the readable,
validated C++ but is **pinned to the schema** by the shared golden vectors.

## The schema format

A single language-neutral **`ui-schema/bridge.json`** (data, not code), consumed by a Python generator
(`code/scripts/gen_bridge.py`, mirroring the existing **`design/gen_tokens.py`** marker-block + `--check` idiom → Python is already the CI
tooling language). Field **offsets are auto-computed** from declaration order (byte 0 is always the
`PROTO_VER` const), so a reorder is a one-line schema edit, not a hand re-count across four files.

### Field types (cover everything in `Proto.h` today)

| `type` | Wire | Notes |
|---|---|---|
| `u8` `i8` `u16` `i16` `u32` | LE int | `none:<v>` marks a sentinel (e.g. `-1`/`0xFF` = "absent") for nicer JS/JSON |
| `const` | 1 byte | fixed value (the `ver` byte) |
| `flags` | 1 byte | `bits:[...]` → named bools at bit 0,1,2… |
| `enum` | u8 | `values:{name:n}` (RecCmd/RecState/CalCmd/CalWireState/WkCmd) |
| `str` | N bytes | `len:N`, NUL-padded on the wire, NUL-terminated in structs |
| `scaled` | u16/i16 | `raw`×`scale` = model value — binary stores `raw` (e.g. `scaleMilli`), JS/JSON expose the ÷scale float (`1.0`) |
| `array` | count-prefixed | `count:"u8"`, `max:N`, `of:[…fields…]` — for Curve (`{power u16, factor u16}`) + ScanList (`{name str19, rssi i8, flags flags}`) |

### Worked example — the Status packet (exactly matches `Proto.h` offsets)

```jsonc
{ "name": "Status", "char": "0001", "dir": "notify", "len": 20,
  "fields": [
    { "name": "ver",            "type": "const", "value": 1 },
    { "type": "flags", "bits": ["srcConnected","outAdvertising","recording","srcIsAnt","outIsAnt"] },
    { "name": "srcPowerW",  "type": "i16", "none": -1 },
    { "name": "outPowerW",  "type": "i16", "none": -1 },
    { "name": "cadenceRpm", "type": "i16", "none": -1 },
    { "name": "balancePct", "type": "i8",  "none": -1 },
    { "name": "batteryPct", "type": "u8",  "none": 255 },
    { "name": "scale",  "type": "scaled", "raw": "u16", "scale": 1000 },
    { "name": "offset", "type": "scaled", "raw": "i16", "scale": 10 },
    { "name": "recSamples", "type": "u32" },
    { "name": "uptimeS",    "type": "u16" }
  ] }
```
→ offsets `ver@0, flags@1, srcPowerW@2, outPowerW@4, cadenceRpm@6, balancePct@8, batteryPct@9,
scale@10, offset@12, recSamples@14, uptimeS@18`, total 20 — **identical to `packStatus`**.

### The full packet set (from `Proto.h`, ported 1:1 — no invented bytes)

`Status`(0001,20,notify) · `Config`(0002,44,r/w, +validate scale/offset) · `RecCtl`(0003,write) +
`RecState`(0003,12,notify) · `RecData`(0004, header/data/trailer framing) · `Curve`(0005,var,r/w, array,
+validate factor) · `Calibrate`(0006,write) + `CalState`(0006,16,notify) · `ScanList`(0007,var,notify,
array) · `Workout`(0008,write) + `WkState`(0008,18,notify) · `Buttons`(0009,8,r/w).
Enums: `RecCmd, RecState, CalCmd, CalWireState, WkCmd`. Validation hooks: `Config`(scale 500–2000, offset
±1000), `Curve`(factor 250–4000), `RecCtl`(rate ∈ {13,26,52,104}).

### Also in the schema (the non-Bridge contracts that drift the same way)

- **`caps`** — the capability flags (`config, scan, calibration, workout, recording, buttons, antCapable,
  scalarCorrection`) with the **per-transport values** (BLE vs HTTP), so the SPA's two hand-written `caps`
  objects (`LvglUi`… no — `web/index.html`) become generated. Source of the `scalarCorrection:false`
  ESP32 gate verified live 2026-07-11.
- **`buttonActions`** — the shared `sb20ActionOptions` order (index↔token), already parity-tested
  (`test_wire_format_parity.py`); folds into the same schema so the SPA `OBC_ACTIONS`, the firmware map,
  and the docs share one list.
- **`httpConfig`** — the ESP32 `/config` + `/scan` + `/curve` JSON shapes (`WebJson.h`) so the HTTP
  transport's field names/types are generated too (the `mode`, `has_curve`, `single_sided`, `src_filter`,
  `out_name` set verified over HTTP 2026-07-11).

## Generator outputs + how each is verified

`code/scripts/gen_bridge.py --emit <target>`:

| Output | Path | Verified by |
|---|---|---|
| Golden vectors | `ui-schema/bridge-golden.json` | committed; the oracle everyone checks |
| JS codec | `web/bridge-codec.js` (+ inlined into `WebSpa.h` via `gen_spa_header`) | **new Node test** round-trips golden vectors; `test_spa_sync` keeps the embedded copy in sync |
| C++ golden test | `firmware*/test/.../test_bridge_golden.*` | asserts `Proto.h` pack/unpack == golden vectors (locks C++ to schema) |
| Monkey-C offsets | `BridgeBle.mc` region (or a `.mcgen` include) | doc-checked vs golden; a `monkeyc` CI job is a stretch (needs the Garmin SDK) |
| `WebJson.h` field list | `firmware/lib/proxy/WebJson.h` (generated region) | existing host tests + a golden JSON case |
| caps + buttonActions | emitted into SPA + firmware | extends `test_wire_format_parity.py` |

**CI:** add a Node step to `tests.yml` (Node parity test) + a `gen_bridge.py --check` that fails if any
generated file is stale vs the schema (same guard style as `test_findings_index.py` / `test_project_map.py`).

## Rollout (each its own PR; U2 is a mini-track)

- **U2a — foundation + the highest-drift mirror.** Land `ui-schema/bridge.json` (all packets) +
  `gen_bridge.py` + committed `bridge-golden.json`; generate `web/bridge-codec.js`, wire the SPA to import
  it (keep the single-served-file via `gen_spa_header` inlining), add the **Node parity test** + the C++
  golden test. This alone removes the SPA↔`Proto.h` drift — the biggest blast radius.
- **U2b — the rest of the mirrors.** Generate the `WebJson.h` field list + the `caps`/`buttonActions`
  exports; fold `test_wire_format_parity.py` onto the generated artifacts.
- **U2c — Monkey-C.** Generate the offset table + a golden reference; a `monkeyc` CI job is a stretch goal
  (SDK-gated) — at minimum the doc + shared golden JSON is the reference.
- **`--check` in CI** from U2a so nothing can drift once it's generated.

## Open decisions for the owner (before U2a code lands)

1. **Schema language:** JSON (proposed — language-neutral data) vs a Python DSL (executable, richer). JSON
   keeps the "single source is data" property + is trivially consumed by JS too. **Recommend JSON.**
2. **Node in CI:** the Node parity test needs a Node step in `tests.yml`. OK to add? (The alternative — a
   Python JS-interpreter — is worse.) **Recommend adding Node.**
3. **Generate into `Proto.h` or keep it hand-written + golden-locked?** **Recommend keep hand-written**
   (its validation is domain logic) and lock it to the schema via the golden vectors — lowest risk, keeps
   the readable C++.
4. **Monkey-C depth:** generate-and-doc-check now, or defer the whole `.mc` until there's a Garmin SDK CI
   job? **Recommend generate + doc-check now** (cheap; the drift risk is real even without a CI build).

*Once you're happy with 1–4, U2a is the first implementable branch. It's gated on a green build/CI, which
is currently blocked by the intermittent safety-classifier outage — everything above is authorable and
reviewable without it.*
