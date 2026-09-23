# Captures index

The committed, lossless record of every Phase-0 capture. JSONL is canonical (never edit a
capture; produce a new analysis). Conclusions drawn from these live in
`../phase-0-report.md` (synthesis) and `../decisions.md` (chronology).

> Naming drifted across sessions; these tables are the authoritative inventory, and CI
> (`code/tests/test_captures_index.py`) fails on a file that is not listed here or a row naming a file
> that is not there. One Phase-0 file is **misnamed** (flagged below). Add the row in the same change as
> the capture. Session captures from session 2 onward are grouped by session below the Phase-0 tables.

## ANT+ captures (`01_capture_stages.py` / `07_capture_multi.py`, run in WSL)

| File | Device(s) | What it is |
|---|---|---|
| `A-stagesL-steady-20260610-1740.jsonl` | Stages L crank 62144 | First **smoke** capture (~56 s, old script, no ext-messages). Verdict REVIEW. First sighting of `manufacturer_id=69`. |
| `A-stagesL-steady-20260614-165737.jsonl` | Stages L crank 62144 | **The real Session A** (898 s, hardened script). Validator **PASS**; 2,575 broadcasts; power 0→569 W. |
| `C0-ack-dryrun-20260614-1636.jsonl` | Stages L crank 62144 | First C-0 zero-reset attempt — **missed** the calibration page (timing). Kept as history. |
| `C0-ack-dryrun-20260614-164426.jsonl` | Stages L crank 62144 | **The C-0 that worked** — 8× page-0x01, `0xAC` + offsets **903 / −950**. The keystone Phase-0 result. |
| `QUICK-multi-20260615-064037.jsonl` | Stages 62144 + Assioma 17039 + **bike FE-C 105** | Multi-source, one clock. Source of the **#7 pass-through** result (FEC/crank=0.997) + calibration spot-check (~1.13). |

## BLE captures (`06_capture_ble.py`, run on native Windows)

| File | Device | What it is |
|---|---|---|
| `ble-adv-survey-20260614-1607.jsonl` | (passive scan) | Advertisement survey — who's on the air (Stages/CPS advertisers). |
| `G-stagesL-ble-recon-20260615-064641.jsonl` | **the BIKE** (not the crank!) | ⚠ **Misnamed.** Connected by name "Stages" → grabbed the bike's FTMS device `Stages Bike 0105`. Useful: bike FTMS GATT + Nordic DFU. |
| `G-crank62144-ble-20260615-065556.jsonl` | Stages L crank 62144 | Crank BLE recon by address — model SPM2, CPS, crank-length 172.5 read off hardware. |
| `G-assioma17039-ble-20260615-065730.jsonl` | Assioma L 17039 | Assioma BLE recon by address — model Assioma, CPS, battery 73%. |
| `G-crank62144-ble-zero-20260615-070353.jsonl` | Stages L crank 62144 | BLE zero-reset — offset 0, no bonding needed. |
| `G-assioma17039-ble-zero-20260615-070458.jsonl` | Assioma L 17039 | BLE zero-reset — offset −1, no bonding needed. |

## Passive qdomyos Peloton ride (2026-07-06, `sessions/CAPTURE-qdomyos-sb20-passive.md`)

