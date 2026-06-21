# 🚴 Bike session 6 — sniff the app↔SB20 erg conversation + resolve the power topology

**Status: 🟡 PLANNED** (prepared 2026-06-21, desk) · tracked in [`sessions/README.md`](README.md) · run via
[`sessions/PLAYBOOK.md`](PLAYBOOK.md). Runs **on the bike laptop** (the desk machine where this was built is
elsewhere — the nRF dongle moves to the laptop). Designed to be **driven by a Claude session, hands-free**:
the rider only sets up, pedals, and works the Stages app when asked; Claude runs every capture + the erg drive
and records actuals into this doc.

## Why this session — two open questions, one rig

1. **What does a *legitimate* erg controller actually write to the SB20, and does it bond/encrypt?**
   We can now **passively sniff** with the nRF dongle ([`code/findings/nrf-sniffer.md`](../code/findings/nrf-sniffer.md)).
   Watching the **Stages app drive the SB20 in erg** tells us: the exact FTMS Control-Point sequence a trusted
   client uses (Request Control → Set Target Power values, any cadence/sim writes), **whether the link is
   encrypted/bonded** (if so, that explains any `control-not-permitted` to *us* and means we'd need to bond too),
   and whether the SB20 treats the app differently from our third-party control. **This is the headline** — it
   directly de-risks our FTMS erg feature. *(Block S.)*
2. **Power topology — single- vs dual-sided, which meter the SB20 ergs off, and the true scale.** Session 4
   found the SB20's erg power reads ~½–⅔ of the Assioma and the cause is still open
   ([`sb20-power-topology.md`](../code/findings/sb20-power-topology.md) Phase 1 refuted simple single-sided →
   ~1.3× under-read). Resolve it with the **simultaneous multi-device capture** that was always the Phase-2 plan.
   *(Block T.)*

The nRF sniffer is **passive**, so it runs through **both** blocks: in Block S it follows the *app↔SB20*
connection; in Block T it follows *our↔SB20* connection — giving a direct **"does the app's erg write look like
ours?"** comparison.

---

## Pre-stage — laptop setup (do BEFORE the rider is on the bike)

**Rider/owner, once:**
- [ ] **Install Wireshark** on the laptop: `winget install WiresharkFoundation.Wireshark` (used to *open/analyse*
  the `.pcap`; the capture itself is headless via `sniff_ble.py`).
- [ ] **Move the nRF dongle** from the desk machine to the laptop and plug it in. It's **already flashed** with the
  sniffer firmware — nothing to re-flash. It will enumerate as a COM port (USB PID `522A`).

**Claude (laptop), at the desk-derisk stage — verify, don't discover on the bike:**
- [ ] `git fetch origin && git status` — sync to `origin/main` (this session's tooling must be merged first).
- [ ] **Stage SnifferAPI** (the headless capture borrows Nordic's library from the matched-version extcap). The
  makerdiary clone carries it — copy it where both Wireshark and `sniff_ble.py` look:
  ```powershell
  # if the makerdiary repo isn't on the laptop yet:
  #   git clone https://github.com/makerdiary/nrf52840-mdk-usb-dongle C:\repos\nrf52840-mdk-usb-dongle
  Copy-Item -Recurse -Force C:\repos\nrf52840-mdk-usb-dongle\tools\ble_sniffer\extcap\* `
      "$env:APPDATA\Wireshark\extcap\"
  ```
  *(Or skip the copy and pass `--extcap-dir C:\repos\nrf52840-mdk-usb-dongle\tools\ble_sniffer\extcap` to
  `sniff_ble.py`.)*
- [ ] **pyserial** in the interpreter that runs the script: `pip install pyserial psutil` (a `py -3` / venv that
  has it is fine — the desk used `pyserial 3.5`).
- [ ] **Smoke test (the go/no-go for Block S):**
  ```bash
  python code/scripts/sniff_ble.py --scan-only --duration 12
  ```
  Expect: `sniffer on COMxx …` and a list of advertisers. **Confirm the SB20 appears** (power it on first) — note
  its **advertising MAC** (it may differ from the connect-time `E4:AA:5A:D6:0E:D4`; identify by the `Stages …`
  name). That MAC is what Block S follows.

> **No-hang note (the rider is not babysitting this):** `sniff_ble.py` and every capture here are **time-bounded**
> (`--duration`, hard `--max-duration` ceiling) and shut the dongle down cleanly on exit. Nothing blocks waiting
> for a device that never shows — a missing target degrades to "advertising-only" with a warning, then ends.

---

## Block S — sniff the Stages-app ↔ SB20 erg conversation ⭐ (the headline; do first)

**Rig:** Stages app on the phone, SB20 on, dongle in the laptop. **We do NOT connect to the SB20 in this block**
(so the sniffer follows the *app's* connection, not ours).

1. **Confirm the follow address** (from the smoke test, or re-run `--scan-only`). Call it `<SB20_ADV>`.
2. **Start the sniff BEFORE the app connects** (so the connection setup — and any pairing/bonding — is captured):
   ```bash
   python code/scripts/sniff_ble.py --device <SB20_ADV> --duration 420 \
       --output code/findings/captures/SNIFF-sb20-app-$(date +%Y%m%d-%H%M).pcap
   ```
   Wait for `FOLLOWING <SB20_ADV>` on stderr.
3. **Rider:** open the **Stages app**, connect to the SB20, **start a workout / set an erg target** (a couple of
   different target watts if the app allows), and **pedal** ~2–3 min. Change the target once or twice.
4. Capture ends on `--duration`. **Commit** the `.pcap` to `findings/captures/`.
5. **Analyse** (Claude, desk-side or live): open in Wireshark, filter to the **FTMS Control Point `0x2AD9`** and
   **Indoor Bike Data `0x2AD2`**. Record:
   - **Pass criterion:** the app's **Request Control (`0x00`)** + **Set Target Power (`0x05` + sint16 LE watts)**
     writes are visible **in cleartext** → we can read exactly what a trusted controller sends and confirm it
     matches our codec. ✅
   - **The bonding fork:** if the GATT writes are **encrypted** (sniffer shows ciphertext / "encrypted" link) →
     **finding:** the app bonds. That explains any `control-not-permitted` to our third-party control and means
     erg-from-us may require pairing. We captured from before connect, so the **pairing is in the file** — note
     the security mode; the extcap can take an LTK/passkey if we can obtain one. ⚠️

> **Why this matters even if encrypted:** "the app bonds, we don't" is itself the answer to whether our own erg
> control will ever be accepted as-is. Don't treat encryption as a failed session — it's a decisive result.

---

## Block T — power-topology Phase 2: simultaneous multi-device capture (we drive erg)

**Rig:** as session 4 — we connect to the SB20 + meters. Keep the **sniffer running** following `<SB20_ADV>` so we
also record *our* erg writes on-air. (See [`traffic-observability.md`](../code/findings/traffic-observability.md)
§"Phase 2 run-sheet" for the full command set.)

1. **BLE half** (laptop, Windows) — the SB20 (FTMS + shifter) + each meter's BLE CPS, one clock:
   ```bash
   python code/scripts/capture_ble_multi.py --duration 600 \
       --device sb20:all:<SB20_ADV> \
       --device stagesL:cps:<stages-62144-ble-addr> \
       --device assiomaL:cps:<assioma-17039-ble-addr> \
       --output code/findings/captures/MULTI-ble-$(date +%Y%m%d-%H%M).jsonl
   ```
   *(Scan addresses once with `sniff_ble.py --scan-only` or `06_capture_ble.py --adv-only`.)*
2. **ANT half** (the stick) — all four ANT ids on one clock. **First apply the [Errno 13] permission fix**
   (`traffic-observability.md` §"ANT+ stick on WSL", or run ANT+ Windows-native):
   ```bash
   python code/scripts/07_capture_multi.py --duration 600 \
       --meter stages:62144 --meter stagesR:4963 \
       --meter assiomaL:17039 --meter assiomaR:22428 \
       --output code/findings/captures/MULTI-ant-$(date +%Y%m%d-%H%M).jsonl
   ```
3. **Drive erg + hold steady** so each target is a clean bin: `capture_ftms.py --erg` / the Ride Director.
   **Note the wall-clock at each hold** (the §C trick that let the FIT align). Targets e.g. 100 / 150 / 200 W.
4. **Cheap decisive diagnostic — pull the real Stages **L**-crank battery** during a steady hold: if the SB20
   **still** gets power → it's **ESP/Assioma-fed**; if power **drops** → it was on the **real Stages L crank**
   (single-sided). One action settles the topology that Phase 1 could only infer.
5. **Reconcile** (desk): `13_build_sqlite.py --reconcile --basis iso` per pair (SB20 vs Assioma-total vs
   Assioma-L vs Stages) → the per-hold ratio resolves single-vs-dual-sided + the true scale.

---

## Capture checklist (commit these as the canonical record)
- [ ] `SNIFF-sb20-app-*.pcap` — Block S (app driving erg; the headline)
- [ ] `MULTI-ble-*.jsonl` + `MULTI-ant-*.jsonl` — Block T (multi-device, both transports)
- [ ] *(optional)* `SNIFF-sb20-us-*.pcap` — the sniffer following *our* erg drive in Block T (for the app-vs-us compare)
- [ ] Note the **battery-pull** result (step T4) — it's a one-line topology answer

## Risks / front-loaded gates
- **Encryption (Block S):** the main value risk — handled by the decision tree above; capture-from-before-connect
  is mandatory so pairing is in the file.
- **SB20 advertising address ≠ `E4:AA:5A:D6:0E:D4`:** likely (rotating/random adv address). **Always scan-first**
  and follow the address the sniffer actually reports for the `Stages …` name. (If it uses a resolvable-private
  address that rotates, the sniffer's "Follow IRK" handles it — note it and we adapt.)
- **ANT+ `[Errno 13]` (Block T):** apply the permission fix at the desk *before* the ride; verify with the 5 s
  smoke capture in `traffic-observability.md`. BLE half + the sniffer don't depend on it.
- **One connection at a time:** the sniffer follows a single connection — run Block S (app) and Block T (us)
  **sequentially**, not overlapping the two controllers.

---

## Cold-start prompt for the bike laptop

> Paste this into a fresh Claude Code session on the **bike laptop** to run this session. (The full prompt is also
> in the chat that generated this doc.)

```
We're running bike SESSION 6 on this laptop — sniff the Stages-app↔SB20 erg conversation with the
nRF dongle, plus the power-topology Phase-2 multi-device capture. The plan + run-sheet is
sessions/session-06-sniff-and-power-topology.md — read it, then drive it per sessions/PLAYBOOK.md:
one step at a time, record each actual (pass/fail + observed bytes/values) back INTO that doc, and
keep its Status header current.

First sync: `git fetch origin && git status` (work off origin/main). Then do the Pre-stage section
(stage SnifferAPI from the makerdiary clone, pip install pyserial, and run
`python code/scripts/sniff_ble.py --scan-only --duration 12` to confirm the dongle + the SB20's
advertising MAC) BEFORE the rider is on the bike. The nRF dongle is already flashed (sniffer
firmware, USB PID 522A) — do not re-flash it. Everything is time-bounded; don't let anything hang.
I (the rider) will pedal and work the Stages app when you ask. Start with Block S.
```
