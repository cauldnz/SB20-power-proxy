# 🚴 Bike session 6 — sniff the app↔SB20 erg conversation + resolve the power topology

**Status: ✅ DONE (2026-06-21)** — Block S answered (cleartext; erg over proprietary `0c46be`, **not FTMS**);
pcap+FIT→SQLite pipeline delivered + tested; power-topology Phase 2 **blocked** (adverts-only sniffs →
"sniff *before* connect" lesson). Started **17:11** local (UTC+10) · tracked
in [`sessions/README.md`](README.md) · run via [`sessions/PLAYBOOK.md`](PLAYBOOK.md). Runs **on the bike
laptop**. Designed to be **driven by a Claude session, hands-free**: the rider only sets up, pedals, and
works the Stages app when asked; Claude runs every capture + the erg drive and records actuals into this doc.

### Live run log (actuals)
- **17:11** — Session start (bike laptop). Synced to `origin/main` `5d84581` (sniffer + Phase-2 tooling merged).
- **17:11 · Pre-stage env check:** nRF sniffer dongle present = **COM8** (`VID_1915&PID_522A` → `autodetect_port`
  finds it); ESP32-C3 spoof on COM5. ⚠️ makerdiary clone + Wireshark-extcap dir + venv-win `pyserial` all
  absent → staging: clone makerdiary for SnifferAPI, `pip install pyserial psutil`, run via `--extcap-dir`
  (Wireshark not needed for the headless capture).
- **17:11 · Staged ✅:** makerdiary cloned → `…/tools/ble_sniffer/extcap/SnifferAPI` present; venv-win has
  `pyserial 3.5` + `psutil 7.2.2`. Run sniffer with `--extcap-dir C:\repos\nrf52840-mdk-usb-dongle\tools\ble_sniffer\extcap`.
- **17:12 · Sniffer smoke test ✅ (Block-S go/no-go PASSED):** `sniff_ble.py --scan-only --duration 12` →
  **`sniffer on COM8 @ 1000000 baud`**, SnifferAPI loaded, **29 advertisers**. ⚠️ The SB20 *bike*
  ("Stages Bike 0105") was **NOT advertising** — only the **ESP spoof "Stages 62144" @ `38:44:be:45:e9:a6`
  (−74 dBm)**. Need the SB20 powered on + Stages-app-disconnected to capture its advertising MAC (Block S
  follows that address). *(Re-scan pending SB20 power-on.)*
- **17:13 · SB20 MAC CONFIRMED** (rider moved downstairs/pedalling → SB20 in range; re-scan): **`<SB20_ADV>`
  = `e4:aa:5a:d6:0e:d4` (−51 dBm)** — same as the session-4 connect address, **not rotating** (no name in the
  12 s scan, but the address matches). Also seen: ESP spoof `38:44:be:45:e9:a6` + real L crank
  `e8:cf:d8:d9:3a:20` (both "Stages 62144"), real R crank `e3:25:39:38:92:71` ("Stages 4963"). ESP
  reconnected to the downstairs AP. **Pre-stage COMPLETE.**

## Block S — actuals
- **17:13 · START:** launched `sniff_ble.py --device e4:aa:5a:d6:0e:d4 --duration 420` →
  `findings/captures/SNIFF-sb20-app-20260621-1713.pcap` (capturing from *before* the app connects, to catch
  pairing/bonding). Waiting for `FOLLOWING` before cueing the rider to open the app.
- **17:13 · FOLLOWING confirmed** — `FOLLOWING e4:aa:5a:d6:0e:d4`, sniffer streaming (~4 k advert packets
  pre-connect). Cued rider: open Stages app → connect → erg workout (set 150 → 200 → 100 W, narrate each),
  pedal ~2–3 min. Will `TaskStop` when the rider's done, then analyse the `.pcap` (cleartext CP writes vs
  encrypted/bonded).
- **~17:15 · Rider erg workout (narrated = ground truth):** Stages app connected to the SB20; **Set Target
  Power 200 → 299 → 351 W**, pedalling ~70 rpm. In the `.pcap`, a *cleartext* app would show ATT writes to
  the FTMS CP (`0x2AD9`): `05 c800` (200), `05 2b01` (299), `05 5f01` (351). Absence/ciphertext ⇒ bonded.
