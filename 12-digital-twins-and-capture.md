# 12 — Digital twins & community capture

> ⛔ **SUPERSEDED — historical.** Part of the **pre-pivot brief**, written before the on-bike captures
> and before the firmware existed. Kept for provenance. For the current state read
> **[`PROJECT-MAP.md`](PROJECT-MAP.md)** (what already exists) and
> **[`code/findings/decisions.md`](code/findings/decisions.md)** (what was decided and measured).
> Where this doc disagrees with those, **they win.**

Two owner ideas (2026-06-15) that turn out to be one coherent pipeline, and a
reusable asset well beyond this one bike.

## The ideas

1. **Digital twin of the SB20** (and of meters): software that *implements the
   bike's/meter's BLE+ANT protocols* so we can develop and test the proxy
   **without riding** — and, more broadly, an open **library of fitness-equipment
   digital twins** other developers can use.
2. **Structured capture mode** on the proxy/companion: beta testers with *other*
   gear (different power meters, different trainer bikes) record structured
   protocol traces and upload them, so we can fold their devices into the tool.

## Why they're one pipeline (and converge with raedian-probe#1)

```
  capture (structured)  ──►  digital twin (replay)  ──►  test proxy vs twin (no hardware)
        ▲                          │                              │
        │                          ▼                              ▼
  beta testers' gear        open twin library            CI / regression
        │                          ▲
        └──────── grows the library + calibration models ────────┘
```

The **impersonation-capture firmware** (`raedian-probe#1`) is the hinge: a device
that *presents a captured GATT profile and logs what connects to it* is, run the
other way, a device that *replays a captured profile to drive a real central* —
i.e. **a twin**. Capture and twin are the same component in two modes. Everything
shares one **structured protocol-recording format**.

## Components

### A. Structured capture format (the lingua franca)
Formalise the JSONL we already emit into a portable, brand-agnostic **device
protocol recording**: metadata (brand/model/firmware/serial, protocol, capture
tool+version, crank length & other config) + the advertisement + the full GATT
table (and/or ANT channel params + pages) + the timestamped read/notify/write
streams. One schema that twins replay, uploads carry, and analysers consume.
*We already have most of this in the capture JSONL — this is mostly documenting
and versioning it.*

### B. Digital twins (replay a recording as a real device)
- **Desktop twin** (Python `bless`/BlueZ peripheral, or ESP32) that advertises a
  captured GATT and replays its notification stream + answers reads/control-point
  ops from the recording. First targets: an **SB20 twin** (FTMS trainer 0x1826 +
  CPS 0x1818 + the crank-pairing behaviour) to test the proxy's bike-facing side,
  and **meter twins** (Stages crank / Assioma CPS) to test the input side.
- **Embedded twin** = the `raedian-probe#1` firmware in replay mode (ESP32).
- Built straight from the captures in `findings/captures/` — today's crank +
  Assioma BLE recons are the first twin source material.

### C. Community capture mode (multi-brand support)
The proxy (or a companion app/the LCD device) ships a **capture mode** that records
a tester's gear in the structured format and uploads it. Each upload (a) grows the
twin library, and (b) feeds the per-pair **calibration** data across brands — so
the tool can support many power meters and trainer bikes, not just Assioma+SB20.
This is the engine behind use-case-3 (distributable) and a genuine community asset.

## Value
- **Develop/test without riding** (the owner's main ask) — and CI regression tests
  against twins instead of a bike.
- **Multi-brand** support via community uploads, not the owner buying every device.
- **An open library** of fitness-equipment protocol twins — useful to QZ-style
  projects and any developer in this space; a contribution larger than the SB20 fix.

## Sequencing (this is future work, after the core proxy)
1. **Now:** capture source material (today: crank + Assioma BLE profiles ✓). Keep
   recordings structured.
2. **Soon:** document/version the capture schema (A) — cheap, unblocks the rest.
3. **With the ESP32 work:** the impersonation firmware (`raedian-probe#1`) doubles
   as the first embedded twin; a small desktop twin built from the SB20 captures
   lets us test proxy logic offline.
4. **At productisation:** the capture-and-upload mode + the growing twin/calibration
   library.

Not a detour from the proxy — the same captures, format, and firmware serve both.