| File | Device | What it is |
|---|---|---|
| `QDZ-sb20-ftms-gatt-20260706-0739.jsonl` | the bike (`E4:AA:5A:D6:0E:D4`) | Pre-ride **uncontended FTMS GATT dump** (149 events; reads + subscriptions, no writes). |
| `QDZ-sniff-qdomyos-sb20-20260706-0742.pcap` | (nRF sniffer, followed `E4:AA:5A`) | 30-min ride sniff — **negative result**: E4 advertised all ride ⇒ qdomyos drives resistance WITHOUT connecting to the bike's FTMS surface. Advert timelines + the "SB20 Bridge"/spoof/crank went-silent evidence. See decisions 2026-07-06. |
| `QDZ-ant-20260706-0742.jsonl` | (ANT+ discovery scan) | Device inventory during the ride: Stages `#62144`, `#17039`/`#29064` power (one = stray Assiomas, daughter's bike, much lower power), type-35 `#7092`, FE-C `#105` (the bike). |

## Other

| File | What it is |
|---|---|
| `A-assioma-watch-20260614.fit` | Assioma side recorded on the **watch** during Session A (dual-meter calibration reference, coast-notch sync). The watch method was later **dropped** in favour of multi-channel ANT+. |

## Sessions 2–3 (2026-06-17 → 06-19, `BIKE-SESSION-2.md` / `BIKE-SESSION-3.md`)

| File | Device | What it is |
|---|---|---|
| `G-crankL-ble-recon-20260617.jsonl` | Stages L crank 62144 | The crank's full BLE surface (services, characteristics, reads) captured before session 2 — what the ESP32 spoof replicates byte for byte (decisions 2026-06-17). |
| `session2-log-20260618-0713.txt` | ESP32 `/log` | Session 2 firmware log: the SB20 pairing to the spoof, the calibration write and the disconnect that PR #5 fixed. |
| `session2-confirm-assioma-20260618-0742.txt` | ESP32 `/log` | Session 2, the clean Assioma-only confirm (the SB20 reading spoofed power + cadence, crank-free). |
| `SHIFTER-probe-20260618.jsonl` | the bike (`0c46be60`) | First shifter probe (session 2) — the vendor characteristic that carries the handlebar buttons (`shifter-ble-protocol.md`). |
| `SHIFTER-probe-3-20260619-0838.jsonl` | the bike | Session 3's full six-button map: one-hot bitmap, stateless button events (BIKE-SESSION-3 §B). |

## Session 4 (2026-06-21, `sessions/session-04-enhanced-offset-and-brake-levers.md`)

| File | Device | What it is |
|---|---|---|
| `SHIFTER-probe-4-20260621-1000.jsonl` | the bike | 420 s probe with the app off: brakes produce no BLE signal (session 4 §B). |
| `SHIFTER-probe-4-buttons45-20260621-101727.jsonl` | the bike | The hidden buttons 4/5: each press a `0c46be60` frame `<u16 A><u16 btn-bitmap>`; 1≡4 / 2≡5 over BLE. |
| `SHIFTER-probe-4-gestures-20260621-102429.jsonl` | the bike | Chords, double-taps and the third/control buttons (L3 `0x04` …). |
| `SHIFTER-probe-4-holdtaps-20260621-103001.jsonl` | the bike | Hold vs tap: a hold is one continuous `01000400` stream, a tap a single frame. |
| `G-sb20-ftms-erg-20260621-0949.jsonl` | the bike (FTMS `0x1826`) | The first FTMS erg-acceptance capture (§C): Set Target Power accepted by the SB20. |
| `G-sb20-ftms-erg200-20260621-104341.jsonl` | the bike (FTMS) | The 200 W erg hold — the capture behind the flat-scale-vs-curve desk job. |
| `G-sb20-ftms-erg3way-20260621-110555.jsonl` | the bike (FTMS) | The three-way erg run (SB20 FTMS + Assioma + the ESP poll) that surfaced the power-topology question. |
| `CAL-assioma-ant-3way-20260621-110654.jsonl` | Assioma 17039 (ANT+) | The Assioma's ANT+ stream during the same three-way run (device 17039, period 8182, RF 57), from the file header; no document cites it directly. |
| `ESP-assioma-poll-erg3way-20260621-1106.txt` | ESP32 `/log` | The ESP32's Assioma poll during the three-way run (decisions 2026-06-21). |
| `G-garmin-assioma-session4-20260621.fit` | Garmin head unit | The Assioma side of session 4 as the Garmin recorded it (FIT UTC +10 h) — power-topology Phase 1. |

## Session 6 (2026-06-21, nRF sniffer, `sessions/session-06-sniff-and-power-topology.md`)

| File | Device | What it is |
|---|---|---|
| `SNIFF-sb20-app-20260621-1713.pcap` | app ↔ bike | Block S: the Stages app's erg conversation, captured from before the app connects — cleartext, the proprietary `0c46be` channel, not FTMS. |
| `SNIFF-sb20-zero-20260621-1717.pcap` | app ↔ bike | The zero-reset attempt (adverts only: the sniffer was started after the connection, the "sniff before connect" lesson). |
| `SNIFF-sb20-bleZero-20260621-1721.pcap` | app ↔ crank | The BLE zero-reset (~520 kB): offsets ≈ the ANT+ zero (902/951), so the offset is stable across radios. |
| `SNIFF-sb20-sweep-20260621-1726.pcap` | bike | Sweep #1 (short, aborted): 100/200/300 W. |
| `SNIFF-sb20-sweep2-20260621-1728.pcap` | bike | Sweep #2, the longer power sweep for topology Phase 2 (adverts only). |
| `G-garmin-assioma-earlier-20260621.fit` | Garmin | The earlier block (erg + zeros), Garmin activity 23325047298. |
| `G-garmin-assioma-sweepShort-20260621.fit` | Garmin | The clean sweep (UTC 07:59–08:02), activity 23325151488 — the Phase-2 reference. |

## Session 7 (2026-06-22, the comprehensive ride monitor, `sessions/session-07-comprehensive-monitor.md`)

| File | Device(s) | What it is |
|---|---|---|
| `RIDE-ant-ride-20260622.jsonl` | Stages 62144 + 4963, Assioma 17039, FE-C 105, HR 54880 (ANT+, one clock) | The whole qdomyos training ride on one clock (16.8 MB): the capture that **resolved the power topology** — SB20 = Stages crank 1:1, both ~11 % high vs the Assioma. |
| `RIDE-ble-sb20-ride-20260622.pcap` | bike (nRF sniffer) | The BLE sniff of the same ride following `E4:AA:5A:D6:0E:D4`, started before connect so the `CONNECT_IND` was caught (44 k ATT records). |
| `MANIFEST-ride-20260622.json` | `15_monitor_ride.py` | The ride manifest the monitor wrote: tag `ride-20260622`, start 07:12:52, 5,400 s, the device list and the two output files above (from its content; no document cites it). |
| `G-garmin-assioma-session7-20260622.fit` | Garmin | The Assioma side of the ride as recorded by the Garmin — folded in by PR #78 to confirm SB20 ≈ 1.11× Assioma. |
| `ASSIOMA-ble-cps-20260622.jsonl` | Assioma 17039 (BLE CPS) | The Assioma's BLE Cycling Power frames with the L/R balance field — grounds the balance forwarding; golden vectors in `test_ble_cps.py` (`supported-meters.md`). |

## Desk (2026-06-21)

| File | Device | What it is |
|---|---|---|
| `F-ftms-hwloop-server-20260621-0116.jsonl` | ESP32 FTMS server env ↔ laptop | The on-air FTMS hardware loop (F6): the Python codec talking to the ESP32 trainer-server seam — PASS (`ftms-protocol.md`). |

## Session 8 (2026-06-25, `sessions/session-08-sb20-spoof-calibration.md`)

| File | Device | What it is |
|---|---|---|
| `G-crank62144-ble-enhanced-0x10-20260625-0712.jsonl` | Stages L crank 62144 | G1 attempt 1: the real left crank had fallen asleep — the scan saw only `Stages 4963` and the bike. |
| `G-crank62144-ble-enhanced-0x10-20260625-0716.jsonl` | Stages L crank 62144 | G1: the real crank's `0x10` enhanced-offset reply — company id **442** + the manufacturer data encoding L 901 / R 951 (the fix for the app's spinning calibrate). |
| `G2-calibrate-pass-log-20260625-0758.txt` | ESP32 `/log` | G2 PASS: the Stages app's calibrate completing against the spoof with the real 442 + mfg-data reply. |

