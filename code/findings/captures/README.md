# Captures index

The committed, lossless record of every Phase-0 capture. JSONL is canonical (never edit a
capture; produce a new analysis). Conclusions drawn from these live in
`../phase-0-report.md` (synthesis) and `../decisions.md` (chronology).

> Naming drifted across sessions; this table is the authoritative inventory. One file is
> **misnamed** (flagged below).

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