- **~17:16 · OPPORTUNISTIC — rider does a ZERO/calibrate** (suspects calibration is off — a candidate cause
  of the ~30 % SB20-vs-Assioma gap). Sniffer still following → the **app's calibration conversation lands in
  the same `.pcap`** (high value: sessions 2–3 only saw the zero *spin* from our spoof side; this captures
  what the *app* actually sends). Rider stops pedalling, cranks still; to narrate start + the **app-reported
  offset + pass/spin**. *(Current Block-S `.pcap` had only ~30 s left → letting it end cleanly (erg writes
  already captured), then a **fresh sniff** for the zero so the calibrate is captured from before it starts.)*
- **17:17 · Block-S erg `.pcap` COMPLETE + intact** — `SNIFF-sb20-app-20260621-1713.pcap` (~1.0 MB, ~19.5 k
  packets, full erg conversation incl. the 200/299/351 writes). Script exited 1 only on a **cosmetic
  final-print `UnicodeEncodeError`** (`→` vs Windows cp1252, Python 3.14) — *after* a clean `doExit` + file
  close, so the capture is whole + dongle clean. **Fix forward:** `PYTHONIOENCODING=utf-8` (and/or ASCII-safe
  the prints — minor `sniff_ble.py` bug to file).
- **17:17 · ZERO capture START:** fresh sniff (utf-8) following `e4:aa:5a:d6:0e:d4` →
  `SNIFF-sb20-zero-20260621-1717.pcap`. Awaiting `FOLLOWING`, then cue the rider's calibrate.
- **17:18 · KEY RIG CLARIFICATION (owner) — the dongle is BLE-only:** Stages app **power-meter pairing =
  ANT+**, "Pair with Bluetooth" toggle **OFF**. ⟹ the **zero-reset was ANT+** → **LHS 902 / RHS 951** (LHS
  battery just replaced; ≈ session-4's 903/951, so the **zero-offset is stable — NOT the ~30% gap's cause**).
  ⚠️ **The nRF BLE dongle does NOT capture ANT+**, so the Assioma + Stages cranks + this zero are invisible
  to it — that traffic belongs to the **ANT+ stick** (Block T ANT half). Stopped the BLE zero-sniff (no
  calibrate on BLE). **OPEN, decides Block S's value:** is the app↔SB20 **erg** control **BLE (FTMS)** or
  **ANT+ (FE-C)**? If ANT+, the `SNIFF-sb20-app-1713.pcap` BLE connection is the **SB20↔crank** link (maybe
  the ESP), not the app's erg writes. → resolve via the owner + `.pcap` dissection.
- **17:19 · RESOLVED (owner):** the **Stages app ↔ SB20 bike is ALWAYS Bluetooth**; the ANT-vs-BT setting is
  only how the **power-meter cranks pair to the bike** (currently ANT+). ⟹ the app's erg control
  (200/299/351 Set-Target-Power) is **BLE FTMS → captured in `SNIFF-sb20-app-1713.pcap`** — **Block S got the
  right conversation; the bonding question is answerable from it** (subagent dissecting → pcap→SQLite). The
  **cranks↔SB20 (ANT+)** path (power + the 902/951 zero) is the **ANT+ stick's** domain (Block T). Corollary:
  the **SB20 ergs off the real Stages cranks over ANT+ (zeroed 902/951)** — the Phase-2 target.

