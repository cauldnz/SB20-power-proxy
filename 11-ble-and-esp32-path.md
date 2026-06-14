# 11 — BLE path & ESP32 productisation

Strategic + technical planning for taking the proxy off ANT+/Pi and onto a cheap,
self-contained **BLE device (ESP32)** — the path to use-case-3 (distributable to
other SB20 owners). Written after the day-1 ANT+ success and after reviewing the
owner's `cauldnz/raedian-probe` project, which supplies most of the substrate.

> **Sequencing rule:** ANT+ baseline first (broadcast, no pairing — proves the
> protocol model + that erg tracks Assioma watts), *then* commit to BLE only once
> **Session G** confirms the SB20's BLE crank pairing gives full erg and is
> spoofable. This doc plans for that, it doesn't jump the gun.

## Why ESP32/BLE at all

- **ESP32 cannot do ANT+** (Nordic/Garmin-licensed radio). So any cheap, embedded,
  single-purpose proxy *must* run on the BLE crank-pairing path. ESP32 = committing
  to BLE.
- **Upside:** ~A$2–5 chip (ESP32-C3), tiny, low-power, instant-on, no OS to maintain,
  an optional screen for on-device calibration UX. Ideal for shipping to other owners.

## The core architecture — and it's a known-good shape

The proxy is a **dual-role BLE device**: a *central* connected to the Assioma, and a
*peripheral* impersonating the Stages crank to the bike, concurrently.

```
  Assioma pedals ──BLE CPS notify──▶ ESP32 (central) ─┐
                                                       │  proxy: relay power +
                                                       │  answer calibration
   SB20 bike ◀──BLE CPS (impersonated crank)── ESP32 (peripheral) ◀┘
                 + Cycling Power Control Point (calibration)
```

- **Dual-role is confirmed feasible:** NimBLE on ESP32 supports concurrent
  central+peripheral with up to ~9 connections.
- **This is the same shape as `raedian-probe`'s `esp32_bridge_spec.md`** — "charger
  half = BLE client to the wallbox" + "decoupled control loop" + "OLED/QR
  onboarding". We map: *charger half → SB20-crank-peripheral + Assioma-central half*;
  *control loop → relay Assioma watts (+ answer calibration)*; *onboarding/status →
  the calibration/setup UX*. **Reuse the bridge's NimBLE client, OTA, observability,
  and OLED-onboarding scaffolding wholesale.**

## What we reuse from `raedian-probe` (the big de-risk)

| Need (SB20 BLE path) | Reuse from raedian-probe |
|---|---|
| Characterise the crank as a BLE peripheral (to impersonate it) | the staged recon toolkit: `scan.py` / `enumerate.py` / `listen.py` (our `06_capture_ble.py` is a subset — adopt their structure) |
| Drive a calibration and capture the response | `probe_write.py` pattern (guarded single writes) → write Cycling Power Control Point 0x2A66, observe the indication |
| Passively capture the **bike↔crank** link incl. bonding + the bike's exact calibration write | the **nRF52840 sniffer → Wireshark → JSON** pipeline in `sniffer_setup_runbook.md` + `pcap_analyze.py` |
| ESP32 firmware: NimBLE, OTA, remote debug/observability, OLED onboarding | `esp32_bridge_spec.md` firmware scaffold — same ESP32-C3 toolchain |
| Methodology / doc patterns | HANDOVER baton, recon_plan, protocol_map — mirror in our `findings/` |

The upshot: the *unglamorous* 80% (toolchain, OTA, sniffer pipeline, BLE-client
patterns, onboarding UX) is already built. The SB20-specific 20% is the crank
impersonation + the SB20's calibration handshake — which Session G captures.

## Board strategy (mirrors the EVSE "dev board vs hero SKU" split)

- **Dev / premium UX:** the **Waveshare ESP32-C6-LCD-1.47** (~A$18, 1.47″ ST7789
  172×320, BLE 5.0). The LCD runs an **on-device calibration/setup wizard** — the
  guided ride, on the device, no laptop. Strong use-case-3 differentiator.
- **Hero / cheap SKU:** **ESP32-C3 + 0.96″/1.3″ OLED** (~A$2–5 + ~A$2), exactly the
  `esp32_bridge_spec.md` hero-SKU recipe (and the owner already has C3 + OLED boards).
  QR onboarding + status panel; configured over WiFi/phone.
- (C6's WiFi 6 is irrelevant here — the C6-LCD board is bought *for the screen*. Any
  of C3 / C6 / S3 work; pick by form factor, screen, price.)

## The real risks (all BLE-specific, all Session-G-gated)

1. **Bonding/security on the SB20↔crank link** — does the bike bond (pairing keys,
   identity binding)? If it bonds tightly, impersonation is harder. *Sniffer answers this.*
2. **Does BLE-crank mode even give full erg?** The whole path is void if the SB20's
   "Pair with Bluetooth" mode is degraded vs ANT+. *Session G must confirm erg works.*
3. **Exclusive connections** — a BLE peripheral typically allows one central. The
   Assioma's BLE is exclusive to the ESP32 — but the Assioma **dual-broadcasts ANT+
   too**, so the owner's watch keeps the Assioma's ANT+ stream. (Our day-1 BLE survey
   confirmed `ASSIOMA17039L` advertising while ANT+-active.)
4. **Reconnection robustness** — stateful BLE links drop; both sides must recover
   cleanly mid-ride. This is the engineering tail (the bridge spec's anti-flap /
   state-handling patterns transfer).

## Bottom line

The ESP32/BLE proxy is feasible, the dual-role question passes, and ~80% of the build
is already done in `raedian-probe`. It's gated entirely on **Session G** — capturing
the SB20↔crank BLE conversation (active recon for the crank profile + passive sniff
for bonding & the bike's calibration). Spec for that: `findings/session-G-ble-capture-spec.md`.
