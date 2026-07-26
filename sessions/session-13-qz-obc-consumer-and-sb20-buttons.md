# 🚴 Session 13 — qz as an OBC consumer + the SB20 handlebar buttons, on air

**Status:** ✅ **DONE (2026-07-26)** · **Outcome:** G1 PASSED — all six SB20 handlebar buttons drive qz,
one action per press; found+fixed the defect that silently dropped ~2/3 of presses (qz keyed on a `0x03`
commit frame the bike sends only ~6% of the time). G2 BLOCKED by a discovery-ordering conflict (now
understood, issue filed). · **Bike:** Stages SB20 (`E4:AA:5A:D6:0E:D4`) ·
**Board:** C3-OLED `sb20proxy.local` / **192.168.1.165** · **Firmware branch:** `main` ·
**nRF:** XIAO nRF52840 Sense `DE:F2:ED:C4:F3:FD` (`SB20 Bridge`) · **CYD:** `192.168.1.234` ·
**qz branch:** `bench/obc-listener+sb20-buttons` (local bench merge, X270) ·
**Budget: ~65 min, 6 must-dos** (3 SB20/qz + 3 nRF)

> **Design principle:** the desk already did everything that doesn't need your hands. Every gate below
> needs *a physical paddle press* or *a real SB20 link* — nothing else survived the bench.

> **⛔ Housekeeping — this doc deliberately breaks the "one open session at a time" rule.**
> Sessions 11 and 12 are both still 🟢 READY. This session **absorbs session 11's residual G2/G3**
> (its G1 is banked, and the desk re-proved it today). Session 12 (the erg workout ride) is
> untouched and remains the bigger, independent ride. Close 13, then 12.

**The rider's job is the hardware and the pedals. The agent runs every tool** (PLAYBOOK §"You run the
tooling"). **Tell the agent explicitly when you are AT THE BIKE** — it must never infer that.

---

## 1. Why now