### Tooling spun off (forking pattern)
- **17:19 · pcap→SQLite importer** — background subagent (`feat/pcap-sqlite`): build a `LINKTYPE_NORDIC_BLE`
  pcap dissector → SQLite (ATT writes, FTMS CP decode, encryption-state) + run it on `SNIFF-sb20-app-1713.pcap`
  → compact verdict (cleartext CP writes? bonded? what connection?) + commit the `.pcap`s + verify if the nRF
  can sniff ANT+. PR for review (not merge). *(Token-efficient: dissection in the subagent's context, not here.)*
  - **⛔ SUPERSEDED (desk, ~18:5x):** the pure-Python Nordic-BLE dissector stalled (no commits). **tshark
    (Wireshark CLI) dissects these pcaps natively** (BLE/L2CAP/ATT/GATT) → pivoted to a **`tshark→SQLite`**
    importer (below). Subagent stood down; its `feat/pcap-sqlite` worktree to be cleaned up.

## Block T (pivot to BTLE power-meter mode) — actuals
- **~17:20 · BTLE power-meter mode (owner):** ANT+ stick not plugged (single USB-A port), so to make the crank
  power + zero **BLE-sniffable** the rider switched the SB20's power-meter pairing to **BTLE** and **powered the
  ESP spoof OFF** (so the SB20 pairs the *real* cranks, not the spoof "Stages 62144"). Block T ran BLE-only.
- **~17:20 · BTLE ZERO — LHS 903 / RHS 951**, captured while following the SB20 →
  `SNIFF-sb20-bleZero-20260621-1721.pcap` (~520 kB). ≈ the ANT+ zero (902/951) ⟹ **offset stable across
  ANT+/BTLE**. ⭐ **Hypothesis the pcap tests:** is the zero/calibrate *always carried over ANT+* (hence
  invisible to the BLE sniff even in BTLE mode)? `bleZero` is the definitive check.
- **~17:2x · Sweep #1 (short, aborted)** — `SNIFF-sb20-sweep-20260621-1726.pcap` (~130 kB): 100/200/300 but
  intervals too short → redo.
- **~17:59 · Sweep #2 (CLEAN) ✅** — `SNIFF-sb20-sweep2-20260621-1728.pcap` (~354 kB): fresh sniff; rider ran
  **1 min each at 100 / 200 / 300 W** (Stages-app targets) with a **Garmin lap at each minute boundary** on a
  **new Garmin workout** → the crank-vs-Assioma reconcile data. *(Filename `…1728` is a ~30-min-early estimate —
  my running clock drifted; the Garmin clock [sweep ~17:59 local] is authoritative. Align by data/laps, not the
  filename. **Retro:** stamp from capture iso_time, not estimates — recurring session-4 lesson.)*

## Garmin FITs + preliminary topology finding
- **Two FITs preserved** in `findings/captures/` (Assioma reference; parsed via `fitparse`, never read raw):
  - `G-garmin-assioma-earlier-20260621.fit` (Garmin act. **23325047298**) — earlier block (erg + zeros),
    UTC 07:23–07:54, 1873 rec, lap0 = 26 min @ avg **198 W**. Pairs with `app-1713` + the zeros.
  - `G-garmin-assioma-sweepShort-20260621.fit` (act. **23325151488**) — **the CLEAN sweep**, UTC 07:59–08:02,
    3 × ~1-min laps **avg 103 / 180 / 274 W** at SB20 targets **100 / 200 / 300**. Pairs with `sweep2`.
- ⭐ **PRELIMINARY (FIT-only; to confirm via the sniffed-crank reconcile):** at targets 200/300 the Assioma read
  **180 / 274** — *below* target. The SB20 holds its **source (the real Stages cranks)** at the target ⟹ **the
  Stages cranks read ~10 % HIGH vs the Assioma** here.
  - ✅ consistent with the *old* "Stages reads 5–13 % high vs Assioma";
  - ⚠️ **opposite** session 4's "SB20 reads ~30 % *low* vs Assioma" → strong evidence **session 4 erged off a
    different / miscalibrated source**, not these freshly-zeroed BTLE cranks.

## Desk phase — parser + findings ✅
- **Pipeline DELIVERED (committed + tested):** `analysis/pcap_sqlite.py` (tshark → `pcap_att` raw spine +
  decoded CPS/FTMS power + `0c46be` control) and `analysis/fit_sqlite.py` (Garmin FIT → `fit_record`/`fit_lap`)
  load **both** the BLE sniffs *and* the FITs into one SQLite index (reconcile = a timestamp JOIN);
  `scripts/14_build_pcap_fit.py` is the turnkey build. **tshark** (Wireshark CLI) natively dissects the
  Nordic-BLE pcaps — it replaced the **stalled pure-Python subagent** (`feat/pcap-sqlite`, abandoned). 11 new
  hermetic tests; **full suite 322 green, ruff clean.**
- **(a) Block S — ANSWERED (`app-1713`):** app↔SB20 erg is **cleartext** (zero `btsmp`/bonding, 636 ATT ops)
  and driven over the **Stages-proprietary `0c46be`** char (handle `0x0039` = `0c46beb1`, write
  `02 00 <u16-LE> 00 00`), **not FTMS**. The `<u16>` is **not watts** — joined vs the overlapping "earlier"
  FIT it tracks power loosely (ratio 1.4–5.2×, median ~1.75, cadence-dependent) ⟹ an **app-side
  resistance/load setpoint** (vs our FTMS path where the SB20 closes the loop).
- **(b) "zero always ANT?" + (c) crank-vs-Assioma scale — BLOCKED.** `sweep2` / `bleZero` are **adverts-only**
  (4979 frames, no `CONNECT_IND` → the sniffer never followed the link → no ATT/CPS). **Not encryption.**
  **Lesson:** start every sniff **before** the device connects (power-cycle / toggle the link). The ~10 %-high
  topology preliminary (FIT-only; **conflicts with Phase 1**) stays unconfirmed → see `decisions.md`.
- **Next session:** the comprehensive passive-sniff — now with the parser working **and** the
  start-before-connect lesson baked in.

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