## Session 9 (2026-06-26, `sessions/session-09-zero-reset-onair-confirm.md`)

| File | Device | What it is |
|---|---|---|
| `G-zero-reset-onair-pass-20260626-0651.txt` | ESP32 `/log` | The zero-reset forwarded to the real Assioma on air: the app's calibrate → firmware `0x0C` → Assioma `200c01ffff` SUCCESS (a real zero, not cosmetic). |

## Session 13 (2026-07-26, `sessions/session-13-qz-obc-consumer-and-sb20-buttons.md`)

| File | Device | What it is |
|---|---|---|
| `SNIFF-session13-coldstart-qz-only.pcap` | bike (nRF sniffer) | Cold start with qz as the sole consumer — the *healthy*-telemetry half of the #288 A/B pair. |
| `SNIFF-session13-sb20-app-wake-20260726.pcap` | bike | Degraded telemetry, then the Stages app connecting and "waking" it (29,339 packets) — the other half of the #288 pair. |
| `SNIFF-session13-buttons.pcap` | bike (`0c46be60`) | The handlebar-button frames: the `0x01` held-stream, the rare `0x03` commit and the `0x04`/`0x08` terminators behind qz PR #4850. |
| `SNIFF-session13-phaseD.pcap` | bike | Phase D of the session (see the session doc §Captures). |
| `session13-qz-g1-buttons.log.gz` | qz (`--log`) | The authoritative button-behaviour record for G1: which press produced which qz action, including the dropped LEFT-down. |

## Perf soaks

The `perf/soak-*.jsonl` runs behind [`perf-results.md`](../perf-results.md) live in `../perf/`, not here.
