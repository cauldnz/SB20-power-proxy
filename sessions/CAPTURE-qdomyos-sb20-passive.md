# 📡 Passive capture — a qdomyos-zwift Peloton ride on the SB20

**Opportunistic passive-monitoring run** (NOT a numbered ledger session — doesn't block session 10).
For a **second Claude on the bike laptop** to execute while the owner rides. The owner is doing a
qdomyos-zwift **Peloton-mode** ride on the real **Stages SB20**; two USB radios are plugged in (an
**ANT+ stick**, vendor `0fcf`; an **nRF52840 sniffer dongle**, PID `522A`). Just watch + record — the
data is a gift, not a test.

## ⚠️ The one rule: PASSIVE. Do not touch the ride.

- **Never write to the SB20.** No `--erg`, no control-point writes, no config changes. The bike is
  under qdomyos's control for a real workout — interfering could ruin the rider's session.
- The nRF sniffer is **fully passive** (it never connects — it listens to the air). Prefer it.
- `capture_ftms.py` **without `--erg`** only reads + subscribes (never writes) — but it does open a
  BLE *connection*. Only run it in the **pre-ride window** (SB20 on, qdomyos NOT yet connected) or
  **after** the ride, to avoid contending with qdomyos's link. Do NOT connect a second central to the
  SB20 mid-ride unless the sniffer alone clearly isn't seeing the GATT you need.

## 🎯 Why this is worth doing

Our head-unit already has an **on-device FTMS erg drive** that's twin-proven against a sim but has
**never spoken to the real SB20**. A qdomyos Peloton ride is a real controller driving the real bike's
resistance over BLE — so sniffing it captures **the exact protocol our erg feature needs to emit**:
which service/characteristic qdomyos writes, and the byte layout of its resistance/power commands.
Plus we get the SB20's own **Indoor Bike Data** encoding and whatever it broadcasts on **ANT+**.

Everything here reuses existing tools — see `code/findings/nrf-sniffer.md`, `ftms-protocol.md`,
`code/findings/README.md`. **Don't rebuild any capture path.** Run all commands from the repo root in
the bike-machine venv (`code/.venv/Scripts/python.exe`, or `python` if that's the project interpreter).

---

## 0 · Pre-flight (2 min, before the rider clips in)

```powershell
# a) sniffer dongle present + what it can see (also grabs the SB20's MAC for step 2)
python code/scripts/sniff_ble.py --scan-only --duration 12
#    -> note the SB20's MAC (advertised name usually contains "SB20"). Also note the Assioma
#       (ASSIOMA…) and the C3 spoof ("Stages 62145") if present.

# b) ANT+ stick present + advertisers
python code/scripts/16_scan_ant.py --duration 20
```

If the sniffer isn't auto-detected: it needs the Wireshark nRF Sniffer **extcap** staged (see
`nrf-sniffer.md` §"What's already installed"); pass `--extcap-dir` if it lives elsewhere. If the dongle
enumerated as the bootloader (PID `521F`) not the sniffer (`522A`), it needs the sniffer firmware
flashed — `nrf-sniffer.md` §"How it was flashed" (nrfutil DFU, **not** UF2 drag-drop).

Stamp a timestamp once for filenames: `$ts = Get-Date -Format yyyyMMdd-HHmm`

---

## 1 · SB20 FTMS GATT dump — PRE-RIDE (before qdomyos connects) · ~90 s

The clean, uncontended read of the bike's FTMS surface — the erg-drive question ("does the SB20 expose
FTMS `0x1826` + a Control Point + a Supported Power/Resistance Range?").

```powershell
python code/scripts/capture_ftms.py --name SB20 --duration 90 `
    --output code/findings/captures/QDZ-sb20-ftms-gatt-$ts.jsonl
```

Passive by default (no `--erg`): dumps the GATT table, reads static chars (FTMS Feature, Supported
Power/Resistance Range), subscribes to Indoor Bike Data + Fitness Machine Status, logs `raw_hex` on
everything. If it can't connect because qdomyos already grabbed the bike, skip it and rely on the
sniffer (§2) — or retry this after the ride.

---

## 2 · nRF sniff of the qdomyos ↔ SB20 conversation — DURING THE RIDE ⭐ (the main event)

Fully passive. Follows the SB20's link and captures **qdomyos's control-point writes** — the real
Peloton-mode resistance/erg protocol. Start it right after qdomyos connects and the rider is pedalling;
run it for a good chunk of the ride (e.g. 10 min = 600 s; extend/re-run as long as convenient).

```powershell
python code/scripts/sniff_ble.py --device <SB20-MAC-from-step-0> --duration 600 `
    --output code/findings/captures/QDZ-sniff-qdomyos-sb20-$ts.pcap
```

Time-bounded; shuts the dongle down cleanly on exit. Re-run for a second window if the ride is long or
if qdomyos changes modes (warm-up → intervals → cool-down) — mode changes are where the interesting
control writes live. **Note in your capture log roughly what the rider was doing** (steady, interval
on/off, resistance up/down) with timestamps, so the control writes can be correlated later.

---

## 3 · ANT+ passive capture — DURING (optional, parallel) · run alongside §2

Does the SB20 (or Assioma) broadcast over ANT+ (FE-C fitness-equipment / power)? Passive, read-only.

```powershell
python code/scripts/16_scan_ant.py --duration 600 `
    --output code/findings/captures/QDZ-ant-$ts.jsonl
```

---

## 4 · Wrap up (after the ride)

1. **Optional post-ride FTMS dump** if §1 was skipped (qdomyos now disconnected):
   `python code/scripts/capture_ftms.py --name SB20 --duration 90 --output code/findings/captures/QDZ-sb20-ftms-gatt-post-$ts.jsonl`
2. **Commit the raw captures** — they're the canonical lossless record (never edit them):
   ```powershell
   git add code/findings/captures/QDZ-*
   git commit -m "capture: passive qdomyos Peloton ride on the SB20 (FTMS GATT + BLE sniff + ANT+)"
   git push
   ```
3. **Leave analysis to the desk** — do NOT hand-parse the Nordic pcap. Later, on the main machine:
   open the `.pcap` in Wireshark, or run it through the tshark→SQLite indexer
   (`sb20proxy.analysis.pcap_sqlite`, per `nrf-sniffer.md` §Analyze). The JSONL captures summarize via
   `python code/scripts/04_summarize_capture.py <file>`.
4. **Jot a 3-line note** in the commit / a reply: how long each capture ran, what the rider was doing,
   anything that looked odd (dropouts, qdomyos reconnects).

## What we hope is in there

- **The Peloton-mode control protocol** — the service/char + byte layout qdomyos writes to set the
  SB20's resistance/target. Directly informs our on-device erg drive against the real bike (§14 phase 5).
- The **SB20's Indoor Bike Data** broadcast encoding (power/cadence/speed) and its **FTMS Feature +
  Supported Power/Resistance Range** (the clamp our erg client should respect).
- Whether the SB20 exposes anything on **ANT+**.
- Bonus: the SB20's proprietary/Stages control surface if qdomyos uses it instead of standard FTMS.
