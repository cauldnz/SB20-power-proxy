# Session: nRF S340 + ANT+ bring-up (flash the S340 build, prove ANT on air)

**Status:** ✅✅ **DONE — FULL END-TO-END PROOF (2026-07-14).** S340 flashed with **NO probe** (rebuilt
S340-bundled bootloader, DFU); the ANT+ Bike Power master transmits *and* was **received + decoded by a
Garmin ANT USBStick2** (openant, desk-side): `device_id=62144 type=11 trans=5`, page 0x10 = **150 W / 90 rpm**
(the exact mock). read→correct→ANT-rebroadcast is proven end-to-end; the "needs SWD" status was premature
(RTFM turnaround in the actuals). **Next:** feed real BLE-source readings into `setReading()` (P4b). Full
actuals at the bottom.
**Prereq:** ✅ done — S340 provisioned (`vendor/softdevice/`), the `xiao-sense-s340` firmware **compiles +
links** (`.pio/build/xiao-sense-s340/firmware.hex`), broadcasting a mock **150 W** ANT+ Bike Power master.

> **Read first:** this is a **SoftDevice swap** (S140 → S340), *not* a normal UF2 upload. It carries real
> brick/boot risk and several **runtime unknowns the compile can't answer**. Go one step at a time, keep
> the S140 build handy to recover, and **stop + record** at the first failure. Budget: the rider's patience.

---

## What was built overnight (the state you're starting from)
- `[env:xiao-sense-s340]` — the S140 default build is **byte-identical/untouched**; this is an *additive*
  second env. App links at **0x31000** (S340 map: MBR 0x1000 + SD 0x30000). `firmware.hex` + `firmware.zip`.
- `src/ant/AntMasterChannel` — an ANT+ Bike Power **master** (dev 62144 / type 0x0B / tx-type 5 / RF 57 /
  period 8182) broadcasting `AntMasterScheduler`'s pages (host-tested). Guarded so it's inert without S340.
- **Runtime unknowns (compile ≠ runs):** (1) does the S340 actually **enable** under the Bluefruit core,
  BLE still up? (2) **ANT license key** — `sd_softdevice_enable`'s 3rd arg; Bluefruit may pass NULL, so
  ANT may not start without a core patch to pass `ANT_LICENSE_KEY`. (3) **app RAM base** 0x20006000 may be
  too low for concurrent BLE+ANT → `sd_ble_enable`/`sd_ant_enable` returns the needed base (bump the
  linker `RAM ORIGIN`). Watch `[ant] master ... (err=0x...)` + `[bridge] up` on the USB serial.

## Which board — recovery matters
- **XIAO nRF52840 Sense** (the real Bridge board): recovers via **double-tap-RST → UF2 drive** *as long as
  the Adafruit bootloader isn't overwritten*. A bad **app/SD** is recoverable (re-flash the S140 build);
  a bad **bootloader** is SWD-only. So: **do NOT flash a rebuilt bootloader yet.**
- If you have the **Nordic nRF52840 dongle** (the sniffer, `1915:522A`): its button-forced Open-Bootloader
  DFU is the most recoverable staging ground, but our firmware targets the XIAO — a dongle build is extra
  work. For today, the XIAO with UF2 recovery ready is fine.

---

## Steps (tick in place: ✅ / ❌ / ⚠️ + what you saw)

**S0 — Baseline (prove the board + your recovery path).**
- [ ] Plug in the XIAO. `pio run -e xiao-sense -t upload` (the *S140* build). Confirm it boots, advertises,
      BLE works (Web Bluetooth or `nRF Connect`). **This proves your normal flash+recover loop before you
      touch S340.** If this fails, fix it first.

**S1 — Inspect the DFU package (does it carry the S340 SoftDevice?).**
- [ ] `unzip -l .pio/build/xiao-sense-s340/firmware.zip` (or open it). Note whether it contains the **S340
      SoftDevice** + a `manifest.json` referencing it, or **app-only**. This decides S2:
      - **app-only** → uploading it leaves the S140 SD in place → the S340 app runs against the wrong SD →
        crash (recoverable). You need to flash the SD too (S2b/SWD).
      - **SD+app** → the bootloader's DFU will try to swap the SD (S2a) — the real test.

