# Decisions Log

Append entries chronologically. Don't edit old entries — write new ones to supersede. Date stamps in YYYY-MM-DD format. The point is that future-you (or another implementer) can read this top to bottom and understand how thinking evolved.

Format per entry:

```
## YYYY-MM-DD — Short title

Decision: <one sentence>

Context: <why this came up>

Options considered: <list>

Why this option: <reasoning>

Will revisit if: <what would change our minds>
```

---

## 2026-05-10 — Initial project scoping (placeholder, replace with date of actual start)

Decision: Pursue an ANT+ man-in-the-middle proxy approach rather than pure-app-side bridging (TR PowerMatch / QZ).

Context: The goal is for the SB20 *itself* to use Assioma data — not just for connected apps to see Assioma numbers. App-side bridges leave the bike's internal control loop using its own Stages cranks, which become a single point of failure when (not if) those cranks die.

Options considered:
1. App-side bridge (TR PowerMatch / QZ) — works today but doesn't address the bike-level use case
2. ANT+ MITM proxy presenting as a Stages crank — addresses the use case if technically feasible
3. Modify SB20 firmware — out of reach without serious reverse engineering, also brittle
4. BLE-side spoof — alternative to ANT+ MITM, kept as fallback

Why option 2: Cleanly addresses use cases 1 and 2 from the brief. Independent of any specific training app. Builds on the bike's existing protocol contract.

Will revisit if: Phase 0 captures reveal the SB20 has a bespoke handshake we cannot replicate without proprietary keys / firmware reverse engineering. In that case, BLE-side or app-side approaches become the next options.

## 2026-05-10 — Project sits inside parent research effort

Decision: Treat the owner's parent `Research_Content` document as foundational context rather than duplicating its content into this package.

Context: The owner has a broader fitness-sensor research effort surveying ANT+/BLE protocols, Python and Rust libraries, Pi hardware, browser delivery, and target devices including SB20, Tacx Neo, and Concept2 PM5. This SB20 project is one specific application of that broader work.

Why this matters for the package:
- Foundational protocol knowledge (ANT+ vs ANT-FS, GATT services, profile mappings) lives in the parent doc — we reference rather than re-explain.
- Library recommendations (openant, pycycling, bleak; eventually ant-rs, btleplug, bluer) come from there.
- Long-term architectural direction (Python today, Rust gateway later via openant→MQTT→Rust pattern) is articulated there.

Will revisit if: the parent research is updated in a way that changes the recommended toolchain.

## 2026-05-10 — Python today, Rust later

Decision: v1 implementation in pure Python (openant). Defer any Rust port until Phases 0–3 are working.

Context: The parent research notes that Rust ANT+ support (ant-rs / ant-plus) is improving but not yet at openant's maturity, particularly for the master/transmit side that this project depends on. Python's openant has been used successfully for ANT+ master-broadcast in projects like dhague/vpower.

Will revisit if: ant-rs reaches feature parity for master-mode Bike Power broadcasts (the 2026 roadmap suggests this might happen). At that point the openant→MQTT→Rust pattern becomes worth considering, especially for Pi deployment where Rust's resource efficiency would help on a Pi 4 or smaller.

## 2026-05-10 — QZ as architectural validator and Peloton reference

Decision: Treat `cagnulein/qdomyos-zwift` (QZ) as the primary external reference for fitness-device protocol bridging, but do not depend on or copy from it.

Context: QZ is a mature C++/Qt cross-platform application that bridges dozens of proprietary fitness bikes/treadmills/ergs into standard BLE FTMS/CPS broadcasts. Its hierarchical device architecture (a `bluetoothdevice` abstract base class with virtual-device targets) is a C++/Qt analogue of our `PowerSource`/`PowerTarget` design — independent convergence on the same shape from a much larger codebase is encouraging. QZ also has working bidirectional Peloton support, which is the closest reference for the future Peloton project the owner has flagged.

Why we don't copy: QZ is GPL-3.0. Our project will be MIT (or similar permissive) so other SB20 owners can adopt it without copyleft constraints. We can study and reimplement, but not transcribe.

Will revisit if: the project's planned permissive license becomes a non-issue (e.g. the owner decides GPL-3.0 is fine after all), in which case selective porting from QZ becomes legitimate.

## 2026-05-10 — Defer creation of project-level CLAUDE.md

Decision: Don't create a `CLAUDE.md` at the project root yet; do so as one of the first concrete actions after Phase 0 captures begin to land.

Context: QZ has a substantial `CLAUDE.md` documenting build commands, architecture, and verification steps for adding new device patterns. We could mirror that, but right now we don't have enough concrete project surface (no real device patterns yet, no captured protocol details) to write a useful equivalent. Creating one too early would be aspirational rather than load-bearing.

Will revisit when: Phase 0 captures are committed and Phase 1 implementation begins. At that point a `CLAUDE.md` describing capture-then-decode workflow, the source/target ABC contract, and how to add a new `PowerSource` becomes worth writing.

## 2026-06-10 — openant 1.3.4 API verified; capture path hardened before first real session

Decision: Harden `01_capture_stages.py` ahead of Session A / Session C-0, after verifying every assumed openant API against the actual installed source (openant **1.3.4**), not against assumption.

What was verified (against real source, openant 1.3.4):
- `Channel.Type.BIDIRECTIONAL_RECEIVE == 0x00`, plus `set_id` / `set_period` / `set_rf_freq` / `set_search_timeout` — all present with the assumed signatures (`easy/channel.py`).
- `on_acknowledge_data` is the correct RX-ack hook: `node._main` dispatches received acknowledged data to `on_acknowledge_data` (`easy/node.py:209-210`). Note: openant's own slave reference (`devices/common.py:272`) wires `on_acknowledge` instead, which is **never** dispatched — a latent bug in the library's reference; our script's `on_acknowledge_data` is the right choice, do not "fix" it to match the reference.
- `ANTPLUS_NETWORK_KEY == [0xB9,0xA5,0x21,0xFB,0xBD,0x72,0xC3,0x45]` imports from `openant.devices`.
- `Channel.enable_extended_messages(enable)` exists natively (`easy/channel.py:118` → 0x66 `ENABLE_EXT_RX_MESGS`). **No pirower fork needed.**
- Reference `PowerMeter` uses `period=8182, device_type=11, trans_type=0` (`devices/power_meter.py:36-40`) — identical to our script defaults and the validator's `EXPECTED_PERIOD`.

Load-bearing protocol facts (confirmed against `devices/common.py`):
- **Common Page 0x50 (Manufacturer ID):** `hw_rev = data[3]`, `manufacturer_id = data[4:6] LE`, `model = data[6:8] LE` (common.py:366-369). The H2 smoking-gun offset (manufacturer_id at bytes 4-5) is **correct** as coded.
- **Extended-message tail:** when 0x66 is enabled, the source channel ID is *appended* after the 8 data bytes — `data[8]=flag, data[9:11]=device number LE, data[11]=device type, data[12]=trans type` (common.py:329-334). Because it is appended, decode of pages 0x10/0x12/0x50/0x01 (all in bytes 0-7) is unaffected.

Changes applied to `01_capture_stages.py`:
1. Actually call `enable_extended_messages(1)` in `setup()` — the docstring previously *claimed* extended messages were on but the code never enabled them. Logs an `ext_messages` record (`enabled=True/False`).
2. `decode_page` now parses the extended tail into `ext_device_number` / `ext_device_type` / `ext_transmission_type` (source-meter ID per packet — disambiguates Sessions C/F).
3. `decode_page` matches pages against `data[0] & 0x7F` (toggle-bit-robust) while still recording the raw page byte + toggle bit.
4. New `--log-channel-events` flag tees non-data channel events (RX_FAIL=2, search-timeout=1, channel-closed=7, collision=9) into the JSONL via a chained wrap of `node.ant.channel_event_function`. Off by default; recommended for Sessions C/F. (Closes the gap that the previous `_on_event` handler was dead code — openant never dispatches channel events to a per-channel callback.)

Hypothesis refuted / reframed: the assumption that `on_acknowledge_data` would capture the **SB20→crank** pairing/zero-reset traffic is **wrong**. The SB20 is itself an ANT+ *slave* to the crank-master, and a second passive slave cannot sniff another slave's uplink. The capturable artefact is the crank's calibration **response** (page 0x01, ID 0xAC + offset), which the Bike Power profile sends as an interleaved **broadcast** — so it arrives as `kind="broadcast"`, page 0x01, **not** `kind="acknowledged"`. Session C-0's pass criterion in `03-...md` was rewritten accordingly.

Validation done (no hardware): `py_compile` clean; `decode_page` smoke-tested on synthetic fixtures (plain page, page+ext tail, 0x50 manuf=69, toggled 0x90 page, 0x01 cal response offset=-50, short-payload guard — all pass); a full synthetic JSONL run end-to-end through `00_validate` (PASS/REVIEW), `04_summarize` (manuf_id + cal_id + channel-event row render), and `05_diff` (manufacturer_id 69-vs-263 headline row). Reference fixture for the 69-vs-263 manufacturer-ID diff retained.

Will revisit when: real Session A + C-0 captures land. If C-0 shows no page 0x01 broadcast on zero-reset, the next step is sniffer hardware for the request bytes — but the proxy only strictly needs the response.

## 2026-06-10 — WSL ANT-stick passthrough hardened; openant udev helper is broken for pip installs

Decision: Stop using `python -m openant.udev_rules` in all setup docs; write the udev rule directly. Harden the WSL USB-passthrough instructions.