The **consumer half** of OBC (qz's `obclistener`, our issue
[cagnulein/qdomyos-zwift#4791](https://github.com/cagnulein/qdomyos-zwift/issues/4791)) has never
touched air. As of today it has — at the desk. What's left needs a real SB20.

**⏱ There is a clock:** upstream's stale bot applied `wontfix` on 2026-07-24, and
`.github/stale.yml` closes 7 days after — **#4791 closes ~2026-07-31**. Any activity resets it.
An on-air result is the strongest possible activity.

### Banked already — do NOT re-test

| What | Where | Result |
|---|---|---|
| OBC transmit + decode on air (`/obc/press` → `obc_reader.py`) | session 11 **G1**, 2026-07-08 | ✅ PASS |
| `/obc/buttons.json` NVS round-trip on hardware | session 11 **P1** | ✅ PASS |
| OTA flash of the OBC build to `.165` | session 11 **P0** | ✅ PASS |

---

## 2. Desk pre-stage — 2026-07-26 (all verified today, X270)

| Check | Result |
|---|---|
| qz merged bench binary builds | ✅ `MAKE_EXIT=0`, 0 errors, Qt 5.15.18 |
| qz unit suite on the merged branch | ✅ 478 passed / **0 failed** / 109 skipped (587) |
| Both features in ONE binary | ✅ OBC svc, SB20 vendor char, both setting keys present |
| Board reachable + firmware current | ✅ `build_sha 689757831` = `6897578`; `main` HEAD is docs-only ahead → **no reflash** |
| C3 advertising | ✅ `OBC-SB20` @ `38:44:BE:45:E9:A6` |
| Board transmits OBC (G1 re-proof) | ✅ 4/4 decoded: `0x30 ERG Up`, `0x01 Shift Up`, `0x35 Lap`, `0x31 ERG Down` |
| **qz receives OBC** ⭐ | ✅ **NEW** — `obclistener` connected, subscribed, dispatched 4/4: `1→gear_up`, `2→gear_down`, `48→power_up`, `53→lap` |
| qz runs unprivileged | ✅ via bench-only `-allow-nonroot` (BLE central needs no root) |
| `obc_reader.py` on Linux | ✅ venv at `repos/sb20-power-proxy/.venv`, self-test PASS |
| Python capture tooling (`code/.venv`) | ✅ `sb20proxy`/`openant`/`bleak`/`pycycling` import; **460 passed, 3 skipped** *(note: `NEXT-BIKE-SESSION.md`'s "expect 121 passed" is stale)* |
| nRF sniffer | ✅ live scan, 30+ advertisers — see §4c for the Linux staging |
| Chromium (Web Bluetooth) | ✅ snap 150.0.7871.128 — **Firefox cannot do Web Bluetooth** |
| nRF firmware build on this box | ✅ `xiao-sense` builds (`NRF_BUILD_EXIT=0`); `xiao-sense-s340` **cannot** (no SoftDevice — §7b) |
| USB cables | ⚠️ **several are power-only** — C3 + CYD never enumerated; both are WiFi-controlled so it doesn't block anything |

**So the entire OBC *virtual* path is proven end-to-end, qz included.** Everything below is about
real paddles and real coex.

### Two corrections to the record (fold into `decisions.md` at close-out)

1. **`qz-upstream-contribution.md` §3 review-fix-2 is STALE.** It says the fork matches "service UUID
   only". Commit `bfb84695f` ("match OBC-prefixed advert name as well as the service UUID") landed
   after, and the branch now matches **both**.
2. **Which matcher fires depends on BlueZ cache state.** bleak saw `OBC-SB20` advertising only
   `0x1818` + `d445fe01` (no OBC UUID). qz/BlueZ saw the *same MAC* as `Stages 62145` with **7**
   UUIDs including `d273f680` — a cached service list from a prior connection. On a cold-cache
   machine the **name** match is likely the only one that works. Both paths are load-bearing.

---

## 3. Bring-list

Fresh **CR2032** + coin/screwdriver (real L crank) · the **X270** (qz binary + venv are on it) ·
phone with the **Stages Cycling** app · this doc open.

## 4. Restore-list — write these down before touching anything

| Thing | Restore to |
|---|---|
| Real pairing | `Stages 62144 (L)` : `4963 (R)` |
| Crank length | 165 mm |
| ANT+ zero-offset | L 903 / R 951 *(the spoof's BLE zero-offset is **0** — don't cross-fix)* |
| C3 OBC state at start | `devmode: ON`, `sink: off`, `buttons.json {"enabled":false,"actions":[1,2,5,1,2,6]}` |
| CYD (`192.168.1.234`) | `devmode: off` — leave it off unless G3-stretch runs |

## 4b. The nRF's only instrument is its serial console

The C3 exposes `/log` + `/stats` over WiFi. **The nRF has neither** — no WiFi, so no HTTP. Its
`Serial` output is the *only* view of its internal state (`[shifter] buttons set (sink=N)`,
`[bridge] CPS advertiser found`, the SB20 grab). Treat it as the nRF's `/log`.

**Cable warning (cost us ~15 min at the desk):** two of the USB cables tried were **power-only** —
the board powered up and advertised BLE, but never enumerated. Proven definitively by double-tapping
RST into the UF2 bootloader (which enumerates unconditionally) and still seeing nothing. Use a known
**data** cable; the nRF then appears as `2886:8045` on `/dev/ttyACM0`.

```bash
sudo chmod 666 /dev/ttyACM0        # dialout membership needs a re-login; this is the quick path
python3 -m serial.tools.miniterm /dev/ttyACM0 115200   # or: screen /dev/ttyACM0 115200
```

## 4c. Capture rig — dual-radio RESTORED (ANT stick found)

The ANT+ stick was **found and connected** (`0fcf:1008` Dynastream ANT USBStick2), so the PLAYBOOK's
standing dual-radio rule holds after all. *(An earlier revision of this doc recorded a BLE-only
deviation — superseded.)*

- **⚠️ ONE STEP OUTSTANDING:** the stick enumerates but is `crw-rw-r-- root:root`, so `openant`
  cannot claim it. Install the udev rule from `CLAUDE.md`, then **re-plug**:
  ```bash
  sudo tee /etc/udev/rules.d/42-ant-usb-sticks.rules >/dev/null <<'RULE'
  SUBSYSTEM=="usb", ATTRS{idVendor}=="0fcf", MODE="0666"
  RULE
  sudo udevadm control --reload-rules && sudo udevadm trigger
  ```
  **Until this is done the ANT radio is unusable** — and per the session-9 hard rule, a live test
  capture on EACH radio must pass before the rider is at the bike.

**Sniffer — staged + verified live on Linux 2026-07-26** (dongle already carried the v4.1.1 sniffer
firmware, PID `522a`; scan returned 30+ advertisers incl. the XIAO at −45 dBm):

```bash
cd /home/cauldnz-x270/repos/sb20-power-proxy/code
.venv/bin/python scripts/sniff_ble.py --scan-only --duration 15 \
  --extcap-dir /home/cauldnz-x270/repos/nrf52840-mdk-usb-dongle/tools/ble_sniffer/extcap
```

Two Linux gotchas that cost time and will recur:
1. **`sniff_ble.py`'s default extcap search path is Windows-only** (`C:\Program Files/Wireshark/extcap`),
   so `--extcap-dir` is **mandatory** here. *(Worth a portability fix in the script.)*
2. **`psutil` is a hard SnifferAPI dependency** and is not in `code/pyproject.toml`'s extras — it had
   to be installed separately. *(Worth adding to the `ble` extra.)*

`SnifferAPI` comes from the **matched-pair** makerdiary checkout (never Nordic's, which would mismatch
the firmware): `github.com/makerdiary/nrf52840-mdk-usb-dongle` → `tools/ble_sniffer/extcap`, cloned to
`/home/cauldnz-x270/repos/nrf52840-mdk-usb-dongle`.

## 5. Standing rules (PLAYBOOK pre-flight)

- **BLE-only capture this session** (see §4c); start the nRF sniff **before anything connects**
  (it can only follow a link whose `CONNECT_IND` it caught), and confirm within ~1 min that the
  followed device's **adverts stop** — if packet counts keep coming from adverts alone, you are
  following the wrong personality (the SB20 presents several) and must re-target *now*.
- **JSON POSTs need `-H "Content-Type: application/json"`** (the #239 lesson).
- **A fresh board boot needs ~25 s** before HTTP rebinds. **The C3 never roams** — power-cycle to
  re-associate.
- **Coex watch every gate:** `/stats` free-heap stable and `reboot_count` / `reset_reason` unchanged.

---

## 6. The gates — in information order

### G0 · Board + bike alive — **must-do**, ~3 min

- **Goal:** cheapest possible go/no-go before anything expensive.
- **Action (agent):** `curl http://192.168.1.165/obc` and `/stats`; scan BLE for the SB20.
- **Expected / PASS:** `/obc` renders; heap ≳100 KB; `Stages Bike 0105` (`E4:AA:5A:D6:0E:D4`) visible.
- **FAIL → fallback:** board unreachable ⟹ power-cycle, wait 25 s (it never roams). SB20 invisible
  ⟹ wake it (pedal a turn), check the CR2032.

### G1 · Real SB20 paddle → **native qz** (no ESP32 in the path) — **must-do** ⭐, ~10 min

This is the half that has *never* been tested at all — not even at the desk, because it needs a real
SB20 emitting on `0c46be60`.

- **Goal:** prove qz reads the SB20's own handlebar buttons directly.
- **Setup (agent):** `sb20_buttons_enabled=true` (default), **`obc_listener_enabled=false`** —
  see Risk R1, they double-fire if both are on. Restart qz. **Do not use `-no-gui`** (Risk R2).
- **Action (rider):** pedal so the bike is awake; press each of the 6 buttons in turn, narrating.
- **Expected / PASS** (from the merged `ftmsbike.cpp`, one action per press):

  | Button | Bitmask | qz action |
  |---|---|---|
  | LEFT up | `0x0001` | target power **+** |
  | LEFT down | `0x0002` | target power **−** |
  | LEFT 3rd | `0x0004` | **gear down** |
  | RIGHT up | `0x0008` | peloton offset **+** |
  | RIGHT down | `0x0010` | peloton offset **−** |
  | RIGHT 3rd | `0x0020` | **gear up** |

  Agent greps `0c46be60` in the qz log for `03 00 <bit> <bit>` commit frames; rider confirms the
  tile moved. RIGHT up/down need a **Peloton Power Zone** workout loaded to be observable.
- **FAIL → fallback:** only `01` frames and no `03` ⟹ that's the documented alternate path; record
  the raw bytes and treat as a finding, don't fix on the rider's clock. Nothing on `0c46be60` at all
  ⟹ check qz subscribed the vendor service (grep `0c46be5f`).

### G2 · Real paddle → C3 shifter-sink → OBC → **qz** — **must-do** ⭐⭐, ~12 min

Session 11's G2, with qz as the consumer instead of `obc_reader.py`. **The dual-role
central+peripheral coex on a single-core C3 is the known risk.**

- **Goal:** the full product path — bike's own buttons driving qz through our box.
- **Setup (agent):** `obc_listener_enabled=true`, **`sb20_buttons_enabled=false`** (Risk R1);
  restart qz. Then enable the sink **live, no reboot**:
  ```bash
  curl -X POST http://192.168.1.165/obc/buttons.json \
    -H "Content-Type: application/json" \
    -d '{"enabled":true,"actions":[1,2,5,1,2,6]}'
  ```
  Confirm `curl http://192.168.1.165/log` shows **`[shifter] SB20 connected`**.
- **Action (rider):** press each paddle, narrating.
- **Expected / PASS** — the **live** `Sb20ButtonMap` (one OBC id per press; *not* the legacy
  multi-action `ObcSb20Map`, which is test-only):

  | Button | OBC id | qz log line |
  |---|---|---|
  | LEFT up / RIGHT up | `0x01` Shift Up | `obclistener: button 1 -> gear_up` |
  | LEFT down / RIGHT down | `0x02` Shift Down | `obclistener: button 2 -> gear_down` |
  | LEFT 3rd | `0x35` Lap | `obclistener: button 53 -> lap` |
  | RIGHT 3rd | `0x16` Menu | **no qz action** — `0x16` is unmapped in `obclistener` (expected) |

- **⚠️ Record:** press→action latency, any missed/duplicated presses, heap low-water.
- **FAIL → fallback:** board resets or the SB20 link drops with OBC-peripheral + WiFi up ⟹ **that is
  the single-core C3 coex ceiling and a real result, not a failure of the session.** Capture
  `/stats` reset-reason + the `/log` tail and stop the gate.

### G3 · Live re-bind (session 11 G3) — optional, ~5 min

- **Goal:** prove the config path end-to-end with qz as the observer.
- **Action (agent):** rebind RIGHT 3rd (slot 5) from `menu` to **`erg_down`** (index 4):
  ```bash
  curl -X POST http://192.168.1.165/obc/buttons.json \
    -H "Content-Type: application/json" \
    -d '{"enabled":true,"actions":[1,2,5,1,2,4]}'
  ```
- **Expected / PASS:** GET reflects the new `actions`; pressing RIGHT 3rd now yields
  `obclistener: button 49 -> power_down` (previously nothing).
- **FAIL → fallback:** GET doesn't reflect ⟹ the `Content-Type` header (#239). Reflects but no
  action ⟹ the sink didn't re-apply live; reboot and retry once.

### N0 · nRF web SPA over Web Bluetooth — **must-do**, ~8 min

The nRF has **no WiFi** — its UI is the shared SPA talking **Web Bluetooth**. If this is good, the
nRF becomes a serious target device (that's the point of the gate).

> **✅ ALREADY PROVEN ON iOS (2026-07-26, pre-ride).** Bluefy on iPhone connected to `SB20 Bridge`
> from the GitHub Pages build. Getting there fixed **three real bugs** — all of which would have
> burned rider time (see §7 R11-R13). On the bike this gate is now a re-confirm plus the parts that
> need the SB20 present, not a first attempt.

- **Goal:** prove the SPA drives the nRF end-to-end from this laptop.
- **Setup (agent, desk):** Chromium is installed (snap 150.0.7871.128 — **Firefox cannot do Web
  Bluetooth**, it was the only browser here until today). Serve the canonical file over **localhost**
  (a secure context; `file:` is not reliably one for Web Bluetooth):
  ```bash
  cd /home/cauldnz-x270/repos/sb20-power-proxy/web && python3 -m http.server 8899
  chromium http://localhost:8899/index.html
  ```
  `pickTransport()` sees a non-device origin → selects **BleTransport**.
- **Action (rider):** in the Chromium chooser, pick **`SB20 Bridge`** (`DE:F2:ED:C4:F3:FD`).
- **Expected / PASS:** connects; Status notifies ~2 Hz; Config reads back (scale/offset/mode);
  the UI renders live values.
- **FAIL → fallback:** no chooser entry ⟹ Chromium needs BLE permission / the board is in a
  connection already (it is a 4-central design — see R8). Connects then stalls ⟹ capture the
  console and treat as a finding; do not debug the SPA on the rider's clock.

### N1 · nRF as an OBC proxy (SB20 paddles → nRF → OBC) — **must-do** ⭐, ~10 min

**No reflash needed** — I initially thought it did. `-D OBC_SINK_SHIFTER=1` only sets the *first-boot
default*; the **Buttons char `0009`** write handler does `g_sinkShifter = b.enabled` and starts/stops
the SB20 central **in place** (`main.cpp:127,178-188`), persisted to LittleFS `/buttons.bin`. That's
the BLE twin of the ESP32's `/obc/buttons.json`.

- **Goal:** the nRF re-presenting the SB20's handlebar buttons as OBC.
- **Action (agent):** in the SPA's **"SB20 handlebar buttons"** card, tick *enabled* and keep the
  default binding `[1,2,5,1,2,6]`. **Rider** presses each paddle.
- **Expected / PASS:** the same `Sb20ButtonMap` ids as G2 (`0x01`/`0x02`/`0x35`/`0x16`), observed on
  the OBC Button-State char. **Watch for the two local-only actions** `bias_up`(8)/`bias_down`(9) —
  those nudge the nRF's own erg target ±10 W and produce **nothing** on OBC by design.
- **FAIL → fallback:** sink won't enable ⟹ read the Buttons char back; if `enabled` doesn't stick,
  that's a LittleFS/persistence finding. SB20 never grabbed ⟹ the nRF's 4th central couldn't
  connect (R8).

### N2 · Make qz discover the nRF — **must-do** ⭐⭐, ~7 min

**This gate exists because the nRF is currently INVISIBLE to qz** (see R9). It's also the cleanest
independent validation of the fork's name-prefix matcher.

- **Goal:** qz binds the **nRF** (not the C3) as its OBC controller.
- **Action (agent):** over Web Bluetooth, write **Config offset 25** (`broadcast identity name`,
  `u8[19]`) = **`OBC-Bridge`**. `main.cpp:645-648` applies it live (stop/start advertising) —
  **corrector mode only; in spoof mode the name is pinned to the Stages crank.** Then power the C3
  **down** (so it can't win the race), restart qz, let it bind.
- **Expected / PASS:** qz logs `obclistener: connecting to OBC controller OBC-Bridge`, then
  `subscribed to OBC Button-State`; a paddle press yields `obclistener: button 1 -> gear_up`.
  **This proves the `OBC-` name match is load-bearing** — the nRF advertises no OBC service UUID at
  all, so the UUID path *cannot* fire here.
- **FAIL → fallback:** name doesn't change ⟹ check `spoof` is 0 (b3 of the Config flags byte); name
  >19 bytes is rejected. Advert changes but qz doesn't bind ⟹ that's a real matcher finding for the
  upstream PR — capture the bleak scan showing the advert.
- **Restore:** set `outName` back to **`SB20 Bridge`**.

### G4 · Stretch — only if fresh and time remains

- **S1 · The double-fire, deliberately.** Set **both** `sb20_buttons_enabled` and
  `obc_listener_enabled` true with the sink on, press LEFT up once. Expect **two** actions
  (`target_power+` natively *and* `gear_up` via OBC). Confirms Risk R1 is real and gives the
  upstream PR a concrete design question.
- **S2 · CYD as the OBC host.** `POST http://192.168.1.234/obc/devmode/on`, then restart qz with
  **both** boards advertising `OBC-SB20`. qz `break`s on the first match, so the binding is
  **non-deterministic** — record which it picks. ⚠️ CYD health is marginal (see R4).

---

## 7. Risks & landmines — designed around, not discovered at the bike

- **R1 · Double-fire (NEW, desk-found).** With both features on and the sink relaying, one physical
  press fires **twice** in qz — natively *and* via OBC — with *different* actions. Neither branch
  can show this alone; the merge created it. **Mitigation: one path at a time** (G1 vs G2).
- **R2 · `-no-gui` silently disables everything.** It sets `forceQml=false`, so `homeform` is never
  constructed and **both** dispatch paths hit their `homeform::singleton()` guard and no-op. Buttons
  log and do nothing. **Never use it for this session.**
- **R3 · Spurious press replayed on connect (NEW, desk-found — a real qz bug).** On subscribing,
  `obclistener` dispatched `button 49 -> power_down` for a press fired **58 s earlier**. The
  Button-State characteristic is Read/Notify and retains the last value; service discovery reads it,
  `handleButton` sees a rising edge and fires. **Every qz connect will replay the last button press
  as a live action.** At the bike this shows as an unexplained power/gear jump the moment qz
  attaches — **do not chase it as a bike fault.** Fix (post-session): seed `m_lastState` from the
  initial read instead of dispatching, or ignore values received before the CCCD write completes.
- **R4 · CYD is a weak OBC host.** `free_heap` 61 KB (vs the C3's ~108 KB), `min_free_heap` 53 KB,
  **`loop_max_us` 21,968,121 = a 22-second max loop**, 75 stalls >200 ms. Only relevant if S2 runs.
- **R5 · Ordering.** qz's OBC block runs in `connectedAndDiscovered()` over the *accumulated* scan
  list, and discovery **stops** when the bike connects. **The C3 must already be advertising before
  qz connects the bike**, or the listener never attaches. Power the board first, always.
- **R6 · `obc_reader.py` and qz contend** for the same peripheral. Run **one** consumer at a time.
- **R7 · Bench binary ≠ upstream binary.** `bench/obc-listener+sb20-buttons` carries the local merge
  *and* `-allow-nonroot`. Results transfer to the PR only for the `obclistener` behaviour; do not
  claim the merged branch is what's proposed upstream.
- **R8 · Three BLE consumers, one SB20, one laptop radio.** The nRF opens a *4th* central onto the
  SB20; the C3's sink opens one; qz opens one; the SPA holds the nRF. **Only one thing may own a
  given link at a time** — that's exactly what bit us at the desk: the nRF had latched the C3 and
  the C3 consequently never advertised `OBC-SB20` at all. **Sequence the gates; never run G2 and N1
  concurrently.** If a board "won't advertise", suspect it is already *in a connection*.
- **R9 · The nRF is invisible to qz as shipped (NEW, desk-found).** It advertises name
  `SB20 Bridge` + service `0x1818` only. `main.cpp:1056` deliberately omits the 128-bit OBC UUID
  ("*Web Bluetooth reaches it via optionalServices, no need to advertise the 128-bit*"). So
  **neither** of qz's matchers can fire: no `OBC-` name, no OBC service UUID. N2 works around it by
  renaming `outName`. The durable fix is a firmware decision — advertise the OBC UUID (31-byte
  budget!) or ship an `OBC-` default name in devmode — **desk work, not rider-clock work.**
- **R11 · The Pages SPA deploy shipped `index.html` alone (FIXED).** Since the R2a change it is a
  `<script type="module">` importing `./bridge-codec.js`; a module whose import 404s **does not run
  at all** — no handlers, no banner, no log, dead Connect button. `deploy.sh` now parses the imports
  and aborts if one is missing. *(The ESP32 was never affected — it inlines the codec.)*
- **R12 · iOS/Bluefy rejects the numeric UUID shorthand (FIXED).** `optionalServices: [SVC, 0x1818]`
  → instant reject, **no picker**, and the rejection is not an `Error`, so the UI said only
  `connect failed: undefined`. Canonical UUID strings work. Chrome accepts both forms; **iOS does
  not** — so a desktop-only check can never catch this class of bug.
- **R13 · Bluefy drops the GATT link whenever it backgrounds (MITIGATED).** iOS suspends JS and tears
  the link down — **normal on a phone, not a fault**. The SPA now auto-reconnects (backoff, silent
  `getDevices()` resume, immediate retry on `visibilitychange`). **If the rider backgrounds the
  browser mid-gate, expect a brief red dot then self-recovery** — do not call that a failure.
- **R10 · BlueZ service caching can mask a discovery bug.** qz found the C3 via the *UUID* path only
  because BlueZ had cached 7 UUIDs (incl. `d273f680`) from an earlier connection; a live bleak scan
  of the same MAC showed only `0x1818` + `d445fe01`. **On a cold-cache machine the name match may be
  the only thing that works.** If you need a clean test: `sudo systemctl stop bluetooth &&
  sudo rm -rf /var/lib/bluetooth/*/cache && sudo systemctl start bluetooth`.

---

## 7b. Di2 / ANT+ shifters — **NOT in this session, and why**

Worth stating plainly so it isn't re-litigated at the bike. The groundwork is real:
`firmware/lib/proxy/AntControlsSource.h` (ANT+ Controls / Generic Command → OBC decoder, `#14`,
host-tested) exists, and the S340 ANT+ master was proven transmitting on air (`decisions.md`
2026-07-14, received by a Garmin stick). Per `obc-shifter-sources.md`, Di2 D-Fly / SRAM AXS spare
buttons are reachable over **ANT**, not plain BLE — so this is an **nRF-only** feature; the
BLE-only C3 can never do it.

**Three things block it today — all desk work, none of it rider work:**

1. **The S340 SoftDevice is not on this machine.** `firmware-nrf/vendor/softdevice/` contains only
   its README. It's licensed by Garmin/Dynastream, gitignored, and needs an owner download from a
   thisisant.com adopter account. Without it the `xiao-sense-s340` env **cannot compile here**.
   *(The plain `xiao-sense` env does build — verified today, `NRF_BUILD_EXIT=0`.)*
2. **Flashing an ANT SoftDevice needs SWD**, not serial DFU (`decisions.md` 2026-07-14) — and the
   nRF didn't even enumerate over USB on this laptop today.
3. **No Di2/D-Fly transmitter is in the test rig**, and `obc-shifter-sources.md`'s mapping table is
   explicitly *proposed defaults, not measured*.

**Next desk step if you want this:** provision the S340 + network key, confirm `xiao-sense-s340`
compiles, then a bench capture of a real D-Fly button to ground the mapping — *before* any gate is
written against it (PLAYBOOK §1: schedule the grounding capture strictly before the dependent gate).

## 8. Cleanup

Restore §4. Set `buttons.json` back to `{"enabled":false,"actions":[1,2,5,1,2,6]}`. Decide
deliberately whether the C3 stays in **devmode ON** (it was left that way after session 11 and never
recorded — record it this time). If S2 ran, `POST http://192.168.1.234/obc/devmode/off`.

---

## 9. Actual — fill in as we go

| Gate | Start | Result | Observed |
|---|---|---|---|
| G0 | 09:1x | ⚠️ then ✅ | C3 had dropped off WiFi (100% packet loss; CYD fine on same LAN) → power-cycle fixed. Bike awake. **Found a collision: the CYD was spoofing `Stages 62144`, the same identity as the real L crank** → powered off. |
| G1 | 09:4x | ✅ **PASS** | All 6 buttons, correct masks, one action each. `LEFT up→Plus target_power`, `LEFT down→Minus target_power`, `LEFT 3rd→setGears -1`, `RIGHT up→Plus peloton_offset`, `RIGHT down→Minus peloton_offset`, `RIGHT 3rd→setGears 0`. End-to-end proof incl. FTMS write `05 05 00`→bike ACK `80 05 01 05 00` and felt resistance change. |
| G1-B | 10:2x | ✅ 5/5 | LEFT up ×5 → target 205→210→215→220→225→230 W. Zero `0x03` commits in the phase — **all five would have been dropped pre-fix**. |
| G1-C | 10:3x | ✅ 5/5 | LEFT down ×5 → 230→225→220→215→210→205 W. **Was 0-for-2 before the fix.** The headline before/after. |
| G1-D | 10:4x | ⚠️→fixed | 3 s hold produced ONE action. Owner: long-press should repeat. Implemented hold-to-repeat (450 ms then 200 ms); built, **not yet ridden**. |
| G2 | 11:0x | ❌ **BLOCKED** | `obclistener` attached to the C3 by **name** (`OBC-SB20`; advert carries no OBC UUID — validates `bfb84695f`). But the C3 could never read the shifter: it discovers the SB20 **by advertisement**, and qz's connection stops the SB20 advertising. Ordering deadlock. |
| N0/N1/N2 | — | ⏭️ not run | Out of rider time. nRF was off for G2 (it latches the C3 and suppresses its OBC advert — reproduced twice). |

**Deviation log:**
- ANT stick found mid-prep → dual-radio restored (the earlier BLE-only deviation was superseded).
- Bike telemetry was dead early on (`Current Watt: 0`, frozen CSC). Resolved after a cold start; **cause not established** — owner's Garmin-on-trainer-protocol hypothesis is the leading candidate. See #288.
- **Agent error:** three orphaned qz processes were competing for the BLE adapter for part of the session (failed `kill`s). May have corrupted some intermediate readings.
- **Agent error:** the Phase C/D sniffer capture was lost to the wrong venv (`.venv` vs `code/.venv`) — the session-12 trap. qz's log was the authoritative record regardless.

**Captures banked** (`code/findings/captures/`):
- `SNIFF-session13-sb20-app-wake-20260726.pcap` (29,339 pkts) — degraded telemetry + app connect
- `SNIFF-session13-coldstart-qz-only.pcap` — healthy telemetry, qz sole consumer (the A/B pair for #288)
- `SNIFF-session13-buttons.pcap`, `SNIFF-session13-phaseD.pcap`
- `session13-qz-g1-buttons.log.gz` — the authoritative button-behaviour record



---

## 10. Retro — mandatory before close-out

### ⭐ Process change agreed mid-session: be Bayesian, together

The agent repeatedly stated causal conclusions that the next measurement contradicted. The
*measurements* were sound every time; the *interpretations* were premature. Owner: **"Let's be
Bayesian together. We gather evidence to improve our confidence."**

Four instances, and they were **not** the same error — each has a different fix:

| Claim | Failure | The move that was skipped |
|---|---|---|
| "You're not pedalling" | 2 hypotheses, picked one | **Ask.** The discriminator cost one line and I owned it |
| "SB20's FTMS flags are inconsistent with its payload" | n=8 → asserted a spec violation | **Prior was backwards.** A shipped device obeying spec is likelier than a hasty flag decode being right. (The bike was honest; it was sending a *degraded frame set*, `0x0011` vs `0x00c5`) |
| "The Stages app woke the telemetry" | n=1 correlation → causation | **Name confounders before concluding.** Owner supplied the one I lacked: a Garmin paired over the *trainer* protocol |
| "The fix double-fires" | miscounted a log that prints every line twice | **Validate the instrument before interpreting it.** Not a hypothesis error — a measurement error |

**Rules to carry into the playbook:**
1. State a claim's **confidence** and the **evidence that would falsify it**, before acting on it.
2. When a cheap discriminating test exists (*ask the rider; look at one more field*), run it **first**.
3. Prefer "my decode is wrong" over "the shipped device is wrong" until proven otherwise.
4. **Check the measurement instrument** (duplicate log lines, wrong venv, self-matching `pkill`)
   before drawing conclusions from it.
5. The rider holds priors the agent cannot observe (their own rig, their own habits). **Surface
   hypotheses to them early** rather than presenting conclusions.

- **Went well:**
  - G1 passed comprehensively, and the *fix* was found, written, built and validated **with the rider
    on the bike** — a bench could never have found it (the bike only emits `0x03` ~6% of the time).
  - Desk pre-stage paid for itself: qz-as-OBC-consumer, the iOS SPA, and both radios were all proven
    before the rider sat down, so bike time went on things that genuinely needed a rider.
  - The owner's domain priors repeatedly beat the agent's inference (ANT+ vs BLE radios; the
    Garmin-on-trainer hypothesis; long-press should repeat). Surfacing hypotheses early worked.

- **Went wrong / slow / confusing (+ root cause):**
  - **No single reference document for how the system is meant to work.** The agent reverse-engineered
    intended behaviour from source and logs over and over — which board serves which role, who
    discovers whom, what the modes mean, what should be connected during which test. Nearly every
    wrong turn today traces back to this. *(Owner, in-session: "You need a solid reference document to
    refer to during our work on how everything is meant to work.")* **This is the #1 action.**
  - **Premature causal claims** (4 of them) — see the Bayesian section above.
  - **Agent process errors that corrupted evidence:** three orphaned qz processes competing for the
    adapter; a capture lost to the wrong venv; `pkill` patterns self-matching the agent's own shell
    (3x). All avoidable, all cost rider attention.
  - **Discovery ordering is a real architectural problem, not a bench artifact** — see G2.

- **Planned vs actual:** planned ~65 min / 6 must-dos. Actual ~2 h for G0+G1 only. The overrun was
  almost entirely diagnosis-of-the-unexpected (dead telemetry, the CYD identity clash, the C3 dropping
  off WiFi, the qz commit-frame bug) — i.e. investigation, which PLAYBOOK §1 says to budget as such.
  **We budgeted these as verification steps; they were investigation.**

- **Changes to make before next session:**
  1. **Write the reference doc** (see the new issue). One page: boards, roles, who connects to whom,
     what each mode means, and the legal configurations. Read it at session start.
  2. **A pre-gate "expected topology" check** — before each gate, state which devices should be
     connected to what, and verify it, rather than discovering mid-gate that a board latched something.
  3. Agent hygiene: verify a process is dead before starting another; always `code/.venv`; never a
     `pkill` pattern that matches the agent's own command line.
  4. Fold the Bayesian rules (above) into PLAYBOOK.md.

- **Next gate + desk work that must precede it:**
  - **G2 needs the ordering problem solved first** (issue filed) — either a documented startup
    sequence or the C3-proxies-everything design. **Do not re-attempt G2 on a rider's clock until
    that decision is made.**
  - **Hold-to-repeat is built but never ridden** — first thing to validate next session.
  - N0/N1/N2 (nRF) never ran; the nRF and C3 cannot both serve OBC (reproduced twice).

*Close-out (PLAYBOOK §close-out): flip Status to ✅ DONE + one-line Outcome, add/refresh the row in
[`sessions/README.md`](README.md), promote durable findings to
[`decisions.md`](../code/findings/decisions.md) — **including R3 (the connect-replay bug), R1 (the
double-fire) and the two §2 corrections** — commit captures, and retarget
[`BIKE-SESSION-READY.md`](../BIKE-SESSION-READY.md) at the next READY session (12).*