**S2 — Flash S340. Pick ONE path; have the S140 build ready to recover.**
- **S2a (UF2/DFU, easiest, try first):** `pio run -e xiao-sense-s340 -t upload`. Watch the nrfutil output
  for SD-update + success/reject. Then watch the **USB serial** on reboot:
  - [ ] `[bridge] up ...` prints → the SoftDevice enabled + BLE init survived. 🎉
  - [ ] `[ant] master up (...)` → ANT channel opened (err=0). If `FAILED (err=0x...)` record the code
        (0x4 = NO_MEM → bump RAM origin, rebuild; a license error → the NULL-key issue, see S3).
  - [ ] If it **hangs / bootloops / no serial** → double-tap-RST → UF2 drive → re-flash the **S140** build
        (S0) to recover. Record how far it got (serial, LED). This is the expected place to learn the SD/
        bootloader-mismatch reality.
- **S2b (SWD full image, the clean way — if you have a J-Link/CMSIS-DAP + the XIAO SWD pads):** flash the
  combined image: the S340 hex (`vendor/softdevice/ANT_s340_nrf52840_6.1.1/ANT_s340_nrf52840_6.1.1.hex`)
  **then** the app (`.pio/build/xiao-sense-s340/firmware.hex`). This bypasses the bootloader-SD-validation
  question entirely. (Details: `nrf52-sense.md §ANT`.)

**S3 — If BLE comes up but ANT doesn't (license/RAM):**
- [ ] **License:** if the ANT channel errors with an invalid-license/param code, the core is calling
      `sd_softdevice_enable` without the key. Patch the core (or a pre-init hook) to pass
      `ANT_LICENSE_KEY` as the 3rd arg, rebuild, re-flash. (Compile-time key is already set; runtime pass
      is the gap.)
- [ ] **RAM:** on `NO_MEM`, read the required `app_ram_base` the enable call reports, set `RAM ORIGIN` in
      `linker/nrf52840_s340_v6.ld` to it, rebuild, re-flash.

**S4 — On-air proof (the payoff).**
- [ ] With `[ant] master up`, open **Garmin Connect / a head unit / SimulANT+** and scan for an **ANT+
      power meter**. Expect a meter at **~150 W** (the mock), device **62144**. Screenshot it.
- [ ] Bonus: `SimulANT+` (in `vendor/softdevice/`) as a slave to decode the pages byte-for-byte.

---

## After (per the playbook)
- Annotate every step above with the actual result. Flip `Status:` to DONE/blocked.
- Promote durable findings to `code/findings/decisions.md` (append-only) — esp. the **real** app RAM base,
  whether the UF2/DFU SD-swap works, and the license-key runtime resolution. Update `nrf-roadmap.md` P3/P4.
- If ANT is proven on air: next is P4b — feed **real** BLE-source readings into `setReading()` (the
  `IRadioSource`/`IRadioSink` seam) + the ANT **slave** source, and the **Stages spoof** identity vs a real
  SB20 (run-sheet R3).

---

## Actual — 2026-07-14

- **S0 ✅** — `pio run -e xiao-sense -t upload --upload-port COM9` programmed; board boots the app, `SHOW`
  + `[hb]` heartbeat respond. **Flash/recover loop proven.** (XIAO app = `2886:8045`; bootloader = `2886:0065`
  on a *different* COM + a UF2 mass-storage drive.)
