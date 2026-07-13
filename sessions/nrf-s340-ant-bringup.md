# Session: nRF S340 + ANT+ bring-up (flash the S340 build, prove ANT on air)

**Status:** PLANNED (built overnight 2026-07-13 → 07-14; flash + on-air is this session).
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
