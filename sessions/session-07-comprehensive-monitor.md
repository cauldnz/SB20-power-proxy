# 🚴 Bike session 7 — comprehensive passive ride monitor (qdomyos training ride)

**Status: 🟢 READY** — tooling built + smoke-tested on the bike laptop **2026-06-21 night**; runs the
morning of **2026-06-22**. Tracked in [`README.md`](README.md); run via [`PLAYBOOK.md`](PLAYBOOK.md).
Bike-laptop, **Claude-driven**: the rider powers on, connects the apps on cue, pedals; Claude runs every
capture + the checks and records actuals here.

## Goal
Passively capture **everything** during a normal qdomyos-zwift training ride (qdomyos drives the SB20 over
**FTMS**; the **Stages app** is also connected; Peloton video for entertainment) and parse it into the
SQLite analysis index. No behaviour change for the rider — pure monitoring.

## Capture matrix (what records what)
| Radio | Tool | Captures |
|---|---|---|
| **ANT+** (1 stick) | `07_capture_multi.py`, 5 channels | Assioma `17039` · Stages crank L `62144` · crank R `4963` · **HR** (wildcard, type `0x78`) · the bike's **FE-C** output (wildcard, type `0x11`) |
| **nRF BLE** | `sniff_ble.py` follow `e4:aa:5a:d6:0e:d4` | the **qdomyos↔SB20 FTMS** conversation (the one BLE-only prize) |
| both | `15_monitor_ride.py` | launches + supervises both, heartbeats health (`growing`/`STALE`/`dead`), clean stop, writes a `MANIFEST` |

**Hard constraint:** one nRF follows **one** BLE connection. The SB20↔crank and Stages-app↔SB20 BLE links
are *not* sniffed — their power is captured on **ANT+** instead. Plus **`16_scan_ant.py`** — a one-stick
RX-scan that lists *every* ANT+ device (id + type), run pre-ride for **ID confirmation + an "anything blowing
around" catch-all**. (Further stretch: scan *during* the ride on the 2nd stick; the ESP as a 2nd BLE eye.)

## Pre-flight — DESK, before the bike ✅ (verified 2026-06-21 night; re-confirm in the morning)
- **ANT sticks claim:** both `0FCF:1008` flipped to **WinUSB** via Zadig; openant opens + runs a node OK
  (libusb via the pip `libusb-package` backend, wired into `07_capture_multi.py`).
- **ANT capture mechanics:** `07_capture_multi.py` opens all 5 channels + the **threading-timer** duration
  fires + clean `session_end` on Windows (no `SIGALRM`). 0 broadcasts with the bike off (expected).
- **nRF scan:** `sniff_ble.py --scan-only` → COM8, ~28 advertisers (incl. the ESP spoof `Stages 62144`).
- **Orchestrator:** `15_monitor_ride.py` real-smoke launched both captures, heartbeat showed nRF `growing`
  + ANT `STALE` (correctly — nothing broadcasting), clean stop + manifest.

Re-confirm command (no bike needed): `python scripts/sniff_ble.py --scan-only --duration 8 --extcap-dir
C:\repos\nrf52840-mdk-usb-dongle\tools\ble_sniffer\extcap`

## THE RUN — one step at a time (Claude drives; **start the capture BEFORE connecting**)
> Why the order: the nRF can only *follow* a connection if it catches the `CONNECT_IND`. Session 6 lost the
> crank data by sniffing after the link was up. Launch the monitor first; connect second.

1. **Rider:** power on the **SB20** (BTLE power mode, cranks paired), wake the **Assioma** (a few cranks),
   put on the **HR strap**. **Do NOT open qdomyos / the Stages app yet.**
2. **Claude:** confirm the SB20's advertising MAC — `sniff_ble.py --scan-only --duration 10` (expect
   `e4:aa:5a:d6:0e:d4`; use whatever it actually advertises).
2b. **Claude — ANT+ ID confirmation (the ANT-side smoke test):** with the bike + meters awake,
   `python scripts/16_scan_ant.py --duration 30` → confirms **Assioma `17039`**, the **crank IDs**, the
   **HR id**, and shows *anything else* on ANT+. **If a crank ID differs from `62144`/`4963`, update the
   `--ant` flag in step 3 to match.** (Confirms we'll receive the right things before the real capture.)
3. **Claude:** launch the monitor (before any app connects):
   ```
   python scripts/15_monitor_ride.py --sb20 e4:aa:5a:d6:0e:d4 \
       --ant assioma:17039,stagesL:62144,stagesR:4963 --hr hrm --fec \
       --duration 4200 --tag ride-20260622
   ```
   Wait for `MONITOR START` + the first heartbeat.
4. **Claude:** confirm **ANT `growing`** (Assioma/HR broadcasting) and **nRF `growing`** (adverts). If ANT is
   `STALE`, the meters aren't broadcasting → have the rider wake them (spin / re-seat HR).
5. **Rider (on cue):** open **qdomyos → connect to the SB20 FIRST** (the nRF locks onto the first connection
   to that address), **then** the **Stages app → connect**. Start the workout / Peloton.
6. **Claude — VERIFY the connection was caught** (the session-6 gate):
   ```
   & "C:\Program Files\Wireshark\tshark.exe" -r <RIDE-ble-sb20-...pcap> -Y btatt -c 5 -T fields -e btatt.opcode
   ```
   **Non-zero ATT ⇒ we caught the CONNECT_IND + the FTMS conversation.** If only adverts (no ATT after the
   rider connected), the sniff missed the connect → **stop the monitor, relaunch it, have the rider
   disconnect+reconnect qdomyos** so we re-catch the `CONNECT_IND`.
7. **Claude:** confirm both streams logging — heartbeat `growing`, and the ANT JSONL has `broadcast` records
   from `assioma`/`hrm` (+ `stagesL/R` if the cranks dual-broadcast ANT+).
8. **Leave the rider for the hour.** Captures self-stop at `--duration` (70 min). The `MANIFEST-*.json`
   records the outputs.
9. **Rider:** "done." **Claude:** stop the monitor if still running (Ctrl-C) → collate (below).

## Collate (after the ride)
```
python scripts/13_build_sqlite.py          # ANT JSONL  -> ant_broadcast + power_sample
python scripts/14_build_pcap_fit.py        # nRF pcap (+ any FIT) -> pcap_att / ble_control_point / fit_record
```
Then query the one index: `power_sample` (Assioma vs cranks vs SB20 FE-C, reconciled), `ble_control_point`
(qdomyos's FTMS Set-Target-Power writes), `pcap_att` (the full FTMS conversation), HR via the ANT JSONL.
**Commit** the canonical captures (`.pcap` + force-add the `.jsonl`) + a `decisions.md` entry; close this doc.

## Risks / notes
- **Do the cranks broadcast ANT+ in BTLE mode?** Unknown until step 4. If `stagesL/R` stay `STALE`, the crank
  power is BLE-only on the SB20↔crank link (which the nRF isn't following) → note the gap; the Assioma + the
  SB20 FE-C still give the topology picture.
- **Two controllers:** connect **qdomyos first** so the nRF locks the qdomyos↔SB20 link (not the Stages app's).
- **nRF pcap is large** (~1 MB/min of adverts → ~70 MB/hour). Fine on disk.
- **Clock:** anchor on the capture `iso_time` / the `MANIFEST`, not the filename (drift seen in session 6).