- **S1 ✅** — the PlatformIO DFU zip is **app-only** and its manifest mislabels `softdevice_req: 0x123`
  (S140, from the board JSON) → uploading it as-is can't swap the SD. Built a proper **combined SD+app**
  package with `adafruit-nrfutil dfu genpkg --dev-type 0x0052 --sd-req 0x0123 --softdevice <S340.hex>
  --application <app.hex>` (both images present, softdevice_req 0x123 so the bootloader accepts it).
  - Tooling gotcha: the bundled `adafruit-nrfutil.exe` is a frozen exe **blocked by Application Control**
    (`python38.dll` load blocked). Fix: `pip install adafruit-nrfutil` into the venv and use that console
    script (PlatformIO itself uses the `.py` via python, which is why S0's upload worked).
- **S2a ❌ — serial DFU cannot flash the S340 SoftDevice on the stock (S140) bootloader.** `--touch 1200`
  resets to the bootloader on a *new* COM (COM9→COM11), then `dfu serial -p COM11 --singlebank` **opens,
  begins sending the SoftDevice image, and dies on the first packet:** `WriteFile failed
  (PermissionError 13 'The device does not recognize the command')` — the bootloader resets when it starts
  the SD update, dropping the USB CDC mid-transfer. This is exactly the roadmap's *"bootloader rebuilt
  against S340"* caveat, hit empirically.
- **Recovery ✅** — the SD write failed on packet 1 (before erasing), so the S140 SD survived: re-flashing
  the **S140 app** to the bootloader (`dfu serial -p COM11 -pkg xiao-sense/firmware.zip`) booted straight
  back to a working Bridge. **The board is fine.**

### Verdict + next step
The **build is done and proven** (compiles/links, no S140 regression); the blocker is purely **getting the
S340 SoftDevice onto the chip**. The stock Adafruit/Seeed bootloader won't serial-DFU an ANT SoftDevice
(USB drops on the SD reset). Paths:
1. **SWD (ordered a probe) — the clean fix (S2b):** flash the S340 hex + app hex directly, bypassing the
   bootloader. `nrfjprog`/`pyocd`/OpenOCD + the XIAO SWD pads. **Do this when the probe arrives.**
2. UF2 mass-storage for the SD is unlikely (the bootloader typically protects the SD region) — not worth
   the risk vs waiting for the probe.
3. (Bigger) rebuild the Adafruit bootloader against S340 — itself brick-risk; a probe makes it moot.

**When the probe lands:** `combined_s340.zip` + the raw hexes are already built in
`.pio/build/xiao-sense-s340/`; SWD-flash `ANT_s340_nrf52840_6.1.1.hex` then `firmware.hex`, then watch the
serial for `[bridge] up` + `[ant] master up` and scan from a Garmin/SimulANT+ for a ~150 W meter (dev 62144).

---

## Actual — 2026-07-14 (later): flashed WITHOUT a probe; ANT transmit CONFIRMED ✅

The "needs SWD" verdict above was **wrong / premature** (RTFM guardrail — a failed first attempt ≠
impossible). Reading the `Adafruit_nRF52_Bootloader` source showed it already has **first-class ANT
support** and is itself **DFU-updatable** — so the SWD probe was never actually required.

- **Rebuilt an S340-bundled bootloader** (WSL, `arm-none-eabi-gcc`) and DFU-flashed **bootloader+SD**, then
  the **app** (`--sd-req 0x00B9`), all over serial — **no probe**. Board boots the S340 app, BLE scanning +
  `[hb]` alive. The runtime `ANT_LICENSE_KEY` (eval key) is baked into the bootloader's
  `sd_softdevice_enable` call — verified the string is in `main.o`. Reusable recipe + flash budget written up
  in `code/findings/nrf52-sense.md §ANT`.
- **Added an `ANT` USB-serial diagnostic** (`AntMasterChannel` counters + a command in `main.cpp`, both
  `NRF_HAS_ANT`-guarded → default build byte-identical) to read channel state headlessly, since the
  DTR boot-log reset doesn't fire on the XIAO. Result on hardware (COM9):
  `[ant] beginErr=0x00000000 step=0 opened=1 events=189 tx=189 rx=0 lastEvt=0x03 lastTxErr=0x00000000`
  → all six `sd_ant_*` bring-up calls SUCCEEDED, `EVENT_TX` fires, `tx` climbs **~4/sec** (= period 8182 =
  4.06 Hz), every broadcast returns 0. **The Bike Power master is on air.**
- **RTFM ruled out all three roadmap "runtime unknowns" by reading, not flashing:** `sd_ant_enable()` is
  optional (SD defaults to 1 channel + 64 B burst — exactly our config, `ant_interface.h:916`); the ANT
  license is present (key in the bootloader `main.o`); RAM origin `0x20006000` was fine (BLE **and** ANT
  both enabled cleanly — no NO_MEM, no bump).
- **S4 (on-air pair) ✅ PASS — received + decoded desk-side.** Owner plugged a **Garmin ANT USBStick2**
  (0x0FCF:0x1008) into the dev box; **openant 1.3.4** decoded the broadcast: scanner saw
  `device_id=62144 type=11 trans=5` and the `PowerMeter` profile read **page 0x10 = 150 W, cadence 90**
  (24 pages / 16 s, zero errors) — the exact mock. Windows gotcha: pyusb `NoBackendError` until a libusb
  backend is wired via `libusb-package` (`pip install openant libusb-package`; recipe in `decisions.md` +
  `nrf52-sense.md §ANT`). **This is now the desk-side ANT-out twin — no head unit needed to validate the
  master.** Scripts: `scratchpad/ant_rx.py` + `ant_power.py`.