Refuted assumption: the documented `sudo $(which python) -m openant.udev_rules` (in CLAUDE.md, START-HERE.md Step 4, and 07's WSL + Pi sections) does **not** work with a pip-installed openant. Verified against openant 1.3.4: `udev_rules.install_udev_rules()` does `shutil.copy("resources/42-ant-usb-sticks.rules", "/etc/udev/rules.d")` — a **relative** path — and the `resources/` directory is **not shipped in the wheel** (confirmed: no `*.rules` file anywhere under site-packages). So the command fails with `FileNotFoundError` regardless of how it's invoked. The previous START-HERE variant (`pip install --user openant` + `sudo $(python3 -m site --user-base)/bin/python3 -m openant.udev_rules`) was doubly broken: `--user` installs no python at `~/.local/bin/python3`, and root can't see `--user` packages anyway.

Replacement (works, self-contained, no openant dependency):
```bash
sudo tee /etc/udev/rules.d/42-ant-usb-sticks.rules >/dev/null <<'RULE'
SUBSYSTEM=="usb", ATTRS{idVendor}=="0fcf", MODE="0666"
RULE
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=usb --attr-match=idVendor=0fcf --action=add
```
Vendor-only match (0x0fcf = Dynastream/Garmin) covers all ANT stick PIDs (0x1004/0x1008/0x1009) and future ones. `lsusb` shows our stick as `0fcf:1009`.

Other WSL passthrough fixes applied to START-HERE.md / 07:
- Added `wsl --update` to Step 1. The #1 silent failure is an old WSL kernel lacking USB/IP (vhci_hcd): `usbipd attach` reports success but `lsusb` is empty. Added a troubleshooting note for exactly this symptom.
- Noted the udev-in-WSL caveat: rules only auto-apply if WSL runs systemd/udev. Fallback documented — run captures as `sudo $(which python) scripts/01_capture_stages.py ...` (absolute venv-python path preserves the environment).
- Reordered: udev rule now applied *after* the venv `pip install` (one-time apt libs stay in the passthrough section).

usbipd syntax confirmed current (4.x): `usbipd list` / `usbipd bind --busid <B>` (persists) / `usbipd attach --wsl --busid <B>` (per WSL session). VID to look for: 0x0fcf.

Will revisit if: openant ships the rules file in a future wheel, or adds an `openant udev` CLI subcommand — then the helper becomes usable again.

## 2026-06-10 — Final pre-session review: end-of-capture crash fixed, findings path unified

Decision: Three more fixes from a final fresh-eyes review before the first hardware session, all verified without hardware.

1. **`stop()` made idempotent in `01_capture_stages.py`.** The duration-expiry path called `stop()` twice — from the SIGALRM handler, then from `run()`'s `finally`. The second call closed an already-stopped driver, the `except` then logged to an already-closed JSONL file, and the script died with `ValueError: I/O operation on closed file` at the end of every duration-limited capture. The JSONL itself was complete (first `stop()` wrote `session_end` before closing), so the validator would have said PASS while the console showed a traceback — a confusing "looks broken, actually fine" failure for the very first session. Pre-existing bug, present before this revision's other changes; found by tracing the alarm path end-to-end rather than reviewing `stop()` in isolation. Guard: `self._stopped` flag, second call returns immediately. Tested: double/triple `stop()` produces exactly one `session_end` and no exception.

2. **Validator checks `ext_messages`.** `00_validate_capture.py` now reports PASS if extended messages enabled, WARN if the enable failed (with the error string) or if the record is missing (likely an outdated copy of the capture script). Tested on all three fixture variants.

3. **Findings path unified to `code/findings/captures/`.** Docs disagreed on where captures live: script docstrings → `code/findings/` (correct, where `decisions.md` and the committed tree are); START-HERE (cwd repo root) and 09/code-README/07 (cwd `code/`) both resolved to a nonexistent root-level `findings/`. All commands now resolve to `code/findings/captures/`. Root `.gitignore` patterns updated (`code/findings/captures/*.jsonl|json` ignored by default, `.gitkeep` kept, opt-in commit via `git add -f` — policy unchanged, just re-anchored). HANDOFF and CLAUDE-CODE-PROMPT references updated so the *next* Claude session looks in the right place.

Also: validator glob examples quoted everywhere (`--input 'code/findings/captures/A-*.jsonl'`) — the script picks the newest match itself; an unquoted glob breaks via shell expansion as soon as a second matching file exists (e.g. smoke-test + real Session A on the same day).

Confirmed fine on review (no change needed): `02_capture_assioma.py` re-executes 01 by path and inherits all hardening including `--log-channel-events`; `pyproject.toml` pins `openant>=1.3.0` (verified against 1.3.4); the channel-event tap's `_DATA_EVENT_CODES` filter matches openant's `Message.Code` values (data events 3/1000/2000/3000 excluded; RX_FAIL=2, SEARCH_TIMEOUT=1, CHANNEL_CLOSED=7, COLLISION=9 captured).

## 2026-06-10 — First real capture: Stages manufacturer ID CONFIRMED = 69

Finding (from `A-stagesL-steady-20260610-1740.jsonl`, device #62144, 56s smoke run, validator verdict REVIEW — short length + old-script warning only):

- **Page 0x50 manufacturer_id = 69 (0x45) — the hypothesised Stages value is confirmed from a live capture.** `raw_hex 50ffff0345000300`: hw_rev 3, manufacturer_id 69, model_number 3. The H2 diff target is no longer "to be confirmed."
- Page 0x51: sw 18.2 (main 18, supp 2), serial 11821518. Page 0x52 present.
- **Page mix: 0x12 (crank torque) dominant** — 86×0x12, 43×0x10, 30×0x13, 2 each of 0x50/0x51/0x52 over ~56s. The proxy will likely need to emit 0x12 (and possibly 0x13), not just power-only 0x10.
- Power mean 156 W / max 314 W, cadence 42–72 rpm — real pedalling, plausibly combined (not half) power.
- **Aggregate rate 2.94 Hz, not 4 Hz — expected, not a dropout.** openant's base layer deliberately skips consecutive identical broadcasts (`ant.py` `_last_data` check: "Only do callbacks for new data"). The JSONL is lossless w.r.t. *distinct* messages; identical repeats are coalesced. Do not chase this as RF interference.
- `openant scan` found two PowerMeter IDs live: **62144** (captured) and **17039** (uncaptured — presumably the other crank or the bike's own PM rebroadcast). Which sticker ID is the L crank still needs confirming against the bike/app before the full Session A is trusted.

Process notes: the WSL working clone was pulled from GitHub and predates Rev 7, so this capture ran the OLD script (no ext_messages record, extended messages off). The two hardened scripts were copied directly into the WSL tree (hash-verified) after this run; the Windows repo still holds the uncommitted canonical changes — commit + push after today's sessions, then `git pull` in WSL to reconverge. openant scan's Ctrl-C `USBError ... attach_kernel_driver` traceback is a known openant scanner-shutdown quirk under WSL (no kernel driver to reattach) — cosmetic, ignore.

## 2026-06-10 — Full read of the Session A smoke capture (56s, device #62144, old-script run)

Protocol observations from reading the complete JSONL (these inform the eventual spoof spec; full 15-min Session A still to come):

- **Device #62144 is broadcasting dual-sided combined data**: page 0x10 carries `pedal_power_differentiation=true` with balance fluctuating 47–65%, and page 0x13 carries distinct left/right TE+PS values. That's the combined (L-master) stream, not a half-power R broadcast — consistent with #62144 being the **L crank or the bike's mirror of it**. Sticker/app check still needed to distinguish from #17039 (the other PowerMeter ID `openant scan` saw).
- **Page cycle**: a repeating ~4-message pattern of 0x12, 0x12, 0x10, 0x13 (crank torque at ~2× the rate of power-only), with a **burst of all three commons (0x50, 0x51, 0x52 consecutively) roughly every 30 s** (seen at t≈13.4s and t≈43.2s). The proxy will need to emit 0x12 and 0x13, not just 0x10, and schedule a commons burst.
- **Battery heads-up**: page 0x52 = `52ff019ddc019db2` → status **Ok** (not New/Good), coarse+frac voltage ≈ **2.61 V**, ~68 h operating time on a 2 s-resolution counter. A fresh CR2032 reads ~3 V. Per the Phase-0 risk note ("cranks may already be partially failing"), consider fresh CR2032s before Session C so weak batteries don't add noise to the calibration capture.
- The observed ~2.9 Hz log rate with occasional identical consecutive 0x12 rows is explained: openant's dedup only drops *immediately* repeated payloads; an interleaved 0x13/0x10 between two identical 0x12 transmissions lets both through. On-air rate is the standard 4 Hz.
- Ctrl-C teardown logged `node_stop_error: [Errno 2] Entity not found` then a clean `session_end` — the same benign libusb attach_kernel_driver quirk as the scanner; handled, cosmetic.

Still outstanding: full 15-min Session A (varied power) with the Rev-7 script (ext_messages on), Session C-0 dry run, identity confirmation of #62144 vs #17039.

## 2026-06-10 — Parallel BLE capture added; BLE path is the ESP32/QZ optionality route

Decision: capture the cranks' BLE Cycling Power side passively, in parallel with the ANT+ sessions, via a new `06_capture_ble.py` (bleak) running on native Windows. The bike stays ANT+-paired throughout; the "Pair with Bluetooth" mode switch remains Session G.

Why: implementation optionality. An ESP32 endgame **cannot use ANT+** (ANT is a Nordic/Garmin-licensed radio; ESP32 has BLE+Wi-Fi only), and a QZ contribution is BLE-only on mobile — so both attractive future targets run on the BLE path. The BLE protocol model of the Stages crank (advertisement, GATT table, CPS measurement flags/fields, control-point surface) is capturable for free during rides we're already doing. DIY ESP32 CPS peripherals that Zwift/Garmin accept already exist (kochcodes/ESP32_BLE_CyclingPowerMeter, kswiorek/ble-powermeter), so the eventual spoof-on-ESP32 step is proven feasible in principle.

Platform decision: **BLE capture runs on native Windows, not WSL.** Stock WSL2 kernels ship without the Bluetooth subsystem (CONFIG_BT/btusb absent); enabling it requires a custom kernel build plus usbipd-attaching the adapter (microsoft/WSL#12234, usbipd-win discussion #310) — not worth it when bleak's WinRT backend works natively. Verified live on the owner's ThinkPad (Intel Wireless Bluetooth): bleak 3.0.2 on Python 3.14 in `code/.venv-win`, 16 devices seen in a 6 s scan, full script lifecycle (scan→JSONL framing→clean exit) tested. ANT+ capture stays in WSL; same host clock means the two JSONL streams cross-correlate on iso_time.

Passivity rule (engineering discipline): the BLE capture NEVER writes to any characteristic — Control Point 0x2A66 is logged as present but untouched. No calibration pokes during A/D captures. Verified the script enforces this by construction (no write_gatt_char anywhere).

API verification (same discipline as openant): bleak 3.0.2 is a new major version; its installed source was introspected before relying on it — `BleakScanner(detection_callback=…)`, `discovered_devices_and_advertisement_data`, `BleakClient(address, disconnected_callback=…, timeout=…)`, `start_notify`, `read_gatt_char`, `services` all present with assumed signatures. CPS measurement decoder (0x2A63 flag-ordered optional fields) unit-tested on synthetic payloads: minimal, balance+crank-revs, torque variants, signed power, short payload, truncated-optional-fields.

Open empirical question for the first dual capture: does the Stages crank's BLE side advertise/accept a connection while the bike holds it on ANT+? If yes → full CPS model for free. If no → that's itself a Phase-0 finding (BLE only available when not bike-paired), which raises Session G's importance for the ESP32 route.

Will revisit when: the first parallel capture lands (does BLE advertise at all?), and at the Phase-0 report (whether BLE pairing mode + Session G gets promoted from optional to planned, which the ESP32 question drives).

## 2026-06-10 — Owner decisions: adv-only first BLE ride; ESP32 promoted to real target

Two scope decisions from the owner (recorded same-day):

1. **First dual-capture ride runs BLE in advertisement-survey mode only** (`--adv-only`) — no BLE connections to the cranks while the ANT+ sessions are being baselined. Connect-mode capture (GATT dump + CPS notifications) happens on a later ride once the survey confirms what's on the air. Rationale: cautious sequencing; removes even the theoretical risk of a BLE client changing crank behaviour during the first real ANT+ sessions.

2. **ESP32 is a real deployment target, not just optionality.** Session G (bike paired to cranks over BLE) is promoted from optional to a **planned** Phase-0 session — after Sessions A–F, so the ANT+ baseline is never disturbed mid-stream. The phase-0 report must now answer: does erg mode work fully with BLE-paired cranks, and what does the BLE calibration handshake (CP 0x2A66) look like? This also raises the eventual priority of a `StagesBleTarget` implementation and keeps the QZ-contribution door open. ANT+/Pi/Python remains the Phase-1/2 build path; ESP32 is the productisation direction to keep unblocked.

START-HERE and 03 updated accordingly.

## 2026-06-10 — Guided ride wizard for solo morning sessions

Decision: wrap the morning's Phase-0 sessions (C-0 → A → optional B) in a single interactive wizard (`code/scripts/ride_wizard.py`, WSL) rather than having the owner juggle commands mid-ride. Companion one-pager at `RIDE-CARD.md`.

Design points worth keeping:
- **Cues are data annotations.** Every on-screen cue (zero-reset moment, power-block boundaries, the 30 s coast) is recorded with planned offset + actual wall-clock time into an auto-generated `<capture>-notes.md` — this *is* the timestamp annotation the Session C/A docs require, produced as a side effect of guiding the rider.
- **Session A block design is protocol-driven, not training-driven**: warmup→200→260→330 W (≈90% Stages FTP 367), a 400+ W surge (exercises power MSB), a full 30 s coast (zero-power/zero-cadence event-count behaviour), then low-cadence (~60) and high-cadence (~95+) blocks for cadence byte spread. Targets stated in Stages watts since that's what the bike/app displays while riding.
- **C-0 verdict is computed by the wizard** (scan for page-0x01 records) immediately after the dry run, with PASS / INVESTIGATE messaging; INVESTIGATE does not block Session A.
- **Live remote analysis**: Claude (Windows session) reads the WSL captures via `\wsl.localhost\Ubuntu-24.04\...` while they're being written — proven earlier today against the smoke capture. The wizard's wrap-up directs the owner to just say "sessions done" in chat.
- **BLE survey auto-launch** uses WSL→Windows interop (`cmd.exe /c start <win-venv-python> 06_capture_ble.py --adv-only ...`), with a manual-fallback command on the ride card. Interop confirmed available in the owner's distro.

Two Python gotchas fixed during in-WSL shakeout (both would have crashed on ride morning; found by executing in the real environment, not by review):
1. `importlib`-loading a module containing `@dataclass` requires `sys.modules[name] = mod` *before* `exec_module` on Python 3.12 (`dataclasses._is_type` does `sys.modules.get(cls.__module__).__dict__`). Note `02_capture_assioma.py` gets away without this only because `01` has no dataclasses.
2. Don't name a `threading.Thread` subclass attribute `_stop` — it shadows the internal `Thread._stop()` that `join()` calls ("'Event' object is not callable").

Verified in the owner's WSL (Ubuntu-24.04, py3.12 venv): compile, module-loads, validator round-trip against the real smoke capture (REVIEW as expected), and a full `--preview` run of both cue schedules (exit 0).

## 2026-06-10 — Calibration-model direction: guided "protocol rides" to fit a meter-vs-meter regression

Owner direction (evening before the first full session): the power-source-consistency use case likely needs more than a constant scale factor, and the ride-wizard pattern just prototyped is the delivery mechanism for fitting whatever it does need.

- **Why a model, not a constant.** P = τ·ω. Strain-gauge slope error lives in the torque domain, so at a fixed power the Stages↔Assioma delta is expected to vary with cadence (different torque). The eventual mapping is anticipated to be a regression over features like power level, cadence, and torque (τ ∝ P/ω), possibly with drift/temperature terms — fitted, not assumed. This generalises open question #7: even if the SB20 applies no internal scaling, the meters' own disagreement may be load-shaped.
- **Mechanism: the calibration ride.** Reuse the guided-ride pattern as a designed experiment — a wizard-led grid sweep (sketch: 3 powers × 3 cadences, e.g. ~150/250/330 W × 60/85/100 rpm, 60–90 s per cell, coast separators between cells) generating clean torque spread. The cue timestamps double as cell labels, exactly as in the Phase-0 wizard.
- **Productisation angle (use case 3):** every SB20 owner's meter pair differs; the guided calibration ride becomes the onboarding flow — ride 12 minutes, the tool fits *your* pair's model, the fitted parameters become proxy config.
- **Data capture for the model:** the proper version is dual ANT+ capture — two sticks, one slave per meter, same ride, perfectly time-aligned JSONL on both sides (the owner has two sticks). Tomorrow's cheap first pass: a watch records the Assioma side while Session A captures the Stages side; the **30 s coast block at t=10:30 is the cross-correlation sync marker** between the FIT file and the JSONL (a distinctive zero-power notch in both streams).
- **Discipline:** no regression code and no grid-ride session design freeze until dual-meter data exists. Analyse tomorrow's first-pass deltas first; design the calibration ride from what they show.

## 2026-06-10 — Baseline workaround documented: dual-FTP hack and why it's a dead end

Current state of the art (owner's actual practice, pre-proxy): set FTP=330 on the watch (Assioma reference) and FTP=367 in the Stages bike control, so that Zone-N / %FTP workouts "roughly line up." This is a constant-ratio correction (367/330 ≈ 1.112) applied at the zone-definition layer — %FTP targets cancel a constant multiplicative error if both FTPs were measured on their own meters.

Why it frays (and why the proxy + calibration model is a different class of solution):
1. If the inter-meter delta is torque-shaped (see calibration-model entry above), the cancellation only holds near the power/cadence where the FTPs were measured — zones line up in the middle and drift at the extremes.
2. Absolute-watt workouts (TrainerRoad/Zwift watt-specified intervals) bypass FTP scaling entirely and break.
3. Cross-meter analytics: TSS/IF/CTL and ride files are computed in two different "currencies" with no exchange rate — indoor and outdoor files aren't comparable.
4. It cannot touch anything that reads raw watts from the bike's broadcasts.

The proxy collapses this to one FTP / one currency (bike's erg loop runs on Assioma-derived watts), and the per-pair fitted calibration replaces the constant with a function. Keep this story for the README / phase-0 report framing: "what owners do today, and why it can't be fixed at the app layer."

## 2026-06-14 — First live ride: SUCCESS, plus a hard-won WSL/USB/process operations lesson

Outcome (the wins are in the next entry): the morning's guided ride captured the calibration handshake and a full Session A. But it cost ~25 min of bike time to WSL/USB/process gremlins that should never recur. Full operational write-up: `code/findings/wsl-capture-runbook.md`. Summary of root causes + fixes:

1. **Zombie capture held the stick → "Resource busy" on every retry (the big one).** openant's worker thread is non-daemon, so when a capture failed *after* Node() started (at channel assign), the process HUNG instead of exiting, keeping the USB device open. Diagnosed with `fuser`/`lsof` on the device node; fixed by killing the exact PID. **Permanent fix applied:** `01_capture_stages.py main()` now wraps `setup()` and `os._exit(2)`s on failure, so a setup error force-releases the USB device immediately.
2. **`CHANNEL_IN_WRONG_STATE` (err 21) at session boundaries.** openant's teardown in WSL doesn't cleanly reset the chip (the cosmetic `node_stop_error: Entity not found` = libusb attach_kernel_driver failing). Clears on a fresh-process retry; the new `run_capture.sh` retries automatically.
3. **USB perms root-only after `usbipd attach`** (udev rule doesn't fire on the cross-distro shared attach) → one `sudo chmod 666 /dev/bus/usb/BBB/DDD`. Delivered via clipboard so the rider just pastes.
4. **`pyusb dev.reset()` is harmful here** — re-enumerates the FTDI-based stick and causes transient busy/driver-claim churn. Don't. Kill zombies + retry instead; or usbipd detach/reattach for a hard reset.
5. **Self-kill footgun:** `pkill -f`/`pgrep -f` with an unanchored pattern matches the controlling `bash -c` shell (its argv contains the pattern string). Killed our own shell twice (exit 15). **Rule:** kill by exact PID, or anchor the pattern (`'ride_wizard\.py$'`), or `grep -v $$`.
6. **Interactive terminal input was unusable** (`read failed 5: I/O error`; focus bounced to the chat window) → the wizard's `input()` prompts never fired. **The working model:** agent drives captures from outside via `wsl.exe -e bash -c`, launches detached (`nohup setsid ... </dev/null &`), polls the JSONL, and guides the rider through chat. This is now the documented default for assisted rides.
7. **Reading WSL files from Windows:** PowerShell `\wsl.localhost\<distro>\...` works; git-bash `//wsl.localhost/...` does not. Console Unicode crashes cp1252 → `PYTHONIOENCODING=utf-8` or ASCII-only output.

Artefacts added: `wsl-capture-runbook.md` (symptom→cause→fix catalog + pre-ride checklist + golden-path launch), `code/scripts/run_capture.sh` (robust launcher: release-stick + detached + retry, no self-kill), and the `os._exit` hardening in the capture script. Net: a 60-second pre-flight + the launcher would have avoided essentially the entire mess.

## 2026-06-14 — Ride RESULTS: calibration handshake captured, Session A PASS, device IDs resolved

The data wins from the first live ride (device 62144 = Stages combined/left crank):

- **C-0 PASS — the calibration handshake is captured on air.** The crank broadcasts its zero-reset reply as page 0x01, `kind="broadcast"` (NOT acknowledged — confirms the reframed C-0 model). Captured `calibration_id=0xAC` (manual-zero success) with offsets **903** (raw `01acffffffff8703...`) and **-950** (raw `01acffffffff4afc...`), exactly matching the app's displayed 903 / 950. The single L-crank ID alternates BOTH crank offsets (left +903, right -950; app shows right as magnitude). **This de-risks the whole proxy: passive ANT+ sniffing sees the calibration exchange, and we now have the exact bytes to emit: `01 AC FF FF FF FF <offset_LE_signed>`.** First two C-0 attempts missed it purely on timing (rider still pedalling at window end); third attempt with the reset done early caught all 8 records.
- **Session A — validator VERDICT: PASS.** 2,575 broadcasts over 898 s, clean session_end, ext-messages enabled. Page mix 0x12×1360 / 0x10×681 / 0x13×449 / commons. Power 0→**569 W** peak; full guided sweep incl. the calibration-critical low-vs-high cadence pair (~210 W @ 60 rpm vs ~230 W @ 100 rpm). `manufacturer_id=69` re-confirmed.
- **Device identity resolved (via the BLE survey):** BLE advertised `ASSIOMA17039L` at addr E6:20:90:8C:F3:FE → **17039 = Assioma left pedal**; therefore **62144 = the Stages crank** (the one we captured). Also saw `Stages 4963` advertising Cycling Power (0x1818) + a Stages custom service.
- **BLE finding for the ESP32 path:** the Stages crank's BLE side **advertises Cycling Power while the bike is ANT+-paired** — promising for a future BLE-server spoof. (Survey was adv-only/passive; connection-level CPS is a later ride.)
- **New protocol detail:** when the rider stops, the Stages crank **latches its last instantaneous power (~416 W) rather than reporting 0 W** — held for >60 s. Relevant to how the proxy should behave on coast, and means "zero-power samples" need the *Assioma* (watch) side, which does zero.

Pending: rider's watch FIT (Assioma side, lap-marked at ride end) for the dual-meter calibration comparison — the coast notch + end lap are the sync markers. Files to commit: C0-ack-dryrun-20260614-164426 (the one with the 8 cal pages), A-stagesL-steady-20260614-165737, and the BLE adv survey(s).

## 2026-06-14 — CALIBRATION RESULT: the meter offset is torque/cadence-dependent (not constant)

First dual-meter analysis, aligning Session A (Stages, device 62144) against the
Assioma watch FIT (`A-assioma-watch-20260614.fit`), 585 matched active seconds.
The coast notch confirmed sync; ~10 s watch/PC clock skew noted (doesn't bias the
steady blocks). Stages = 62144, Assioma reference.

- **Overall Stages/Assioma ratio ≈ 1.085** (Stages reads ~8.5% high). The owner's
  dual-FTP workaround assumes 367/330 = 1.112, i.e. it slightly over-corrects on average.
- **Flat across power** (0.95–0.44 kW bands all ~1.07–1.12) — no strong power dependence.
- **Strongly cadence-dependent at fixed power (the headline).** Matched on Assioma 170–260 W:
  - 60 rpm (high torque): ratio **1.134** (n=83)
  - 70–85 rpm: 1.063 (n=15)
  - 100 rpm (low torque): ratio **1.053** (n=137)
  So at the same power the disagreement is ~13% grinding vs ~5% spinning. This is exactly
  the P=τ·ω strain-gauge-slope signature predicted in the calibration-model entry. **Confirms
  the correction must be f(power, cadence)/torque, not a constant** — a single ratio is right
  at one cadence and wrong elsewhere, which is why the dual-FTP hack only ever felt "roughly" right.
- Caveat: one ride, blocks at different times (minor drift confound, but 8 points >> 3-min thermal
  drift and moves in the predicted direction). Needs a dedicated power×cadence grid ride to fit f cleanly.

## 2026-06-14 — Drop the watch: capture both meters directly over ANT+ (one stick, two channels)

Decision: future dual-meter / calibration captures use **two slave channels on one ANT+ stick**
(openant Node supports 8 channels; ANTUSB2 confirms 8), not a watch FIT export. Owner's call —
"our app can listen to both."

Why it's strictly better than the watch:
- One machine clock for both streams → sample-aligned, no UTC/AEST conversion, no ~10 s skew,
  no coast-notch hunting. Removes the biggest source of noise in the calibration regression.
- No export step (no Garmin Connect / FIT round-trip).
- The Assioma's cycling dynamics (TE/PS/balance, page 0x13) come over ANT+ anyway.
- **It IS the proxy's input path** — Phase 2 is "listen to the Assioma (channel 1) and re-broadcast
  as the Stages crank", so this is real architecture progress, not throwaway capture tooling.

Implemented as `code/scripts/07_capture_dual.py` (reuses 01's decode_page; tags each record with
`source`; carries the os._exit-on-setup-failure hardening from the runbook). Not yet hardware-tested
— shake out on the next ride via the run_capture/runbook procedure. The guided **calibration ride**
(power×cadence grid) now becomes fully self-contained on one stick: ride the grid, fit f(power,cadence),
done — and the same flow is the onboarding step for other owners (use case 3).

## 2026-06-14 — De-scope: the calibration model is OFF the critical path for the core goal

Goal, stated crisply by the owner: "Stages erg mode runs correctly using my Assiomas (or another pedal meter) as the source" — i.e. erg target X W => I produce X W *as measured by my Assiomas*.

Key architectural clarification (prevents over-building):
- The proxy **replaces** the crank power the SB20's erg loop reads with **live Assioma watts**. The erg loop then closes on the Assioma number by construction: set erg to 350 -> bike adds resistance until the number it sees (= Assioma) reads 350 -> you produce 350 Assioma watts. The Stages cranks leave the loop entirely.
- Therefore the **Stages-vs-Assioma calibration model is NOT needed** for the goal. Today's torque-dependent offset (1.134@60rpm vs 1.053@100rpm) is the *diagnosis* of why the current cranks-in-the-loop setup mis-trains and why the dual-FTP patch can't work (it constant-corrects a torque-shaped gap). The proxy *eliminates* that gap rather than modelling it. The power×cadence grid ride is now **optional / research**, not required for delivery.
- The model would only be needed for the inferior "keep Stages as source, correct it to read like Assioma" approach. Not our path.

The ONE thing that could complicate the direct feed — **open question #7: does the SB20 internally rescale crank power before erg/display?** Prior is "no" (Stages docs: the bike "rebroadcasts the same information as the left power meter" => pass-through). If it does scale, it's a single bike-specific factor to compensate, NOT a per-meter model. **Test:** capture the bike's own FE-C power broadcast alongside the crank, same clock; if bike_fec_power == crank_power instant-by-instant, #7 is closed.

Next-session objectives (one structured ride, one same-clock multi-source capture):
1. **#7 verification** — capture Stages crank (62144, 0x0B) + Assioma (17039, 0x0B) + bike FE-C (0x11, wildcard) together; compare FE-C vs crank power.
2. **Calibration grid (research/optional)** — power×cadence cells to map the offset surface and find empirically how few cells are needed (likely collapses to ~1-D in torque P/cadence). Include a **sprint** corner (owner's 800-1000W+ range) to probe the high-torque/high-power extreme.
Tooling built for it: `07_capture_multi.py` (multi-source incl. FE-C), `08_analyze_grid.py` (ratio surface + torque-vs-cadence-vs-power fit + cell-count guidance), `CALIBRATION-RIDE-CARD.md` (run sheet, agent-driven per the runbook).

## 2026-06-14 — Crank length is a measurement confound (must control before the calibration ride)

Owner flagged that the SB20 has adjustable cranks (165–175 mm pedal holes) and the app is told the length. This is load-bearing for the calibration comparison:

- Both meters compute power as **P = F·ω·L**, each applying its OWN configured crank length: the **Stages crank** uses the length set in the **SB20 app**; the **Assiomas** use the length set in the **Favero app**. The pedals thread into the SB20's adjustable arms, so the *actual* length is whatever holes are in use.
- **The Stages/Assioma ratio ≈ ratio of the two configured lengths** (actual length cancels in the ratio). So a config mismatch directly multiplies the measured ratio: 172.5 vs 170 → ~1.5%; 175 vs 170 → ~2.9%. Part of day-1's overall 1.085 could be pure config artifact, not meter behaviour.
- It's a **constant** scaling, so it does NOT explain the torque-dependence (1.134@60 vs 1.053@100 — real strain-gauge-slope difference). But it can shift the overall ratio.
- **Pre-ride control:** make physical holes = SB20-app length = Favero-app length, all three equal, before session 2. Added to `CALIBRATION-RIDE-CARD.md` bike-setup (step 1). Worth capturing the three current values first — a current mismatch is itself a finding.
- **Proxy implication:** once spoofing, the Stages crank leaves the loop, so the SB20's crank-length setting becomes moot. Only the **Favero/Assioma** crank-length config must be correct (so the Assioma watts we feed are accurate). Crank length is therefore a *measurement* confound, not a proxy problem. (Note: the SB20 may push a crank-length config to the crank during pairing — our spoof can capture and ignore it.)

## 2026-06-14 — Stages app UI findings: BLE-source option + editable meter IDs (screenshots)

Owner captured screenshots of the Stages app's POWER METERS tab for the SB20
(*Stages Bike 0105*). Filed under `code/findings/screenshots/stages-app/` with a
README documenting each. Three findings with architectural weight:

1. **BLE can be the power-meter source, not just ANT+.** The app has a "Pair with
   Bluetooth — use the power meter Bluetooth connection with the Stages Bike"
   toggle. So the SB20↔meter link is not ANT+-only → opens a **pure-BLE proxy on
   an ESP32** (no ANT+ stick / no Pi). Aligns with the ESP32 BLE-CPS prior art
   just added to `06-prior-art-and-references.md`. Open: does the bike's BLE pair
   accept *any* CPS peripheral or only Stages-branded? Hardware test needed.
2. **The bike stores the meter IDs it listens for** (`Stages 62144 : 4963`,
   crank length 165 mm). A spoof can target chosen IDs → possibly **keep the real
   Stages meters' batteries in** and still inject. Try different IDs once spoof is real.
3. **Refuted shortcut:** owner already tried pointing the bike at the **Assioma's
   native ANT+ IDs** — **did not work**. ID-matching alone is insufficient; need a
   full spoof (correct manufacturer ID, page formats, whole Bike Power contract),
   not just channel/device IDs. Reinforces Phase-0-capture-first.

Recorded values (don't treat as fixed — zero-offset drifts per calibration):
meter `Stages 62144 : 4963`, crank 165 mm, zero-offset Left 903 / Right 951.
Owner action: print physical stickers with the meter ID(s) so they aren't lost.

## 2026-06-14 — Crank-length settings on ride day + a testable prediction for session 2

Owner reported the day-1 crank-length config:
- Physical pedal holes: **172.5 mm**
- Assioma (Favero app): **172.5 mm** — correct, matches physical.
- Stages (SB20 app): **165 mm** — WRONG, 7.5 mm too short (165 is a common default, likely never updated).

Direction/magnitude: a force-based crank meter computes P ∝ force × length × cadence, so a too-short configured length makes it **under-report**. Stages at 165 vs actual 172.5 under-reports by 165/172.5 = 0.957 (~4.3%) relative to a correctly-configured Stages. This *deflated* the day-1 measurement.

**Prediction for session 2 (after setting SB20 Stages length 165→172.5):** the overall Stages/Assioma ratio should rise from the measured **1.085** to **~1.134** (= 1.085 × 172.5/165). Applying the same constant ×1.045 to the torque test: 60 rpm 1.134→~1.19, 100 rpm 1.053→~1.10 — i.e. Stages reads ~10–19% high vs Assioma once lengths match, cadence-dependent. The length fix is a constant scale: it shifts the level but preserves the torque-dependence shape. **This is a clean experiment** — confirming ~1.13 validates the crank-length model and cleanly separates config artifact (~4.5%) from the real meter-slope difference (~13%).

Action before session 2: SB20 app Stages crank length 165 → 172.5 (Assioma already correct). Reinforces "Assiomas are the trustworthy reference; the old Stages cranks read high."

Opened for the #7 capture: determine whether the SB20 consumes the crank's **power** (watts, length-independent — then the SB20 length setting is moot for the spoof) or computes power from crank **torque** × its configured length (length-dependent — then the SB20 length setting would scale a torque-based spoof). The multi-source capture (crank 0x10 power vs 0x12 torque vs bike FE-C output) should disambiguate.

## 2026-06-14 — Correction: the Stages 165mm setting was DELIBERATE (a manual gain hack)

Correcting the prior entry's guess ("likely never updated"): the owner had set the Stages crank length to 165 (vs physical 172.5) **on purpose** — using crank length as a manual gain knob to make the over-reading Stages under-report and drop toward the Assioma. It's the same class of correction as the dual-FTP hack, one layer lower (at the crank-power computation instead of the zone/FTP layer). The owner has effectively been stacking TWO constant-gain corrections (crank-length fudge ~−4.3% + dual-FTP) to approximate the gap.

Implications:
- The day-1 measured 1.085 is the *partially-corrected* Stages (the 165 fudge already removed ~4.3%); the honest-length gap underneath is ~13% (cadence-varying 10–19%).
- A constant crank-length gain can only be exact at one cadence — it over/under-corrects across the cadence range, the same wall as dual-FTP. Confirms (from the owner's lived experience) that the correction is torque-shaped and can't be closed with constants at the app layer.
- Session-2 design unchanged: set crank length to the honest 172.5 on both meters to measure the true difference; day-1@165 (1.085) vs session-2@172.5 (predicted ~1.13) is the clean A/B that validates the crank-length-as-gain model.
- Strongest case yet for the proxy: it retires BOTH manual hacks at once (crank-length fudge AND dual-FTP), because the Assiomas drive the erg loop directly with nothing left to hand-tune. (Good blog material — "I'd been hand-fitting a constant to a torque-shaped curve, in two places at once.")

## 2026-06-14 — Calibration ride uses ERG mode (owner's workflow), not level mode

Revising the earlier "level mode" plan. Owner will run the grid in **ERG**: set a power setpoint per interval and report the actual value in chat (the bike's erg doesn't do 1 W steps, so they set the nearest and tell Claude the setpoint, which becomes the cell label).

Why erg is the better design here:
- Erg **pins power** (bike controls resistance to hold the setpoint as measured by the Stages cranks), so within a cell power is rock-steady and the rider only manages **cadence**. Cleaner constant-power cadence sweep = the cleanest possible torque test; less rider variance than holding power+cadence by feel in level mode.
- Data structure: at a fixed setpoint the Stages sits ≈ setpoint, the **Assioma floats to setpoint ÷ ratio**; sweeping cadence makes ratio(cadence) show up directly in the Assioma reading.
- `08_analyze_grid.py` is unchanged — it bins continuously by (power, cadence), so erg-clustered cells analyse the same.

Protocol: 3 erg setpoints (~150/250/330 W, actuals reported) × cadence sweep (60→80→100 rpm, ~60 s each, allow a couple seconds to re-stabilise after each cadence change). **Sprints switch to LEVEL/resistance mode** (erg caps at the setpoint, can't sprint): firm resistance, 4× ~12 s all-out (800–1000 W+), 90 s easy between. CALIBRATION-RIDE-CARD.md updated accordingly. Open: confirm the bike's erg step size near 150/250/330 so Claude cues hittable numbers.

## 2026-06-14 — BLE/ESP32 path planned; huge reuse from cauldnz/raedian-probe

Owner pointed at their `cauldnz/raedian-probe` project (a BLE recon toolkit + ESP32 bridge for a Raedian/Zeekr EVSE). Reviewing it: ~80% of the SB20 BLE/ESP32 substrate already exists and is directly reusable. The SB20 proxy is essentially a second instance of the same pattern.

Reusable assets identified:
- **`esp32_bridge_spec.md`** — ESP32-C3 + OLED + **NimBLE BLE-client** bridge, decoupled control loop, OTA, OLED/QR onboarding, hero-SKU costing. Our proxy maps onto it 1:1 (their "charger half = BLE client to the wallbox" → our "crank-peripheral + Assioma-central half"; their control loop → relay Assioma watts + answer calibration). Reuse the firmware scaffold wholesale.
- **`sniffer_setup_runbook.md` + `pcap_analyze.py`** — a working nRF52840 → Wireshark → JSON passive sniffer pipeline. This is the gold-standard Session G capture (bike↔crank link + bonding); already solved. (Supersedes my "go buy a sniffer" suggestion.)
- **Staged recon toolkit** (`scan`/`enumerate`/`listen`/`correlate`/`probe_write`) — a more complete version of our `06_capture_ble.py`, same "observe exhaustively before writing" philosophy. Mirror its structure; port `probe_write` for the guarded Control-Point calibration write.

Technical confirmations:
- **Dual-role BLE feasible:** NimBLE on ESP32 supports concurrent central+peripheral (~9 connections). The proxy = central→Assioma + peripheral→bike simultaneously.
- **Board strategy:** Waveshare ESP32-C6-LCD-1.47 (~A$18) for dev + on-device calibration UX; ESP32-C3 + 0.96/1.3" OLED hero SKU (the bridge-spec recipe; owner already has C3+OLED boards).
- **Assioma exclusivity is fine:** its BLE goes to the ESP32, its ANT+ stays free for the owner's watch (dual-broadcast, confirmed day-1).

Deliverables: `11-ble-and-esp32-path.md` (A: strategy/architecture/reuse) and `code/findings/session-G-ble-capture-spec.md` (B: the capture checklist, active + passive + the erg-works gate), each item mapped to the ESP32 build need.

Next-session BLE plan (after the ANT+ #7 + grid): (1) active BLE recon of the crank while still ANT+-paired — needs a guarded Control-Point write added to `06_capture_ble.py` (the one tooling prep item); (2) the erg-works-on-BLE-cranks GATE (flip to "Pair with Bluetooth", test erg — no sniffer needed, this is the go/no-go); (3) passive sniff IF the nRF dongle is flashed & ready (else a focused follow-up). Sequencing is mode-exclusive: ANT+/active-recon first (crank BLE-free), then BLE-crank mode for the gate + sniff.

Open prep items before the BLE session: (a) add the guarded Control-Point write to 06_capture_ble.py; (b) confirm whether the nRF sniffer dongle is flashed per the raedian-probe runbook.

## 2026-06-14 — nRF dongle MIA → Session G Part B becomes ESP32 impersonation capture

The owner's nRF52840 sniffer is misplaced. Considered building an ESP32 passive sniffer, but **true passive connection sniffing on ESP32 is not realistic** — following an established connection's channel-hop/timing/whitening is what the nRF firmware is purpose-built for; the ESP32 radio can sniff advertising but not reliably reconstruct a live connection.

Better path (filed as `cauldnz/raedian-probe#1`): **capture by impersonation**. The ESP32 *is* the fake peripheral (Stages crank / EVSE charger); the real central (SB20 bike / phone app) connects to it; the ESP32 logs the central's service discovery, CCCD subscribes, characteristic writes (calibration/control-point), SMP/bonding, and connection params. This gets the central's exact behaviour (the hard-to-get half), and crucially **it is the proxy's first real build step** (the peripheral side of esp32_bridge_spec.md) — not a detour. The real peripheral's responses still come from Part A active recon. nRF passive sniff kept as a fallback/cross-check if a dongle turns up.

Consequences for Session G:
- **Part B reframed** from "passive nRF sniff" to "ESP32 impersonation capture (preferred) / nRF sniff (fallback)". Same captured items (bonding, conn params, bike's calibration write, custom-service behaviour); different method.
- **Next-session BLE scope = Part A (active recon) + Part C (erg-works gate)** — both doable with current kit; together they give the go/no-go and most of the impersonation surface. **Part B waits on the impersonation firmware** (raedian-probe#1) or a new nRF dongle; not a blocker for go/no-go.
- Shared infra: the impersonation firmware benefits both projects and seeds each one's device firmware.

## 2026-06-14 — Built: guarded Control-Point write in 06_capture_ble.py (Session G Part A ready)

Prep item (a) done. `06_capture_ble.py` now has an opt-in `--control-point <ops>` mode (passive by default otherwise): after connecting + GATT dump + static reads, it enables Cycling Power Control Point (0x2A66) indications and runs the requested ops once each — guarded single writes, each logged, no blind loops (mirrors raedian-probe/probe_write.py). Op map covers offset-compensation (0x0C, the BLE zero-reset → offset in the indication, the analogue of the ANT+ 0xAC/903), the read-only requests (sensor locations, crank length, sampling rate, factory cal date), and enhanced offset comp.

Notable: the decoder reads back **request-crank-length (0x05)** in mm — so on the BLE recon we can directly read what the Stages BLE side reports as its configured crank length, a clean cross-check of the deliberate 165-vs-172.5 fudge. Verified (Windows venv, no hardware): compile clean; CP decoder unit-tested on offset 903 (matches ANT+), offset −950, crank length 172.5 & 165, sensor locations, unsupported-op; adv-only lifecycle still frames clean (no regression). offset-compensation prints a "keep cranks still" warning and needs a stationary crank for a valid result; an auth/encryption failure on the write is itself a finding (bonding required).

Remaining BLE prep item: (b) the ESP32 impersonation firmware for Part B (raedian-probe#1) — or a replacement nRF dongle. Not a blocker for next session's A+C.

## 2026-06-15 — Quick session: #7 ANSWERED (pass-through), calibration ~1.13, BLE topology mapped

Owner tired post-training-ride → a short two-capture session instead of the full grid. High value:

**Capture A** (`QUICK-multi-20260615-064037.jsonl`, ~5 min, Stages 62144 + Assioma 17039 + bike FE-C **105**, cadence 47–109):
- **Open-question #7 RESOLVED → pass-through.** `bike_FEC / crank power = 0.997` mean over 149 matched seconds (sd 0.11 is per-second timing jitter from varied riding, not a real factor). **The SB20 does NOT rescale crank power.** => feeding the bike Assioma watts makes erg targets land on true Assioma watts, with no bike-scaling compensation. The biggest open risk for the proxy is cleared. (A steady-state hold next time would tighten the sd, but the mean is decisive.)
- **Calibration spot-check:** Stages/Assioma ≈ **1.124 mean / 1.134 median** (sd 0.19 — noisy, varied ride not held cells). This is ~the **172.5 prediction** (day-1@165 was 1.085; predicted @172.5 ≈ 1.134). Strongly suggests the crank length is now 172.5 — **confirm the app setting** to lock it in. Drivers this ride: power/torque R²≈0.42, cadence R²≈0.00 (small/noisy sample; don't over-read vs day-1's cadence signal — needs the held grid).

**Capture B** (`G-stagesL-ble-recon-20260615-064641.jsonl`): connected to the SB20 over BLE and mapped the topology. **Correction to the plan:** the two "Stages" BLE advertisers are BOTH the bike, not the crank:
- `Stages 4963` = bike CPS power broadcast (0x1818 + Stages custom svc d445fe01-…).
- `Stages Bike 0105` = bike FTMS trainer (0x1826: control point 0x2AD9, indoor-bike-data 0x2AD2, feature 0x2ACC…) + CSC (0x1816). ← this is what `--name Stages` connected to (higher RSSI).
- Device Info: manufacturer "Stages Cycling", model "SB20", fw 1.1 / sw 1.12.4+3792, serial H0512210105.
- Two Stages custom services on the trainer (0c46be5f-…, 0c46beaf-…) + **Nordic DFU (0xfe59)** → the SB20's BLE runs on a Nordic nRF chip (relevant to the ESP32 path).
- **The real crank's BLE does NOT advertise in ANT+ mode.** So crank-impersonation recon (Session G Part A) requires the bike in "Pair with Bluetooth" mode after all — earlier assumption ("Stages 4963 = the crank, reachable while ANT+-paired") was WRONG. The `--control-point` crank-length read failed here because we were on the bike's FTMS device, not a power-meter/crank. Update session-G spec accordingly.
- Bonus surface captured for free: the bike's FTMS trainer GATT (its OUTPUT/control interface) + Nordic DFU.

Files committed: QUICK-multi (A) + G-stagesL-ble-recon (B). Next: confirm crank-length app setting; fix the session-G spec's "Part A while ANT+-paired" assumption; the proxy approach is validated by #7.

## 2026-06-15 — BLE recon of BOTH meters: crank reachable in ANT+ mode; both at 172.5; digital-twin vision

While the devices were awake post-ride, connected to each over BLE by address. Big findings:

**Crank length confirmed at 172.5 on BOTH meters, read directly off the hardware over BLE:**
- Assioma (req-crank-length): `2005015901` → 172.5 mm, success.
- Stages crank (req-crank-length): `20055901` → 172.5 mm (the crank omits the result byte — a non-standard CP framing; decoder updated to use the trailing 2 bytes, robust to both). The owner's 165→172.5 app change is verified on the crank itself. **So today's ~1.13 ratio is the honest meter difference at matched length; the fudge is fully removed.**

**The Stages crank IS reachable over BLE in ANT+ mode — corrects the prior wrong conclusion.** Last session I'd connected to the bike's FTMS device ("Stages Bike 0105") by name-filter and wrongly concluded the crank BLE needs BLE-crank mode. Targeting by address shows the crank advertises as **"Stages 62144"** with full CPS:
- Device Info: Stages Cycling / model **SPM2** / serial 11821518 (matches ANT+) / fw 1.8.2.
- Services: GAP, GATT, **fe01** (Stages custom: char d445fe02 write+notify, d445fe03 notify), **1818 CPS**, 180f battery, 180a DIS, **fe59 Nordic DFU** (crank runs on a Nordic chip).
- CPS Feature 525067; CPS Measurement flags 0x2F (balance + torque + crank revs), e.g. power 135 W.
- Control point: crank-length read works (non-standard framing); sensor-locations & factory-cal-date = op_code_not_supported.
- **Battery 14% — LOW. Put a fresh CR2032 in the crank before the next real session** (could die mid-ride; low battery can also skew power).

**Assioma BLE:** Favero Electronics / model Assioma / serial 17039.013.118 / fw 06.24 / hw 04.01; CPS Feature 1118729; battery **73%** (healthy); custom service 0x0001. **Both pedals advertise separately** — `ASSIOMA17039L` (left, combined L+R — the proxy's input target) and `ASSIOMA22428R` (right). So crank recon (Session G Part A) is doable in ANT+ mode after all — just target by address/name, not the generic "Stages" filter.

**Both crank and Assioma run on Nordic nRF (DFU service)** — as does the SB20 bike — relevant to the ESP32/BLE path.

Captures: `G-crank62144-ble-*.jsonl`, `G-assioma17039-ble-*.jsonl` (+ the earlier `G-stagesL-ble-recon` = the bike FTMS device). These are the first **digital-twin** source material.

**Strategic ideas recorded (owner) → `12-digital-twins-and-capture.md`:** (1) digital twins of the bike/meters to develop+test the proxy without riding, and an open twin library for other devs; (2) a structured capture-and-upload mode so beta testers with other gear grow the twin + calibration libraries (multi-brand support). Key insight: these are one pipeline — capture (structured) → twin (replay) → test — and the `raedian-probe#1` impersonation firmware is both the capture tool and the embedded twin. Future work, not a detour: same captures/format/firmware serve the proxy and the twins.

## 2026-06-15 — BLE zero-reset captured on both meters (no bonding needed; ~0 offset)

Owner offered a zero-reset; triggered it over BLE via `06_capture_ble.py --control-point offset-compensation` and captured the response directly (cranks/pedals held still & unloaded).

- **Stages crank (BLE):** `200c010000` → Start Offset Compensation, **success, offset 0**.
- **Assioma 17039L (BLE):** `200c01ffff` → **success, offset -1** (essentially perfectly zeroed — typical Favero stability).

Key findings:
- **Neither meter requires bonding/pairing for a control-point write** — the calibration write succeeded straight up on both. Big positive for the ESP32 impersonator/twin and for any tool that calibrates over BLE: no SMP/bonding handling needed (at least for calibration).
- **BLE response format for the zero-reset:** `20 0C 01 <offset_sint16_LE>` (success). This is the BLE analogue the impersonator must reproduce when the bike issues a BLE zero-reset.
- **ANT+ vs BLE offset semantics differ:** ANT+ page-0x01 calibration_data was 903 (crank) this morning; BLE Start Offset Compensation returns ~0. Likely different fields/representations (ANT+ = stored raw zero-offset; BLE = residual/result of compensation ≈ 0). Worth confirming against the Stages/CPS spec for the twin, but not blocking.
- Side effect (benign): the crank & Assioma are now **freshly zero-offset calibrated** — a normal, safe operation (same as the app's zero-reset, done unloaded & still).

Captures: `G-crank62144-ble-zero-*.jsonl`, `G-assioma17039-ble-zero-*.jsonl` — added to the digital-twin source material (now we have advert + GATT + reads + CPS measurement + calibration response for both meters over BLE).

## 2026-06-14 — App-survey screenshots: source→target mapping confirmed; crank-length authority is an open question

Filed a screenshot survey of the three relevant apps under
`code/findings/screenshots/` (`stages-app/` = Stages Cycling bike app,
`stages-power-app/` = StagesPower crank-meter app, `favero-assioma-app/` =
Favero app), each with a README. Cross-cutting insights:

1. **Source→target mapping is now concrete (single ANT+ channel).** The Favero
   app's *Compatibility with other apps* offers **Unified channel L** (both
   pedals' data combined, sent from the **left** pedal, only L paired) vs **Dual
   L/R**. Owner runs **Unified channel L**. The SB20's Stages left crank likewise
   combines+rebroadcasts L+R. So the proxy reads **one** channel — Assioma **L
   17039** (combined watts) — and spoofs **one** master — Stages left crank
   **62144**. No need to read/emit the right side. Confirms the single-source design.

2. **The Stages crank meters' full identity is captured** (the spoof target):
   ANT+ IDs **L 62144 / R 4963**, device type **STAGES SMART**, firmware 1.8.2,
   per-meter slope/temp-slope/DPOT. We inject finished watts so we don't need the
   calibration constants, but the IDs + device type + page contract are the clone target.

3. **Crank-length authority is an OPEN QUESTION (extends the crank-length thread).**
   The StagesPower crank-meter app stores **crank length = 165.0 mm** *in the
   meter*, while the Stages Cycling bike app now shows **172.5 mm**. These are two
   distinct settings. Unknown which governs the watts the SB20 actually consumes.
   This bears directly on the day-1 (1.085) vs session-2 (~1.134) ratio
   prediction — if the meter's own 165 is authoritative, the bike-app change to
   172.5 may not move the consumed watts at all. **Test:** change one length at a
   time and watch whether the broadcast/consumed watts shift. (Moot once spoofing
   — the crank leaves the loop — but it explains the measurement history.)

4. **Hardware inventory:** owner has **two** Assioma DUO pairs — A (in use): L
   17039 / R 22428, fw 06.24; B (spare): L 29064. Assioma power scale factor is
   0%/factory (Assioma = ground truth, no source-side correction). HR strap 56954-1.
   Recorded so the spare IDs aren't lost.

## 2026-06-15 — Reconciling the screenshots with today's BLE reads (corrections + the crank-length experiment)

After merging the app-screenshot survey (`code/findings/screenshots/`), three reconciliations:

1. **CORRECTION to my 2026-06-15 BLE-recon entry: "Stages 4963" is the RIGHT CRANK, not "the bike's CPS power broadcast".** The StagesPower discovery + device-details screenshots show the crankset is **L 62144 / R 4963** (linked pair, device type STAGES SMART). So the BLE advertisers are: `Stages Bike 0105` = the bike (FTMS trainer, also seen by the power-meter app), `Stages 62144` = left crank, `Stages 4963` = right crank. The proxy still spoofs only the **left (62144)** combined master.

2. **Crank-length authority — new data point sharpens the open question.** The branch correctly flagged: StagesPower stores the meter's own crank length = **165 mm** (06-14), while the bike app shows **172.5 mm**. My BLE `request-crank-length` read of the crank **today returned 172.5 mm** (`20055901` → 0x0159 → 172.5). So either (a) the meter's firmware crank length was changed 165→172.5 since 06-14 (consistent with the owner's "changed to 172.5", and then the 1.085→1.13 shift IS the crank-length change), or (b) the BLE Control-Point crank length is a different field from the StagesPower "Crank Length" parameter (which sits beside slope/DPOT as the power-computation constant). **These give opposite conclusions about whether today's 1.13 confirms the prediction.** My earlier "prediction confirmed" was premature. **Resolve by the experiment the branch proposed:** change one length at a time and watch whether the crank's broadcast watts move (capture crank 0x10 power before/after). That definitively identifies which knob drives consumed power. (Moot once spoofing — crank leaves the loop — but it's the difference between "1.085→1.13 is the crank-length fix" and "something else moved".)

3. **"Pair with Bluetooth" toggle located + state confirmed:** in the bike app's Power Meters tab (`stages-app/power-meters-tab-172mm.png`), currently **OFF** (= ANT+ crank mode). Flipping it ON is the Session G Part C step (BLE-crank mode for the erg-works gate). Good to know exactly where it is and that the crank's BLE is reachable even with it OFF (today's recon).

Net: the screenshots are high-value — they captured the full crank spoof-identity, confirmed the single-channel source→target mapping (Assioma Unified-channel-L 17039 → Stages L 62144), inventoried the spare hardware, and surfaced the two-crank-length subtlety that makes the calibration history a hypothesis-to-test rather than settled.

## 2026-06-15 — Phase 1A built: TX side + software loopback works (digital twins)

First proxy code written (after the forward plan + the unit-testing rule). Phase 1A is
**code-complete and the software loopback passes** — the whole pipeline runs end-to-end with
**no ANT+ stick and no bike**. 35 unit tests, ruff clean, CI on every push.

1. **ANT+ page codec** (`src/sb20proxy/ant/pages.py`): `encode_page`/`decode_page`, exact inverse
   pair over all 7 Bike Power pages. Reserved-byte fills (all 0xFF) were **verified against the real
   captures** (3,209 records), not the spec — the decoder is lossy on reserved bytes so the encoder
   refills them with what was on the wire. Round-trip gate: `encode_page(decode_page(raw)) == raw`
   for every captured page. Consolidated the planned pages.py + common_pages.py into one codec module.

2. **Decoded vs verbatim — a real finding:** a *decoded* page 0x12 (crank torque) would need
   simulated accumulators (torque / crank-period / event counts) = Phase 2 work. But page **0x10
   (Power-Only) needs none** — power, cadence, balance, accumulated-power are all it carries. So the
   `StagesAntTarget` **decoded mode emits 0x10 + periodic 0x50/0x51/0x52 identity commons** (a valid
   Bike Power meter, drives straight off a live Assioma in Phase 2), and **verbatim mode**
   re-broadcasts captured pages byte-for-byte for the highest-fidelity hardware proof.

3. **The radio seam + digital twins** (`ant/master.py`, `twins.py`) — the key enabler the owner
   asked for: an `AntMaster` ABC with a pure-software **`LoopbackMaster`** (in-process "air") and a
   **`BikeTwin`** (software SB20/display). The real-stick adapter `OpenAntMaster`
   (`ant/openant_master.py`) is the ONLY piece left for the bench — verified by API surface against
   openant 1.3.4, runtime not unit-testable. This means **we can bench-test the proxy as digital
   twins of bike + meter + (later) other devices with zero hardware**, in CI.

4. **Loopback verified** (`tests/test_loopback.py` + a live `03_static_replay.py --radio loopback`
   run): `ReplayFileSource → ProxyCore → StagesAntTarget → LoopbackMaster → BikeTwin`. The twin sees
   real replayed power (123→86→141→193→180 W across the run), the Stages identity (mfr **69**, serial
   **11821518**), and a working **zero-reset handshake** — twin requests a manual zero (ack page 0x01
   0xAA), target answers with broadcast **0x01 0xAC, offset 903**, exactly as the real crank did.

5. **Calibration response is broadcast, not acknowledged-reply** — confirmed the Phase-0 finding in
   the implementation: on the bike's zero-reset request the target injects the 0x01 0xAC response
   into the next few broadcasts (matches how the real crank replied). `send_acknowledged_data` is the
   documented fallback to try at the bench if the SB20 rejects the broadcast form.

6. **Unit testing is now a project rule** (`CLAUDE.md` §Validation + CI `.github/workflows/tests.yml`):
   desk-testable logic ships with tests in the same commit; fixtures come from the real captures;
   hardware is isolated behind a seam and tested with a fake (the `LoopbackMaster`/`BikeTwin`
   pattern); suite stays hermetic + green.

**Left for hardware:** `OpenAntMaster` on a real stick (hardware loopback — `--radio ant`, witness
with a 2nd stick or a phone/Garmin), then the SB20 pairing test (Phase 1B, `NEXT-BIKE-SESSION.md` §7).

## 2026-06-15 — Twin library foundation + radio loopback (the transport seam)

Generalised the digital twins into a small library and built the real-radio loopback path.

1. **Transport seam** (`twins/transport.py`): a `TwinTransport` ABC decouples a twin's logic from
   what's on the other side — `LoopbackTransport` (in-process, CI, no openant), `AntSlaveTransport`
   (a real ANT+ slave on a stick), later a BLE transport. `DeviceTwin` (base) + `BikeTwin` now sit
   on this seam, so the SAME twin runs as a pure software twin, over a real radio, or opposite a real
   device — no logic change. `twins.py` → `twins/` package (import path unchanged).

2. **A single ANT+ stick can't loopback to itself** — it's a half-duplex radio with no internal
   TX→RX path, so a real on-air loopback needs a **second receiver**: a 2nd ANT+ stick (scriptable via
   `10_bike_twin.py`, which runs a `BikeTwin` over `AntSlaveTransport`), or a phone ANT+ app / Garmin
   paired to the spoofed id, or a real meter / the SB20. A single stick still verifies the TX binding
   opens (`pytest --run-hardware` → `OpenAntMaster` smoke test).

3. **Hardware tests are gated** (`conftest.py`): `@pytest.mark.hardware` tests are skipped unless
   `pytest --run-hardware`, so CI and a plain `pytest` stay hermetic (still 35 passed, 1 skipped).

4. **Roadmap** (production twin library): a `PowerMeterTwin` source twin, an FE-C/trainer twin, and a
   BLE display twin — each pure-software in CI, real-radio on a stick, and able to face a real device.
   This is the bench-test harness for Phase 2+ (live proxy) without riding.

## 2026-06-15 — Phase 2 built in software + the meter-to-meter calibration seam

Phase 2 (live proxy) is **code-complete and proven in software** via the digital-twin chain; the
generic source means the Phase 4 "any meter" goal is delivered early. 45 tests, 1 hardware-skipped.

1. **Generic source** (`sources/ant_power.py` `AntPowerSource`): decodes page 0x10 → `PowerReading`
   over the receiver transport. Not Assioma-specific — Assioma / Rotor InPower / XCadey all broadcast
   0x10, so only the device id differs. (Renamed the planned `AssiomaAntSource` to the generic
   `AntPowerSource`.) The live analogue of `ReplayFileSource`.

2. **`PowerMeterTwin`** (`twins/meter.py`): the producer twin — a master broadcasting power with an
   optional `error(power, cadence)` model. This is what makes calibration **testable without
   hardware**: inject a known error into the twin, assert the proxy's correction recovers true power.

3. **Correction seam** (`transform.py` + `ProxyCore` transform arg): the quantitative replacement for
   a "swag" offset. `ScaleOffsetTransform` (linear) and **`GridTransform`** (piecewise-linear
   power→factor, for a NON-linear error across the curve) both verified. Default is pass-through
   (the SB20 path needs no correction — it's the velodrome XCadey-vs-reference case that does).
   Proven end-to-end: meter twin reports +10% high → `ScaleOffsetTransform(1/1.1)` → bike twin sees
   true power.

4. **Calibration product, fully designed** (forward-plan §4a): capture (`07_capture_multi`) → analyze
   (`08_analyze_grid`) → **fit** (small new tool, the only missing piece) → apply (the transform) →
   deploy (ESP32 jersey-pocket bridge). The power-grid stays valuable as the *calibration* artefact
   for any meter pair, even though the SB20 proxy itself doesn't need it.

5. **Hardware available for the next few days** (owner): TacX Neo (Gen-1, FE-C), Rotor InPower,
   XCadey (on the velodrome bike — the calibration target), many HR straps, a Concept2 erg, a Garmin
   footpod, and Garmin 520/540 + Epix 2. **Key:** the Garmins are the on-air loopback **witness/RX**
   (a head unit pairs to the spoofed crank id and shows watts, and can trigger a real zero-reset), so
   the hardware loopback needs no second ANT+ stick purchase. The extra meters generalise the source
   + give real `PowerMeterTwin` fixtures; the Neo is a real FE-C device for a future trainer twin.

## 2026-06-15 — ON-AIR LOOPBACK WORKS (two sticks, real radios) + shutdown hardening

The owner found a second ANT+ stick, so the full radio stack is now validated on real hardware —
the last desk milestone before the SB20 itself.

1. **Two-stick on-air loopback PASSES.** Both sticks are ANTUSB2 (`0fcf:1008`) on one WSL host. The
   new `usb_select` shim pinned each process to a different stick (TX `--usb-index 0`, RX
   `--usb-index 1`) — necessary because openant always grabs the first `0fcf` device. `03_static_replay
   --radio ant` broadcast the spoofed crank **62145** (decoded replay of A-steady); `10_bike_twin
   --usb-index 1` received it as a `BikeTwin`: real power tracked the capture (117→…→548 W surge),
   cadence, and (with `--commons-every 12`) the Stages identity **mfr 69 / serial 11821518** over the air.

2. **The bidirectional calibration handshake works over the air** — the slave (BikeTwin) sent a
   manual-zero request (acknowledged data, page 0x01 0xAA) up to the master; the master answered with a
   broadcast **0x01 0xAC offset 903**, which the slave received. This ack-uplink was the part we were
   least sure about. Validates `OpenAntMaster` (TX), `AntSlaveTransport` (RX), the pinning shim, and the
   calibration contract — all on real radios.

3. **Shutdown bug found & fixed (hardware-only).** A second run HUNG: openant's `node.stop()` join()s an
   internal worker with no timeout and blocks forever if the channel wedged (CHANNEL_IN_WRONG_STATE),
   and the proxy CLIs lacked the `os._exit` hardening the capture scripts have → two processes hung
   holding both sticks (killed by exact PID). Fix: `close()` runs `node.stop()` in a daemon thread with a
   2 s join timeout; `03/04/10` `os._exit(rc)` at the end of `main()`. Re-verified: clean exit (rc 0, no
   zombies). The software loopback could never have caught this (no real openant threads) — exactly why
   the hardware loopback matters.

4. **Identity-cadence finding.** The commons (0x50/0x51/0x52) go out at start + every ~30 s (matching the
   real crank), so a late-joining receiver waits up to 30 s for identity. `--commons-every` now tunes
   this; **watch at SB20 pairing (Phase 1B)** — if pairing is slow, front-load commons.

5. **WSL multi-stick setup notes:** `usbipd bind` needs admin; usbipd-attached nodes are root-only →
   `chmod 666 /dev/bus/usb/.../...` per attach (the udev rule isn't firing). openant claims the device
   synchronously in `Node()`, so per-construction USB pinning works.

**Left:** Phase 1B — pair the real SB20 (and the live-proxy-on-air test once a real meter is broadcasting).

## 2026-06-15 — Power-witness PASS on a real Garmin + FIT comparison + FE-C/TrainerTwin

Validated the transmit chain against a real name-brand head unit, and built the
FIT-comparison tool, the FE-C codec, and a TrainerTwin (a productive run while the owner
was away). 75 tests, all green.

1. **Power-witness test PASS.** Broadcast the A-steady capture on a stick (spoofed crank
   62145); a **Garmin Fenix 2** (2014, ANT+) paired to it and recorded a ~24-min FIT. The
   `12_compare_fit.py` overlay vs the broadcast capture: **correlation 0.9919**, mean abs
   error **6.35 W**, 78.5% within ±10 W over 647 matched seconds, offset 43 s; FIT power
   range 0–569 W (the surge + coast both landed). So a real Garmin faithfully recorded power
   from a crank that doesn't exist — the whole `OpenAntMaster` TX path validated end to end.
   The small residual is the Fenix's 1 Hz + smoothing vs our 4 Hz broadcast (transition lag),
   not a fault. **Gotcha:** the Fenix's clock was years stale (box storage, no GPS), so the
   activity is misdated `2019-07-12` — identify the new FIT by "newest not in the baseline",
   not by date. WSL doesn't auto-mount removable drives, so copy the FIT off `F:` via Windows.
   Evidence: `findings/fenix-witness-20260615.fit`.

2. **FIT-comparison tool** (`fitcompare.py` + `12_compare_fit.py`): extract power from a
   capture and from a Garmin FIT (fitparse), auto-align by best whole-second offset, report
   correlation / mean error / %-within-tolerance. FIT field names confirmed against the real
   Fenix file.

3. **FE-C codec** (`ant/fec.py`): encode/decode pages 0x10 (general FE data) and 0x19 (trainer
   data: power/cadence/accumulated) — round-trip verified over all 777 real SB20 FE-C records,
   dropped bytes refilled with observed constants — plus the 0x31 Target Power control page.

4. **TrainerTwin** (`twins/trainer.py`): a software FE-C smart trainer that broadcasts trainer
   data and **obeys erg control** (a controller sends Target Power → the twin holds the rider
   there). The control complement to the power twins; the "FIT workout drives a trainer twin"
   path, testable in CI. (The Fenix 2 itself predates FE-C control — use an Edge 520/540 or
   Epix for the real control side.)

5. **Calibration profile-fitter** + **TOML config / real `sb20proxy` CLI** also landed earlier
   today (see prior entries / git log).

## 2026-06-15 — ESP32 firmware branch + non-bike desk backlog (autonomous, owner at golf)

Kicked off the ESP32 productisation on branch **`esp32-proxy`** (a dual-role BLE proxy that
mirrors both the Python proxy and the `cauldnz/raedian-probe` firmware conventions: a pure
`lib/proxy/` core host-tested with no hardware, NimBLE 2.2.0, `src/` the only Arduino files),
then cleared a three-item non-bike backlog. Each item shipped with host tests in the same
commit and an ESP32-C3 compile check. **Firmware host tests 7 → 17, all green; both the
default BLE build and the WiFi/OTA build link clean.** All of it stays gated on **Session G**
(does the SB20's BLE-crank mode give full erg + is it spoofable) before it matters on the bike.

1. **GridTransform → C++ `CorrectionCurve`.** The non-linear power→factor curve (the XCadey
   case) ported into `Correction` so it takes precedence when populated; `ProxyCore`/`main`
   unchanged (the `Correction{scale,offset}` aggregate still works, curve defaults empty).
   `factorAt()` interpolates between breakpoints, flat-held outside — **golden values identical
   to the Python `GridTransform`** (e.g. `round(200*0.93)=186`), so firmware and Python agree.

2. **CPS cadence (Crank Revolution Data).** The spoofed crank now emits cadence, not just power.
   Key decision in the `CrankCadence` model: advance the *last crank event time* by exactly one
   revolution-period (**`61440/rpm` ticks**, i.e. `60*1024/rpm`) per completed revolution, so a
   head unit recovers the input rpm **exactly, with no quantization jitter** (the naive "stamp
   event time = now at 1 Hz" approach makes integer revs against a 1 s window read 60/120/60…).
   Tested at 96 rpm (period = exact 640 ticks). CP Feature advertises Crank-Rev-Supported (`0x08`);
   the **full Stages `0x2F`** (pedal balance + accumulated torque too) is deferred to Session G.

3. **WiFi + OTA + HTTP observability (`WifiLink`).** Mirrors the raedian-probe failsafe idiom:
   join WiFi (creds from gitignored `wifi_secret.h`), serve status JSON at `GET /` + OTA at
   `/update`, **boot-guard** `esp_timer` self-reset if never healthy, plus
   `esp_ota_mark_app_valid_cancel_rollback()` on healthy. Decisions: **`USE_WIFI=0` by default**
   so the normal build needs no creds (the `.cpp` body is wholly `#if USE_WIFI`); new
   **`esp32c3-ota`** env (`espota`, `min_spiffs.csv` for two OTA app slots); the served JSON is
   the pure host-tested `Status.h`. Compile-verified with a throwaway gitignored `wifi_secret.h`
   (deleted after) — **Flash 50.5% of the 1.9 MB OTA slot** with WiFi + dual-role BLE + OTA all
   coexisting on the C3.

   **Gotcha:** any `src/` file is compiled in the ESP32 build (only `native` filters `src/` out),
   so a WiFi-only `.cpp` must guard its *entire* body (incl. the `wifi_secret.h` include) behind
   `#if USE_WIFI`, or the creds-free default build breaks.

4. **WiFi captive-portal provisioning (`WifiLink` + `WifiCreds` + `Provisioning.h`).** WiFi creds
   are now set at **runtime** — no `wifi_secret.h` needed. Decisions made:
   - **Custom portal, not a library.** `DNSServer` (wildcard → SoftAP IP, triggers the OS
     captive popup) + the existing `WebServer`. Clean-room MIT — no GPL prior art, no extra dep
     (`DNSServer`/`WebServer` ship with the Arduino-ESP32 core).
   - **AP name `SB20-Setup`, open.** Open AP is the norm for setup; overridable via `-DWIFI_AP_SSID`.
     Portal IP is the SoftAP default `192.168.4.1`.
   - **NVS is the source of truth** (`Preferences` namespace `"wifi"`, keys `ssid`/`pass`).
     `wifi_secret.h` is now **optional** (guarded by `#if __has_include(...)`) and only *seeds* the
     first boot; the unedited `your-2.4GHz-ssid` placeholder is ignored so a stale template can't
     block setup.
   - **Auto-fallback to portal** if stored creds fail to join within `WIFI_CONNECT_TIMEOUT_MS`
     (moved router / wrong password) — re-provision with no USB reflash. `GET /forget` (in both
     portal and station modes) wipes creds and reboots into setup.
   - **Boot-guard disarmed in portal mode.** Waiting for the user is a *stable* state, not a failed
     flash — otherwise the `esp_timer` failsafe would reboot the device out of setup. The guard
     still protects the station+OTA path (armed before the blocking join, cancelled on healthy).
   - **Captive-probe endpoints** answered with a 302 to the portal: `/generate_204`, `/gen_204`,
     `/hotspot-detect.html`, `/ncsi.txt`, `/connecttest.txt`, plus `onNotFound` catch-all — this is
     what makes the setup page auto-pop on Android/iOS/Windows.
   - **Headless now, display seam later.** `IProvisioningDisplay` (default `SerialProvisioningDisplay`
     prints the AP SSID + URL); a future OLED/QR module implements the three calls and is injected
     via `WifiLink::begin(..., display)` without touching portal logic.
   - **Pure logic is host-tested** (`Provisioning.h`: page render, urlencoded form parse incl.
     `+`/`%XX`, WPA/open validation) — 7 new Unity cases (`pio test -e native`, 24/24). The
     SoftAP/DNS/WebServer wiring is bench-tested (see `NEXT-BIKE-SESSION.md`). A **`firmware` CI job**
     (`.github/workflows/tests.yml`) now runs `pio test -e native` on every push, alongside pytest.
   - **Persists through OTA — by design.** Creds live in the `nvs` partition; an OTA update
     (`ArduinoOTA` / the `/update` form, both via `Update`) writes only the inactive app slot
     (`ota_0`/`ota_1`) and flips `otadata` — it never touches `nvs`. Both partition tables in use put
     `nvs` at the **same** offset (`0x9000`, size `0x5000`): the BLE-only `default.csv` and the OTA
     `min_spiffs.csv`, so creds even survive switching between those builds. The only NVS erase is
     `WifiCreds::clear()` from `/forget` (explicit). **Invariant to hold:** do not relocate/resize the
     `nvs` partition — OTA does not rewrite the partition table, so a moved `nvs` would orphan the
     stored creds (and `esptool erase_flash` / `pio run -t erase` wipes them). Worst case is graceful:
     lost creds just drop the device back into the setup portal. Verify on hardware via
     `NEXT-BIKE-SESSION.md` §8 (provision → OTA-flash a new build → confirm it rejoins, no re-setup).

5. **Diagnostic `/log` endpoint (serial-over-HTTP).** The C3 Super Mini's native-USB serial is
   unreliable (the recurring pain on `raedian-probe`), so debugging WiFi setup over the cable is
   flaky. Reusing that repo's "serve logs over a tiny HTTP endpoint" idiom (clean-room — its source
   isn't in scope this session anyway): a RAM **ring buffer** (`lib/proxy/LogBuffer.h`, pure +
   host-tested) is mirrored from Serial by `logf()` (`src/net/DebugLog.*`) and served at `GET /log`
   as text/plain. Decisions:
   - **Plain HTTP, not TLS.** Matches the existing `GET /` status endpoint and is light on a C3
     sharing the radio with BLE (coex); HTTPS would need a cert + much more flash for no real gain
     on a local setup network.
   - **Available in both modes.** `addLogRoutes_()` registers `/log` on the *portal* server (so
     first-time setup is observable, the whole point) and the *station* server (normal operation).
   - **Toggle, on by default, persisted.** `/log/on` · `/log/off` flip a flag stored in NVS
     (`WifiCreds::logEnabled`, default true) so it carries across reboot **and OTA** (same nvs
     partition as the creds); the setup page shows the state + a toggle link
     (`renderLogToggleFooter`, host-tested). When off, `/log` returns 403.
   - **Never log secrets.** `logf` echoes to `/log` over the open setup AP, so the WiFi password is
     never passed to it (join logs the SSID only). Lines are length-capped (`kMaxLine`) and the ring
     holds ~60 lines, bounding RAM.
   - Tested: 5 new Unity cases (ring ordering / eviction / line-cap, footer states, portal footer
     wiring) — `pio test -e native` now 29/29. Bench step added to `NEXT-BIKE-SESSION.md` §8.

## 2026-06-16 — Next-bike-session plan refinements (owner feedback)

Three corrections from the owner while reviewing the `NEXT-BIKE-SESSION.md` plan:

1. **Phase 1B spoof-pairing needs BOTH crank ids.** The SB20/Stages app pairs the crankset as a
   **linked L/R pair**, so it asks for both the left and right ids — not just one. Plan updated: for
   the test, change **only the L id to the spoof `62145`** and leave **R = `4963`** (the real right
   crank stays in and keeps broadcasting); the bike then listens for our spoofed L master and
   ignores the real `62144`. **Restore-after values recorded prominently:** `Stages 62144 (L) :
   4963 (R)`, crank length **165 mm**, zero-offset **L 903 / R 951**. Fallback if the app rejects an
   unmatched L id: spoof the real `62144` with the L-crank battery pulled for the test.
2. **External / Power-Erg via Assioma id is a REFUTED path.** Owner has already tried entering the
   **Assioma ANT+ id directly into the Stages Cycling app** and got **no external-meter erg**. So
   the "simple path" (bike ergs off the Assioma, no crank spoof) is, as far as we know, **closed** —
   the crank spoof is confirmed necessary. Step 4 downgraded to "skip unless a Stages app update
   adds a *dedicated* external-meter pairing UI distinct from the id field that already failed."
3. **Session G Part A (BLE recon) promoted to do-regardless.** Owner wants the crank's BLE surface
   captured/documented whatever Part C decides. This is fine because it's **independent of Part C** —
   the 2026-06-15 finding established the Stages crank is reachable over BLE *in ANT+ mode* (target
   `Stages 62144` by name). No BLE-crank mode required for the recon capture.

## 2026-06-16 — Captive portal: suppress browser "strong password" overlay

The portal's password input asks for an **existing** WiFi key the rider already knows, but iOS
Safari (and the iOS Captive Network Assistant webview) treat any `type=password` field as a *new*
login: they pop the "Use Strong Password" generator and a save-password prompt over the field, which
is useless here and gets in the way. `autocomplete='off'` does **not** fix this — Safari ignores it
for password fields specifically.

Options considered:
- **`autocomplete='current-password'`** — the documented signal that the field is an *existing*
  credential (vs `new-password`, which is what *triggers* generation). It reduces the overlay but
  doesn't reliably eliminate it: Safari may still offer to save, and behaviour in the CNA webview is
  inconsistent. Reduces, doesn't guarantee.
- **Masked TEXT field (chosen).** Render `<input type='text'>` (not `password`) with CSS
  `-webkit-text-security:disc` for the dot-mask, plus `autocomplete='off' autocapitalize='off'
  autocorrect='off' spellcheck='false'`. A non-password field is **never** classified as a
  credential, so no generator/save overlay can fire — the only *guaranteed* fix across all three
  targets. `-webkit-text-security` is supported by WebKit **and** Blink, i.e. every captive-portal
  browser (iOS Safari, iOS CNA webview, Android Chrome). `autocomplete='off'` **is** honoured for
  text fields. Note: `current-password` was deliberately **not** added — it's semantically wrong on
  a text field and could re-trigger the credential heuristics we're avoiding.
  - **Show/Hide toggle** added (`revealPass()` flips a `.show` class that clears
    `-webkit-text-security`) since a text field has no native reveal control. Default state is
    masked.
  - Form still posts `name='pass'` exactly as before — `parseFormUrlEncoded` is unchanged.
- Tested: 1 new host-Unity case `test_portal_page_password_not_a_credential_field` asserts no
  `type='password'` / `new-password`, the `-webkit-text-security:disc` mask, the four
  credential-suppressing attributes on the `#pass` input, and the reveal toggle. Verified locally by
  compiling the full `test_main.cpp` TU under a Unity shim (no PlatformIO on this box) — green; the
  real `pio test -e native` runs in CI. On-device behaviour across the three browsers is a bench
  check (`NEXT-BIKE-SESSION.md`). Files: `firmware/lib/proxy/Provisioning.h`,
  `firmware/test/test_proxy/test_main.cpp`. Tracked in `forward-plan.md` §8.

## 2026-06-17 — ESP32 BLE bring-up: real dual-role proxy, both directions, on hardware

Goal for the day: an initial cut of the ESP32 BLE work — firmware on a real board that (1)
**receives** power-meter data from the Python harness, (2) **broadcasts** spoofed power received
by the Python harness, with (3) both visible in the web UI + OLED. All three met on hardware.

**Python BLE *peripheral* via WinRT directly — NOT bless.** Goal #1 needs the harness to be a BLE
peripheral (the ESP32 is the central). bleak is central-only, and **`bless` is incompatible with
Python 3.13**: bless 0.3.0 pins `winrt-*==2.0.0b1` (requires-python `<3.13`), and bless 0.2.6
imports the removed `bleak_winrt` module. Rather than fight an unmaintained dep (or downgrade
Python), the harness drives the **WinRT `GattServiceProvider` API directly** — the same
`winrt-windows-devices-bluetooth-genericattributeprofile` packages bleak 3.x already installs and
uses. Spiked first: `BluetoothAdapter.is_peripheral_role_supported == True` on this host (Realtek),
advertising settles to `GattServiceProviderAdvertisementStatus.STARTED` (the momentary `ABORTED`
on start is a normal WinRT publisher transition), notify works. New module
`code/src/sb20proxy/ble/winrt_peripheral.py` (`WinrtCpsPeripheral`) — Windows-only, kept OUT of
`sb20proxy.ble.__init__` so the package import graph and the hermetic test suite never pull in
WinRT (it's a hardware seam, like the firmware's OLED/radio).

**WinRT advertises the service UUID but no custom local name** → the firmware `BleMeterClient` now
matches the meter by **advertised CPS service UUID (0x1818)** first, name (`ASSIOMA`) second. A
real Assioma advertises both, so this is strictly more permissive.

**Never read FROM our own spoof identity.** With two boards on the bench, the freshly-flashed
central immediately latched onto the *other* board (running the old mock firmware, advertising
`SPOOF_NAME` = "Stages 62144" + CPS) and relayed its ramp. `BleMeterClient` now **skips any
advertiser whose name == `Config::SPOOF_NAME`** — a correct rule beyond the bench: the meter we
read is the Assioma, never another proxy / sibling board / the real Stages crank we replace.

**Cadence pass-through.** `BleMeterClient` previously decoded only power (`decodeCpsPower`). It now
decodes Crank Revolution Data and recovers cadence (the head-unit delta method) when the crank
fields sit at the fixed 4–7 offset (no balance/torque/wheel field precedes them — true for our
meters' power+cadence frame; `Cps.h` gained `CPM_PRECEDING_DATA_BITS = 0x0015` for the check).
Received cadence now flows through to the spoofed crank, web UI and OLED.

**Staleness watchdog + self-heal.** A connected meter notifies ~1 Hz even at zero power, so a
≥6 s gap (`kMeterStaleMs`) means it's gone. Critically, an abruptly-vanished peer (a *killed* host
process) can leave the controller link up with NimBLE's `onDisconnect` never firing — observed: the
board wedged "connected" to a dead meter forever. So the watchdog resets state **proactively** in
`loop()` (`onDisconnected()` directly, not waiting on the callback) and rescans. Verified the full
lifecycle on hardware: connect → relay → meter-gone → `searching` → reconnect to a fresh meter.
(A *clean* meter disappearance — power-off / out of range, the real-ride case — fires `onDisconnect`
naturally; the watchdog is the backstop for the silent-but-linked case.)

**Observability — both directions.** `ProxyStatus` gained `srcPowerW`/`srcCadenceRpm` (received
from the meter) alongside `lastPowerW`/`lastCadenceRpm` (broadcast to the crank); `renderStatusJson`
emits `src_power_w`/`src_cadence_rpm`; the `/ui` dashboard shows a `METER IN → CRANK OUT` flow row.
OLED is unchanged (already shows live power/cadence/IP at 2 Hz — the "slow update").

**Build envs.** Added `esp32c3-wifi-live` / `esp32c3-oled-live` (+ `-ota`) with `USE_MOCK_METER=0`
so the *real* `BleMeterClient` is compiled into a flashable WiFi/OLED build (every prior WiFi/OLED
env hardcoded the mock). Flashed via **OTA over WiFi** (`espota` → 192.168.1.165), which succeeded
even at RSSI −73 at the desk — the USB-JTAG manual-bootloader dance was not needed this time.

**Verified on hardware (board: ESP32-C3 OLED, `sb20proxy.local`):**
- Goal #1: `fake_meter.py --watts 177 --cadence 90 --steady` → board status `source:connected,
  src_power_w:177, src_cadence_rpm:90` (received power AND cadence from the Python app).
- Goal #2 / full chain: `crank_reader.py --address 38:44:BE:45:E9:A6` → `177 W, 90 rpm`, frames
  `2000b1…` — proving Python meter → ESP32 (receive+relay) → Python reader end-to-end over the radio.
- Goal #3: `/ui` renders METER IN/CRANK OUT; `/` JSON carries both directions; OLED render path active.

**Tested (CI / desk):** host pytest 121 passed (BLE codec/loopback unchanged + still green); ruff
clean. The firmware host-Unity suite gained `src_*` JSON + `/ui` flow assertions but **can't run on
this Windows box (no host gcc)** — it runs in CI (Linux). The full `esp32c3-oled-live` target
**compiles clean** against the real toolchain (the local compile gate). The WinRT peripheral, the
two harness scripts, and the NimBLE `BleMeterClient` paths are bench-tested (the hardware seam),
consistent with the project's "isolate hardware, manual on-air check" discipline.

**Scope / deferred (forward-plan §BLE):** FTMS ("fitness device data" — Indoor Bike Data + erg
Set-Target-Power) is the next layer and was deliberately NOT in this cut (the firmware has no FTMS
yet); CPS power+cadence both-ways is the committed deliverable. Also noted: stale src_* values are
retained (not zeroed) on disconnect — `source:searching` is the authoritative "no live meter"
signal; zeroing is cosmetic polish. Two same-named "Stages 62144" advertisers on the bench mean
`crank_reader` should target board A by address (`--address`), not name.

## 2026-06-17 — Session G Part B: real Stages crank BLE spec captured + firmware made byte-faithful

On the bike: the SB20 **paired to our spoofed ESP32 crank over BLE but displayed NO power**. Root
cause = our spoof was too minimal (a flags-`0x20`, power+crank-rev frame). Captured the REAL crank's
full BLE surface to replicate exactly — `findings/captures/G-crankL-ble-recon-20260617.jsonl` (BLE
recon worked once we targeted by address; the desk PowerShell *background* wrapper was broken, but
foreground/Bash + `--address` is reliable). The spec the spoof must match:

- **Device:** name `Stages 62144`, addr `E8:CF:D8:D9:3A:20`. DIS: manufacturer **"Stages Cycling"**,
  model **"SPM2"**, serial **"11821518"**, FW **"1.8.2"**, + System ID. Battery 12%.
- **CPS Measurement (0x2A63, notify) — flags `0x002F`, 11 bytes**, golden frame
  `2f 00 ae 00 58 3c f7 e6 00 be 6c`:
  `flags(2) | inst power sint16 | pedal balance uint8 (1/2 %) | accumulated torque uint16 (1/32 N·m)
  | cumulative crank revs uint16 | last crank event time uint16 (1/1024 s)`. Decodes to 174 W,
  balance 44% (ref left), torque 63292, revs 230, evt 27838. Two frames → ~54.5 rpm (low-cadence,
  high-torque effort) — internally consistent, which validates the field decode.
- **CP Feature (0x2A65):** `0x0008030B`. **Sensor Location (0x2A5D):** `0x00` ("other", *not* 5/left).
- **Services:** CPS (1818), Battery (180f), DIS (180a), GAP/GATT, **Stages proprietary
  `d445fe01-d139-9a5d-6707-1cc6a58b6303`** (chars `…fe02` notify+write, `…fe03` notify — advertised in
  the scan response), and **Nordic Buttonless DFU `fe59`**. The proprietary service is the likely
  "is this a genuine Stages?" check — strong suspect (with the frame shape) for the no-power result.
- **Control point (0x2A66):** `request-crank-length` (0x05) → 165 mm; `request-sensor-locations`
  (0x03) → `op_code_not_supported`. (Zero-reset not re-run — we have the ANT+ offset 903 from C-0.)

**Firmware made byte-faithful (branch `ble-crank-fidelity`):**
- `encodeStagesCpsMeasurement` → the exact `0x2F` frame; **4 golden-vector host tests built from the
  captured bytes** prove byte-identity (suite 45 → **49/49**). Real-data-first: the fixtures ARE the
  capture, not invented.
- Generic `decodeCrankData`/`crankRevDataOffset` → finds crank-rev behind any preceding optional
  fields, so `BleMeterClient` now reads the **Assioma's cadence** too (it sends balance first, which
  the old fixed-offset path skipped → the `src_cadence:-1` we saw).
- `BleCrankPeripheral`: CP Feature `0x0008030B`, Sensor Location `0`, DIS model `SPM2` + FW `1.8.2`,
  **the `d445fe01` proprietary service advertised + exposed**, and emits the `0x2F` frame (balance
  synthesized 50%, accumulated torque advanced per completed rev = P·60/(2π·rpm)·32).
- **Deferred/uncertain:** the `…fe02/03` payloads are opaque (created empty — presence is the bet;
  logging what the SB20 writes to them is the next capture). Balance is synthesized (single source has
  no L/R split). Compile-green (`esp32c3-supermini` 37%); OTA-flashing `esp32c3-oled-live` + a desk
  GATT re-capture confirm the identity surface — **SB20 power-acceptance is the next bike test.**

## 2026-06-18 — Overnight: observability + fast-iterate instruments (autonomous, PR #5)

Built the instruments to make bike-session 2 maximally productive and to support shipping beta
units. The principle: the SB20's INTERACTIVE protocol (control-point/zero-reset, reconnect/bonding,
proprietary `fe02`) can't be sniffed — but our spoofed crank is the one thing that sees it, so it
logs it. All on branch `ble-crank-fidelity` (PR #5).

1. **ESP write-logging.** `logf` ungated from `USE_WIFI` (always compiled; only the `/log` endpoint
   is WiFi-gated); ring 60→120. `BleCrankPeripheral` logs every control-point write (raw+opcode),
   every proprietary `fe02` write, and connect/disconnect (reason) → `/log`. `BleMeterClient` logs
   each connected meter's name + its raw CPS frame ONCE per connection (flags + `cadence=yes/no` +
   hex) → a field unit teaches us each meter (Garmin/Wahoo/Assioma) and confirms whether it carries
   cadence. `toHex` host-tested.
2. **OLED cadence.** The 0.42″ panel shows only 3 rows, so a 4th (cadence) fell off; power + cadence
   now SHARE row 3 (`"230W 85rpm"`) so the rebroadcast cadence is visible. Tests updated.
3. **PC fast-iterate rig.** `WinrtCpsPeripheral` gained a control-point char that captures every
   write (`.writes` + `on_write`); `scripts/fake_crank.py` stands up a faithful crank (0x2F frame
   golden-matched to the capture) and decodes writes. **WinRT verdict (confirmed):**
   `GattServiceProviderAdvertisingParameters` has **no name field** → it advertises under the PC's
   system BT name, not `Stages 62144`; the SB20 pairs by name, so the PC rig isn't seen as the crank
   unless the PC's BT name is renamed; also one-service-per-provider (no proprietary `d445fe01`). So
   **the ESP `/log` is the reliable handshake-capture path**; the PC rig is the fast-iterate option
   gated on the rename (`scripts/PC-CRANK.md`). A future ESP speedup: runtime-configurable
   control-point responses (HTTP) to avoid reflashing per response experiment.
4. **Beta log parser.** `logparse.parse_log()` turns a unit's `/log` dump into a meter frame spec +
   the decoded consumer handshake + connect/disconnect events; host-tested (5 cases) against the real
   firmware line formats. Run-sheet for session 2: `BIKE-SESSION-2.md`.

Host suite 50/50; ESP builds clean + flashed; new Python ruff-clean + pytest green. **Still
unverified (needs the bike): does the SB20 read the faithful frame, what it writes to the control
point/`fe02`, and reconnect/bonding behaviour.**

## 2026-06-18 — Bike session 2: SB20 power-acceptance PROVEN + interactive protocol captured + shifter-over-BLE found

The fidelity fix works. With the byte-faithful `0x2F` crank (PR #5) the **SB20 reads, displays, and
runs on the relayed Assioma power + cadence** — the core goal, proven on real hardware. We also
captured the interactive protocol the firmware still has to answer, and (bonus) discovered the SB20
broadcasts its shifter/gear state over BLE. Logs: `session2-log-20260618-0713.txt` (calibration /
disconnect), `session2-confirm-assioma-20260618-0742.txt` (clean Assioma-only confirm),
`SHIFTER-probe-20260618.jsonl` (GATT + shifter).

**Core result — power-acceptance PASS.**
- SB20 paired to the spoofed `Stages 62144` (ESP) showed **power AND cadence** in the Stages app and
  on the OLED, tracking the Assioma with no scaling error. Cadence works because the SB20 derives it
  from the crank-rev fields we forward (the ESP's own `cadence_rpm` status field is a separate,
  occasionally `-1` derivation the SB20 does not use).
- **Clean confirmation (no self-deception):** pulled BOTH crank batteries (L `62144`, R `4963`) → the
  only source left is the Assioma → ESP locked onto `ASSIOMA17039L` (`e6:20:90:8c:f3:fe`) and the SB20
  still showed power + cadence. Provably Assioma → ESP → SB20, zero real-crank involvement.

**Assioma source facts.**
- Sends cadence: CPS Measurement flags **0x0023** (pedal-balance + crank-rev data present) → answers
  the long-open "does the Assioma carry cadence" = YES.
- Presents as **two** BLE devices: `ASSIOMA17039L` (`e6:20:90:8c:f3:fe`) and `ASSIOMA22428R`
  (`cc:d2:a0:d6:5c:9d`).

**Interactive protocol captured (the firmware's to-do list).**
- **Proprietary `fe02` handshake = `bfda1853`, CONSTANT.** The SB20 writes this 4-byte value to char
  `fe02` (svc `d445fe01-…`) once on connect — **identical across 4 separate connections** → a fixed
  init token, NOT a nonce. Power streams fine with just the `fe02` service present + this write logged;
  no notify response is needed *for power*.
- **Zero-reset = Cycling Power Control Point opcode `0x10`** (Enhanced Offset Compensation), written to
  `0x2A66`. We logged it but sent no indication back → the app's calibration spun → user cancelled.
- **Control-point writes MUST be answered or the SB20 terminates the link.** Every unanswered CP
  procedure dropped us: **`disconnect reason=531`** = NimBLE `0x0213` = HCI `0x13` "remote user
  terminated." Consequence: the **Set Crank Length (`0x04`) write never even landed** — the link
  dropped before the procedure ran (172.5 mm set in-app showed "empty" on read-back). Power streaming
  itself is stable as long as no CP procedure is invoked.
- **Re-advertise on disconnect is required.** After an SB20-terminated drop the ESP did **not** resume
  advertising → app stuck at "searching", could not reconnect; only an **ESP reboot** recovered it
  (reboot re-advertises → SB20 auto-reconnects and re-sends `bfda1853`). The SB20 remembers the pairing
  and reconnects on its own once we advertise.

**Meter client is too promiscuous.** With several meters in range `BleMeterClient` bounced between
`ASSIOMA17039L`, `ASSIOMA22428R`, and `Stages 4963` (`e3:25:39:38:92:71`), so the relayed source was
non-deterministic — at one point it relayed the real right crank `4963` to the SB20. Needs source
pinning by address/name. (Silver lining: that accidental relay of `4963` demonstrated the
**single-right-crank use case** — forward-plan §8 + `single-right-crank-proxy-usecase` memory; cf.
PedalSmart's single-failed-crank post.)

**BONUS — shifter buttons broadcast over BLE.** Probing the SB20's own GATT
(`06_capture_ble.py --address E4:AA:5A:D6:0E:D4 --subscribe-all`, a new mode that hooks every
notify/indicate char) revealed two **Stages vendor services**: `0c46be5f-…` (chars `0c46be60` notify,
`0c46be61` notify) and `0c46beaf-…` (`0c46beb0` notify, `0c46beb1` write-without-response). **Char
`0c46be60` carries gear state** — dead silent at idle, fires on every shifter press:
- `01 00 <gear:u16 LE>` — current-gear state (repeats)
- `03 00 <gear> <gear>` — shift event (gear field twice; semantics TBD)
- `04 00 <gear>` — shift-complete confirm
- **Gear is a one-hot bitmask:** 6 presses mapped to `0x08/0x10/0x20` (right ①②③) and `0x01/0x02/0x04`
  (left ①②③) = bits 0-5. Full range / per-button direction / the second vendor svc `0c46beaf` + write
  channel `0c46beb1` are the controlled-probe targets next session. See `findings/shifter-ble-protocol.md`.
- SB20 GATT also: DIS = "Stages Cycling" / model "SB20" / serial `H0512210105` / FW `1.1` / SW
  `1.12.4+3792`; FTMS `0x1826` (Indoor Bike Data `2ad2`, Fitness Machine Status `2ada`, Control Point
  `2ad9`); CSC `0x1816` (sensor location "rear_wheel"); Nordic Buttonless DFU `fe59`.

**Next (all specced; desk work, on-air verification bike-gated — `BIKE-SESSION-3.md`):** (1) firmware
control-point responder — ACK `0x10` with a synthetic success (the Assioma is the real calibrated
meter; nothing to zero on the ESP), handle `0x04`/`0x05` crank length, generic `0x20 <op> 0x02` for
unknowns; (2) re-advertise on disconnect; (3) meter-source pinning (also enables single-right-crank);
(4) comprehensive controlled shifter probe + protocol write-up.

## 2026-06-19 — ANT+ vs BLE zero-offset reconciliation (903 vs 0): the BLE crank must answer 0

The Stages crank's zero-offset reads **903 over ANT+** but **0 over BLE** — long an open item, and it
led to a real bug: `Config::SPOOF_CAL_OFFSET` carried the ANT+ value `903` into the BLE crank. They are
**different representations of the same calibrated state**, both correct for their protocol:

- **ANT+ Bike Power** (Calibration page 0x01, 0xAC general response): reports `903` — the crank's
  **raw zero-offset** (the internal no-load ADC value the ANT+ profile exposes directly).
- **BLE CPS Start Offset Compensation** (0x2A66 op 0x0C / 0x10): the real crank replies `200c010000`
  → offset **0** after a successful zero-reset (`G-crank62144-ble-zero-20260615-070353.jsonl`, which
  shows `200c01ffff` pre-zero → `200c010000` post-zero). The BLE op returns the **post-compensation
  residual**, which is 0 once re-zeroed.

So 903 (absolute raw, ANT+) and 0 (residual after comp, BLE) are the same crank from two protocols'
viewpoints. **Fix applied:** our ESP32 IS the BLE crank, so its Start/Enhanced Offset Compensation reply
must use the BLE value — `SPOOF_CAL_OFFSET` **903 → 0**, byte-matching the real crank's captured
`200c010000` (host test `test_calibration_response_bytes` updated to the captured golden). The
ANT+/openant proxy path keeps 903 (ANT+ semantics). This also tightens the spoof — a head unit reading
the offset now sees the real BLE value.

**Provenance note:** this finding originated on `claude/esp32-bike-powermeter-urnc0c` while `main`
advanced in parallel through bike-sessions 2; `main`'s `903` was the un-BLE-validated ANT+ carry-over
(session 2's zero-reset never completed, so 903 was never confirmed on BLE). Ported to `main` via PR #7
alongside the off-loop-OLED perf work, keeping all of main's PR #5 control-point / pinning fixes.

## 2026-06-19 — Bike session 3: firmware-fix verification + full shifter map

Findings (bike session 3; Part B precisely timestamped 08:38:17-08:53:08 local).

CORE PRODUCT RE-VALIDATED on the bike: Assioma (ASSIOMA17039L, e6:20:90:8c:f3:fe) -> ESP -> SB20 relays power AND cadence crank-free (real L-crank battery pulled, R 4963 in). source:connected, src_power_w==power_w, src_cadence==cadence, forwarded climbing. A calibrated meter is zeroed upstream, so the spoofed crank never needs its own zero -> A1/A2 below are protocol-completeness, NOT blockers.

Firmware fixes (PR #5) on the real bike:
- A3 reconnect-without-reboot: CONFIRMED. SB20 disconnect reason=531 (HCI 0x13 remote-terminated) + auto-reconnect 3x, reboot_count held at 7 (no reboot). advertiseOnDisconnect(true) works.
- A4 constant handshake: CONFIRMED. [prop fe02] write bfda1853 on every connect.
- A1 zero-reset (Enhanced Offset Comp 0x10): STILL FAILS. SB20 writes bare 10; firmware replies 20 10 01 00 00 (encodeOffsetCompResponse(0x10, SPOOF_CAL_OFFSET=0)). Link HELD (no disconnect, better than session 2) but Stages app calibrate UI spins forever -> our Enhanced-Offset reply format is wrong. Have the real crank 0x0C reply (200c010000, 2026-06-15) but never its 0x10. DESK FIX: implement proper CPS Enhanced-Offset-Compensation response and/or a write-capable BLE probe to elicit the real crank 0x10 bytes.
- A2 crank-length: setting 170mm produced NO [cp] write 04 to our crank (complete-from-boot /log); app showed 170 briefly then -- after a drop. Stages app does NOT configure crank length over the standard CPS control point (SB20-local or proprietary d445fe0x svc we present but do not implement). Our 0x04/0x05 handlers are vestigial for the Stages path.
- PERF: one 2.6s loop stall (loop_max_us 2619993, stalls_200ms 1) in the BLE (re)connect path. Isolated, no reboot, relay unaffected; desk look. Otherwise OLED-off-loop fix holds (p95 10ms, 0 stalls at idle).

SHIFTER MAP (Part B) — SB20 E4:AA:5A:D6:0E:D4 (Stages Bike 0105, SW 1.12.4+3792, Nordic nRF, Buttonless DFU 0xfe59), via 06_capture_ble.py --subscribe-all -> findings/captures/SHIFTER-probe-3-20260619-0838.jsonl (2687 lines):
- ALL 6 buttons notify ONLY on 0c46be60 (svc 0c46be5f), one-hot uint16: LEFT up/down/3rd = 0x0001/0x0002/0x0004 (bits 0-2); RIGHT up/down/3rd = 0x0008/0x0010/0x0020 (bits 3-5). Completes the session-2 partial.
- Frame per press: 01 00 <bit> streamed while held (~10-20 notifs/press, streams state not a clean edge -> emulator must debounce); commit 03 00 <bit> <bit> (both fields = pressed bit, NOT a left/right split); terminator 04 00 <bit> OR 08 00 <bit> (state-dependent: LEFT-up gave 04 once, 08 on all 10 of the walk; likely gear-changed vs at-limit; one open frame detail).
- STATELESS: LEFT-up x10 kept bitmask 0x0001 (no counter/wrap/clamp). Gear index is NOT on the shifter; consumer owns it -> maps 1:1 onto the Zwift-Click model.
- Silent channels 0c46be61 + 0c46beb0 never fired. HYPOTHESIS (owner): optional clip-on aero-bar REMOTE shifter pods — GATT has two parallel vendor svcs (0c46be5f: be60+be61; 0c46beaf: beb0 + write char beb1) = main + remotes. Untestable without the accessory. Brake levers untested -> session 4.
- Write candidate 0c46beb1 (write-without-response): NOT probed (deferred).
- Haptics: NONE on any pod — the buzz is the phone/Stages app (retro-explains session-2 "no haptic on 3rd").

Next: A1 desk fix (real 0x10 format) before next bike trip; session 4 = brake-lever probe (be61/beb0/FTMS Status) + retest A1; Zwift-Click clean-room research now unblocked.

## 2026-06-19 — A1 desk fix: CPS Enhanced Offset Compensation (0x10) response format

Session 3 proved our `0x10` reply was wrong: the SB20 sends a bare Enhanced Offset Compensation
(`[cp] write 10`), we replied `20 10 01 00 00` (the simple `0x0C` 5-byte shape), the **link held but
the Stages app's calibrate UI spun forever**. Derived the correct format **clean-room from the Bluetooth
Cycling Power Service spec** (read & reimplemented; no GPL prior art):

- **Simple Start Offset Compensation (0x0C)** success reply: `0x20 0x0C 0x01 | Offset (sint16 LE)` —
  matches the real crank's captured `200c010000`. Unchanged.
- **Enhanced Offset Compensation (0x10)** success reply is RICHER:
  `0x20 0x10 0x01 | Offset (sint16 LE) | Manufacturer Company ID (uint16 LE) | Manufacturer-Specific
  Data (opaque, fills the remainder)`. Our 5-byte reply was **too short to be a valid Enhanced
  response** → the procedure never completed → spin.

**Implemented** (`firmware/lib/proxy/Cps.h`): new `encodeEnhancedOffsetCompResponse(offset,
mfgCompanyId, mfgData)`; `handleControlPoint` now splits 0x0C (simple) from 0x10 (enhanced);
`BleCrankPeripheral` passes `Config::SPOOF_MFG_COMPANY_ID`. Host tests in the same commit
(`test_cp_offset_comp_enhanced_0x10` now asserts the spec structure; `test_encode_enhanced_offset_comp_structure`
golden-checks the byte layout incl. a sint16-LE negative offset + trailing mfg data). ESP32 target compiles.

**Residual unknown (capture-gated, real-data-first):** the spec mandates the Manufacturer Company ID
field but NOT its value, and the real crank's `0x10` reply was never passively sniffable (the whole
project premise). So `SPOOF_MFG_COMPANY_ID` is a **flagged placeholder (0x0000)** and `mfgData` is empty.
The spec-correct *structure* is the candidate fix; the exact bytes get **grounded by actively eliciting
the real crank's 0x10 reply** — `06_capture_ble.py --control-point enhanced-offset-compensation` (G1 in
`sessions/session-04-enhanced-offset-and-brake-levers.md`), then a one-line Config update + golden test.
A1 remains **protocol-completeness, not a product blocker** (a calibrated meter is zeroed upstream).
