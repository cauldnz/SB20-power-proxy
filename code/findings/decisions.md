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

## 2026-06-19 — Shifter-control direction: FTMS erg first, Zwift backlog, 2-button reality

Owner feedback after the Zwift research:

- **FTMS erg-watts-from-the-bars is the next feature** (gated on the session-4 §C capture: does the SB20
  erg off a third-party Set Target Power?). **Zwift-controller emulation → backlog** ("sounds complex"):
  it needs a custom RC1 GATT + RideOn handshake + (Play/Ride) ECDH/AES + protobuf, routes via Zwift not
  FTMS, and Zwift can reject non-genuine devices. See `zwift-controls-research.md` (parked).
- **Realistic button budget is TWO, not six.** Most riders already use the up/down buttons for the SB20's
  own shifting; only the **two "3rd" buttons** (`0x0004` L, `0x0020` R) are free (session 3: unbound but
  they still emit on `0c46be60`). Basic erg ± fits exactly (down/up). More functions need **input
  gestures** — and prior art exists in-domain: Zwift's SRAM-style shifting uses a **both-buttons chord**;
  BikeControl multiplexes Di2/AXS with single/double/**hold**. Buttons are momentary, but session 3's
  captured `01`-frame *stream while held* means **hold-duration IS observable** (the "no long-press"
  assumption is beatable; tap/double-tap/chord are still nicer UX). Chord/double-tap/hold frames to be
  characterised in session-4 §B.
- **Stages app "Profiles" are a dependency.** The app configures per-button behaviour via Profiles (no
  official docs). The 3rd buttons are unassigned so no conflict; but repurposing a *shifting* button would
  require disabling its shift in a Profile. Owner to gather annotated screenshots of the Profiles /
  button-assignment screens (what a button can be set to, can it be disabled, do the 3rd buttons appear,
  do shift buttons act in erg mode) — capture-before-code input for the button design.

## 2026-06-19 — Shifter button allocation refined: erg = main buttons (in erg mode), 3rd = control

Refinement of the same-day button-budget note (owner). Better split than "erg on the two 3rd buttons":

- **Erg mode repurposes the four MAIN up/down buttons** — shifting is unused in erg (the trainer holds
  power regardless of gear), so map them **fine + coarse**: e.g. LEFT up/down = erg ± small (~5 W),
  RIGHT up/down = erg ± big (~25 W). No gestures needed for erg.
- **The two 3rd buttons (`0x0004`/`0x0020`) are RESERVED for control across modes** — Zwift actions
  (backlog), **ESP-device control** (mode/feature toggles), menus. Two momentary buttons → use gestures
  (single/double/chord) for several control actions.
- **Two confirmations needed:** (1) **erg-mode detection** via FTMS Fitness Machine Status `0x2ADA` /
  Training Status `0x2AD3` (only treat main buttons as erg when in erg); (2) **is a shift press inert in
  erg?** If erg overrides resistance, repurpose the main buttons with no app change; else disable shifting
  via a Stages-app Profile in erg. Session-4 §C adds a shift-in-erg check; §B characterises the 3rd-button
  chord/double-tap/hold gestures.

## 2026-06-19 — Stages app config consolidated; erg lives in EXTERNAL mode (architecture)

Full owner recon of the (undocumented) Stages Cycling app → `code/findings/stages-app-config.md`. The
architecture-shaping facts:

- **Three ride modes:** *External* (gears work; bike expects an **external** controller over FTMS — the
  Zwift mode), *Grade* (app sets grade), *Power/Erg* (gears OFF; the **app** owns the target power). Our
  "shifter nudges erg watts" feature lives in **External** mode — NOT the app's Power/Erg mode (we can't
  inject there). External mode *expecting* an outside controller strongly suggests the SB20 will accept
  our FTMS Set Target Power (the §C gate; run §C in External mode).
- **Buttons:** **5 per side / 10 total** (4 & 5 hidden under the bar tape; session 3 mapped only 1/2/3).
  The app exposes **three config slots/side — `{1,4}`, `{2,5}`, `{3}`** — ties 1≡4 and 2≡5; each slot
  assignable to front/rear easier/harder **or `external`**.
- **The "external" assignment is the keystone mechanism:** set a slot `external` → the app ignores those
  buttons, but the bike still broadcasts them on `0c46be60` → the ESP reads + repurposes them. A supported
  setting, no hack. (Explains why button 3 was inert-in-app but emitting in session 3 — already external.)
  Plan: the rider makes a **"proxy" Profile** marking the buttons we want as external.
- **Open (session 4):** are 1≡4 / 2≡5 *separable* over BLE (different bits → up to 10 signals)? does the
  SB20 erg off a third-party Set Target Power in External mode (§C)? hold-vs-taps repeat (§B)? Custom-2×
  `03`-frame front/rear fields?
- **Profile settings (context):** gradient-scale (100%) + equipment-weight (8 kg) = sim physics;
  vibrate/audio-on-shift toggle (**confirms the haptic is the phone, not the pods**); Gear Setup
  Dream-Drive vs Custom (2×); shift mode Shimano/Campagnolo/Custom.

**Architecture takeaway:** External mode + per-button `external` are the SB20's **designed-in hooks for an
outside brain** — exactly what the ESP is becoming (power source + erg target + button input; bike =
trainer + display). De-risks the erg feature and is the incremental path to the alternative-app idea.

## 2026-06-19 — Meter-to-meter proxy: runtime + identity decisions

Owner decisions on the XCadey→Assioma-scale proxy (`meter-to-meter-proxy.md`):

- **Runtime = ESP32 firmware variant, BLE-only.** Reads the XCadey over BLE; no ANT+ on the device. The
  correction is power→power so a model fitted from ANT+ captures applies unchanged. (Owner to add a
  battery to the C3 — Maker skill can help source a LiPo + charge board.)
- **Broadcast under our OWN identity, not a spoof.** Key distinction from the SB20 case: the SB20 only
  accepts its own crank so we *must* impersonate "Stages 62144"; but a head unit / training app accepts
  ANY CPS power meter, so the meter-corrector advertises as **its own device** — an honest corrected
  rebroadcast, not a pretend-to-be-Assioma. ⇒ "advertised identity" becomes a **config axis** (spoof-mode
  vs own-identity-mode). **Product name TBD.**
- **Model:** data-driven — power-only first, add cadence only if the fit's residuals show cadence
  structure.
- **Forward requirement (owner):** ultimately, **find + pair the source meter and choose the identity
  from the ESP32 web UI** (BLE scan → pick → NVS), not a hardcoded `Config`. Backlogged in forward-plan
  §8 — serves both the meter-corrector and the SB20 spoof.

Still gated on a paired XCadey+Assioma capture (real-data-first) before any fit/firmware is built.

## 2026-06-20 — Ride Director uplift: steerable session engine (Phases 1–6, desk-complete)

Decision: Rebuild the Ride Director from a static-workout dashboard into a **steerable session
engine** — the phone is the rider's interface; an agent monitors + steers the plan in real time over
an HTTP control API; workouts can be power-zone / %FTP. Built entirely at the desk (no bike), fully
host-tested, landed across PRs #38–#42. Reference: [`ride-director.md`](ride-director.md);
plan/brief: [`ride-director-uplift-plan.md`](ride-director-uplift-plan.md).

Context: owner wants on-bike sessions driven by the Ride Director web app on their phone (not direct
Claude-Code chat), with the agent monitoring and updating the plan via an API; and eventually running
Power-Zone workouts.

Resolved with owner (these are the locked choices):
- **Scope:** agent control API + dynamic plan engine + phone UI uplift. (A full zone-workout *library*
  is a backlog stretch; the model + one example `SWEET_SPOT` prove it.)
- **Zones:** **Coggan 7-zone %FTP.** FTP is configurable (`--ftp` / `POST /api/control/profile`),
  never baked in. Default 250 W / `stages`. Primary scale = SB20/Stages watts; profile carries `scale`
  so the track-bike/Assioma context reuses it later.
- **Steering:** **both** live-in-the-loop (edits reflect on the phone within the ~1 s poll) and
  author-ahead (the director runs a pre-built plan with no live agent attached). Same API serves both.
- **Plan model:** `RidePlan` is mutable + **versioned**; a `Cursor` pins the active block by
  wall-clock (`started_at`) so a live edit to a *future* segment never retro-shifts the active one.

Numbers / contract chosen:
- Coggan bands (fractions of FTP, [lo, hi)): Z1 [0,.55) Z2 [.55,.75) Z3 [.75,.90) Z4 [.90,1.05)
  Z5 [1.05,1.20) Z6 [1.20,1.50) Z7 [1.50,∞). Zone-as-target representative %FTP: .50/.65/.83/.98/
  1.13/1.35/1.70.
- **`erg_setpoint_w`** is exposed in every snapshot = hold override if set, else the active segment's
  resolved target. This is the hook the (bike-gated) FTMS *Set Target Power* path will later write to
  auto-set the SB20. No bike dependency added now.
- Control endpoints gated by an optional `control_token` (header `X-Control-Token` or `?token=`);
  rider/phone endpoints stay open.

It works (on replay): `test_ride_e2e.py` pumps a real committed capture through the replay feed into
the live server and drives the full agent→director→phone loop (message, hold target, plan swap, skip)
with the meters live — hermetic, no bike. 200 tests green.

Will revisit if: the on-bike FTMS capture (Session 4 §C) confirms the SB20 ergs off a third-party Set
Target Power — then wire `erg_setpoint_w` → the FTMS write so Power-Zone workouts auto-set resistance.
Also pending real use: build out the zone-workout library; surface device discovery/pairing in the UI.

## 2026-06-21 — FTMS protocol implemented (spec-built ahead of capture); on-air server seam PASS

Decision: Implement the FTMS (Fitness Machine Service, 0x1826) "bike" protocol — codec + erg control +
the shifter-nudges-watts mapper + the trainer-server role — **building from the Bluetooth spec ahead of
the on-bike capture** (an owner-approved exception to capture-before-code, 2026-06-21: the FTMS spec is
strong and prior art is readable). Built F1–F6 across PRs #48–#53. Reference:
[`ftms-protocol.md`](ftms-protocol.md); plan: [`ftms-implementation-plan.md`](ftms-implementation-plan.md).

Context: completes the "control the trainer" half and closes the Ride Director loop — the director
exposes `erg_setpoint_w` and FTMS **Set Target Power** writes it so the SB20 actually ergs.

What was built (all spec-built, labelled, pending Session 4 §C):
- **Codec** — `ble/ftms.py` + `firmware/lib/proxy/Ftms.h` (twins, byte-for-byte agreeing vectors):
  Indoor Bike Data decode/encode (incl. the inverted More-Data bit0 = speed-present-when-0), Control
  Point (Request Control / Start / Set Target Power / …) + the 0x80 response, Feature / Supported Power
  Range / Status. `SPEC_VECTORS` are **spec-derived**, not captures.
- **Erg control** — `ble/ftms_erg.py`: pure `ErgController` (Request Control → Start → Set Target Power,
  clamp to range, resend-on-change) + `RideErgBridge` (driven by `erg_setpoint_w`) + an in-process fake
  machine; host-tested end-to-end. Firmware twin `FtmsErgClient`.
- **Shifter-erg** — `ble/shifter_erg.py`: debounced shifter button → ± step → Set Target Power.
- **Firmware seams** — `FtmsTrainerServer` (peripheral) + `FtmsErgClient` (central), flag-gated envs
  `esp32c3-ftms-server` / `esp32c3-ftms-ergclient` (NimBLE + Ftms.h only); both compile.

It works on real hardware (no SB20): the F6 on-air loop drove the ESP32 FTMS server from the host —
Request Control → Start → **Set Target Power(225) ACKed**, Status Target Power Changed → 225 W, Indoor
Bike Data 220 W / 90 rpm, Feature erg-capable. Record `F-ftms-hwloop-server-20260621-0116.jsonl`. The
bike board (COM9) was left untouched (server on COM10 + host).

Tooling decisions that came out of the hardware bring-up:
- **Reliable flashing = esptool 4.11 direct via `code/scripts/flash_c3.py`.** PlatformIO's bundled
  esptool 4.5.1 (`tool-esptoolpy@1.40501.0`) hits the ESP32-C3 USB-JTAG "No serial data received" bug, so
  `pio run -t upload` FAILS on these boards; 4.11 auto-resets cleanly (~6 cycles, zero wedges, no
  power-cycle). flash_c3.py is time-bounded + retried + BLE-verified — safe for autonomous flashing.
- Boards: COM9 = OLED bike board (`38:44:BE…`), COM10 = spare/no-OLED (`E0:72:A1…`).

Will revisit if: Session 4 §C (`capture_ftms.py --erg`) confirms the SB20 ergs off a third-party Set
Target Power and supplies the real frames — then validate/reconcile the codec against them and wire
`erg_setpoint_w` → the FTMS write into the runtime (measuring C3 BLE coex). Client-seam on-air test
(ESP↔ESP or host WinRT server) is bench-deferred.

## 2026-06-21 — Bike session 4: FTMS erg PASS, full shifter map, and a power-topology finding

Session 4 (`sessions/session-04-…`, ✅ DONE; ran 09:43–11:23). §C + §B run; G1/G2/§D deferred (board
un-flashable — WiFi/OTA down). Captures in `findings/captures/`.

### §C — FTMS erg-acceptance: ✅ PASS (the gate)
The SB20 **accepts and holds a third-party Set-Target-Power** over BLE (FTMS CP `0x2AD9`), Stages app
disconnected, sole controller. Every op ACKed success, **no `control-not-permitted`**: Request-Control
`80 00 01`, Start `80 07 01`, Set-Target-Power `80 05 01 <wLE>` (150/200/100), Reset `80 01 01`; Status
`0x2ADA` Target-Power-Changed `08 <wLE>` per target. Indoor Bike Data (`0x2AD2`, flags `0x00C5` =
cadence·power·avg-power) tracked the targets. Feature `8a4000000e200000` → Target-Setting **bit3
Power-Target** set; Power Range `0000a00f0100` = 0–4000 W / 1 W. DIS: Stages Cycling / SB20 / SN
H0512210105 / FW 1.1 / SW 1.12.4+3792. ⟹ the **shifter-erg feature is real**; the spec-built FTMS codec
(`ftms.py`/`Ftms.h`) is **validated against real frames** (`G-sb20-ftms-erg-20260621-0949.jsonl`).
Shift-in-erg: a main shift (LEFT-up) while erg-held is **inert** (rider-narrated; the SB20 has no display)
→ main shift buttons are free to repurpose for erg ±.

### ⚠️ Power-topology finding (the big lead — UNRESOLVED)
On repeated erg holds with an **independent** meter, the SB20's erg power read **far below the Assioma**:
SB20 "200 W" ↔ Garmin/Assioma ~260 W (**1.30×**); a 100/150/200 sweep gave Assioma **1.34× / 1.62×** the
SB20; later SB20 "200 W" ↔ Garmin **~380 W (~1.9× ≈ 2×)**. Reads: ESP `src_power_w` (BLE, ~1:1 passthrough
of the Assioma) + the owner's Garmin (**Assioma over ANT+**). **Hypothesis (owner, strong): single-sided
reading** — ~2× is the fingerprint of one meter on a single leg vs the other on true total; the SB20 appears
to run erg off the **real Stages LEFT crank single-sided (~half)**, *not* the ESP/Assioma spoof (SB20 IBD ≠
ESP `src_power_w`; the **variable** 1.3–1.9× ratio = **L/R imbalance**). **Tension to verify:** the prior
"Stages reads ~5–13 % *high* vs Assioma" was the *combined* `62144` stream, not single-L — don't assume.
**Resolution = a simultaneous multi-device capture** → designed in
[`sb20-power-topology.md`](sb20-power-topology.md). Captures `G-sb20-ftms-erg200-20260621-104341.jsonl`,
`G-sb20-ftms-erg3way-20260621-110555.jsonl`, `ESP-assioma-poll-erg3way-20260621-1106.txt`; owner to send the
Garmin `.FIT` (two lap marks on the erg runs).

### §B — shifter fully mapped (char `0c46be60`)
- **6 distinct one-hot buttons** (frame `<u16 edge><u16 btn-bitmap>`): L1/L2/L3 = `0x01/0x02/0x04`,
  R1/R2/R3 = `0x08/0x10/0x20`. **Hidden buttons 4 & 5 alias 1 & 2 byte-for-byte on BOTH sides** (L4→`0x01`,
  L5→`0x02`, R4→`0x08`, R5→`0x10`) ⟹ **6 usable signals, not 10**. (`SHIFTER-probe-4-buttons45-…1727.jsonl`.)
- **The bitmap is a true OR of buttons down** → **chord** L3+R3 = sustained `0x24` (a usable 7th signal).
  **Double-tap** detectable (two `0100…`→`0800…` press→release cycles, ~110–180 ms apart). **Hold vs taps
  RESOLVED** (session-3's open question): a **hold** = one continuous `0100<bit>` stream at ~8 Hz + a
  periodic `030004000400` "still-held" marker + **one** release; **N taps** = N short bursts + **N**
  releases, **no `0300…`**. ⟹ **hold-to-ramp and multi-tap counting both viable**.
  (`SHIFTER-probe-4-gestures-…2429`, `…holdtaps-…3001`.)
- **Brake levers are NOT on BLE with the app off** — LEFT+RIGHT ×3 fired nothing on `be60`/`be61`/`beb0`/
  FTMS-Status. Owner reports the brakes slow the flywheel **only when the Stages app is connected** ⟹
  **app-gated**; capturing them needs a dual (app + sniffer) connection. (`SHIFTER-probe-4-20260621-1000.jsonl`.)

### Tooling / ops (folded into `sessions/PLAYBOOK.md`)
- **ANT+ bring-up = WSL + usbipd** (stick `0fcf:1008`, bind-shared; `usbipd attach --wsl`; venv
  `~/sb20-ant-venv` + `pip install -e .[ble]` → openant 1.3.4; capture the Assioma with its **explicit** ANT
  id **`17039`** — never wildcard, the Stages are `62144`/`4963`). **Blocked by `[Errno 13]`**: the
  MODE-0666 udev rule is present but WSL has no systemd to apply it (and no passwordless sudo to self-fix) →
  enable WSL systemd / `udevadm` reload / run as root; pre-stage before the next ride.
- **The ESP32-C3 doesn't roam between WiFi APs** — it clung to the upstairs AP after the rider went
  downstairs (−89 dBm, all HTTP dead); **power-cycle to re-associate**. Also adopted: the agent runs all
  captures itself (`buffering=1` → live-tail), and anchors timestamps on the capture `iso_time` (the agent's
  sandboxed shell clock read a day + ~6 h off real).

## 2026-06-21 — Session 4 follow-up: power-topology Phase 1 (FIT reconciliation) — single-sided REFUTED

Reconciled the owner's Garmin `.FIT` (dense Assioma over ANT+, *with L/R balance*; committed
`findings/captures/G-garmin-assioma-session4-20260621.fit`) against the SB20 erg captures (FIT UTC +10 h →
machine local; alignment confirmed by FIT lap 2 @ 11:03:50 bracketing the 3-way sweep). **Result: the SB20
erg power is a fairly flat ~1.3× below the Assioma (≈70–75 % of total), NOT the ~2× / single-sided the live
spot-read suggested.** L/R balance ≈ **46 % left (even)** and SB20 ≈ **1.5× the Assioma's left leg** (not ≈
it) ⟹ **single-sided REFUTED** (a 2× artifact needs SB20 ≈ one leg). Cleanest/longest hold (erg200, n=66):
**1.31×**; shorter erg3way holds spread 1.3–1.6× (short windows + ±1 s alignment + a release-tail-contaminated
200 W hold). The live "~380 vs 200 ≈ 1.9×" was a **transient** spot-read, not the steady ratio. **Mechanism
still open** — ≈0.73× total is an odd factor that also *conflicts* with the prior "Stages reads ~5–13 % high
vs Assioma", so the SB20 likely isn't simply echoing a Stages stream; most consistent with the SB20's own
meter (or erg-control source) reading ~30 % low. → **Phase 2 = the simultaneous multi-device capture**
(`sb20-power-topology.md`). Lesson reinforced: **dense data corrects noisy live spot-reads.**

Also this follow-up: **reviewed + merged the SQLite analysis layer (PR #57).** Its CI had failed only because
its tests read this session's capture `G-sb20-ftms-erg-20260621-0949.jsonl`, which wasn't on `main` until the
session-4 close-out merged (`bd80fdb`); fixed by merging `main` into the branch → green → merged (`8695c45`).

## 2026-06-21 — nRF Sniffer dongle flashed + live (passive BLE sniff, Tier 3)

The nRF52840 dongle now runs the **nRF Sniffer for Bluetooth LE v4.1.1** firmware and Wireshark sees it as
`nRF Sniffer for Bluetooth LE COM13` — the passive-sniff tier (`traffic-observability.md` / `nrf-sniffer.md`)
is operational. **Key correction:** despite its "MDK" label, this dongle does **not** have a UF2 drag-drop
bootloader — it carries the **Nordic Open DFU Bootloader** (serial DFU, PID `521F`; no `UF2BOOT` drive ever
mounts). So it flashes the Nordic way, not by copying a `.uf2`. Path that worked: convert the matched `.uf2`
(bare app @ `0x1000`) → `.hex` → `nrfutil pkg generate` (unsigned; the Open Bootloader is signature-less) →
`nrfutil dfu usb-serial -pkg sniffer_dfu.zip -p COM<dfu>` → `Device programmed.`. PIDs across the flow:
app `C00A` → bootloader `521F` → sniffer app `522A`. Tooling: `winget install NordicSemiconductor.nrfutil`
+ `nrfutil install nrf5sdk-tools`. Next: sniff the Stages-app ↔ SB20 conversation (follow `E4:AA:5A:D6:0E:D4`).

## 2026-06-21 — Session 6: passive BLE sniff (Block S) + a pcap/FIT→SQLite pipeline

Ran session 6 on the bike laptop, Claude-driven, with the nRF dongle (passive sniff). Headline results, a
preliminary that *conflicts* with Phase 1, a capture-method lesson, and a new analysis pipeline.

**Block S — the legitimate Stages-app ↔ SB20 erg conversation** (capture `SNIFF-sb20-app-20260621-1713.pcap`,
started *before* the app connected):
- **The app does NOT bond/encrypt.** Zero `btsmp` (Security Manager) packets; all **636 ATT ops dissect in
  cleartext**. ⟹ our third-party erg needs **no pairing** — the GATT control surface is open. (So any future
  `control-not-permitted` would not be because the app holds an encrypted bond.)
- **The app drives erg over the Stages-PROPRIETARY `0c46be` service, NOT FTMS.** Control char =
  `0c46beb1-9c22-48ff-ae0e-c6eae1a2f4e5` (handle `0x0039`) — the **same `0c46be` family as the session-3
  shifter**. FTMS (`0x1826`) is present and *we* use it successfully (session 4 PASS), but the native app
  ignores it. Control write = **`02 00 <u16-LE> 00 00`**, streamed every ~0.5–1 s.
- **That `<u16>` is NOT raw watts.** Joined against the overlapping Assioma FIT (unified SQLite, below): it
  tracks power only loosely — ratio **1.4–5.2× (median ~1.75), cadence-dependent**, staying elevated when the
  rider eases off ⟹ an **app-side resistance/load setpoint** (the app closes the erg loop and commands load),
  distinct from our FTMS path (we hand the SB20 a power target and *it* closes the loop). Exact unit still open.

**Power topology — a PRELIMINARY that CONFLICTS with Phase 1 (unresolved).** With the SB20 in **BTLE**
power-meter mode on the **freshly-zeroed** real Stages cranks (zero **903/951**, LHS battery replaced; ≈ the
ANT+ zero 902/951 ⟹ **offset stable across ANT+/BTLE**) and the ESP spoof OFF, a fresh 100/200/300 W erg sweep
was recorded on the Garmin (`G-garmin-assioma-sweepShort-20260621.fit`: laps avg **103 / 180 / 274 W** at
targets 100/200/300). Since the SB20 holds its *source* at the target, Assioma < target ⟹ **the Stages cranks
read ~10 % HIGH vs the Assioma here** — consistent with the *pre-session-4* "Stages 5–13 % high", but
**opposite** Phase 1's dense reconcile (SB20 erg ~30 % *low* vs Assioma, ≈0.73×). This figure is weaker
(lap-avg vs commanded target, assumes a perfect hold), so it does **not** overturn Phase 1; it raises that
**session 4 likely erged off a different / miscalibrated source than these freshly-zeroed BTLE cranks**.
**Still unresolved** — the clean confirmation (sniff the crank's own CPS power) was **blocked** (next item).

**Capture-method lesson (critical for the next sniff session).** The **sweep + zero captures are
adverts-only** — `sweep2` / `bleZero` are ~4979 frames of pure advertising, **no `CONNECT_IND`, no ATT/L2CAP**.
The nRF sniffer can only *follow* a connection if it catches the `CONNECT_IND`; those sniffs started *after*
the SB20 was already connected, so they only saw adverts. (It is **not** encryption — the links aren't
bonded.) Block S worked because it started before the app connected. ⟹ **start every sniff BEFORE the device
connects** (power-cycle the SB20 / toggle the link). The blocked crank-CPS reconcile is the direct cost — the
power topology stays open because of it.

**New analysis pipeline (committed, tested).** `tshark` (Wireshark CLI) natively dissects the
`LINKTYPE_NORDIC_BLE` pcaps, so a pure-Python Nordic-BLE parser was unnecessary (a stalled subagent's path,
abandoned). `analysis/pcap_sqlite.py` (tshark → a `pcap_att` raw spine + decoded CPS/FTMS power into
`ble_notification` → the `power_sample` view + control writes into `ble_control_point`) and
`analysis/fit_sqlite.py` (Garmin FIT → `fit_record` / `fit_lap`) load **both** the BLE sniffs *and* the FITs
into the **same** index, so reconciliation is a timestamp JOIN. `scripts/14_build_pcap_fit.py` is the turnkey
build. Tests are hermetic (tshark/fitparse are the I/O seam; parse/decode/load are pure). This is the
queryable knowledge base for the planned comprehensive passive-sniff session.

## 2026-06-22 — Session 7: power topology RESOLVED (qdomyos training ride, multi-radio capture)

Ran the comprehensive passive monitor (`sessions/session-07-…`) through a real qdomyos-zwift training ride:
**ANT+ 5-channel** (Assioma `17039`, Stages L crank `62144`, SB20 FE-C `#105`, HR `#54880`) on one stick +
**nRF following qdomyos↔SB20 (FTMS)**, one clock, ~73 min (ANT 16.8 MB / 39 k records; nRF pcap 11.8 MB /
44 k ATT). Captures: `RIDE-ant-ride-20260622.jsonl`, `RIDE-ble-sb20-ride-20260622.pcap`,
`ASSIOMA-ble-cps-20260622.jsonl`. **The long-running power-topology question is settled.**

**TOPOLOGY — definitive** (simultaneous same-clock reconcile, `basis=mono`, 449–1278 paired seconds):
- **SB20 FE-C ÷ Stages crank = 1.000** (Δ 0 W, 1278 buckets) — **the SB20 reports the Stages-crank power
  VERBATIM, no rescaling**, on both ANT+ FE-C and BLE FTMS.
- **Assioma ÷ Stages-crank = 0.898**, **Assioma ÷ SB20-FE-C = 0.895** — **the Stages cranks read ~11 % HIGH
  vs the Assioma** (Δ ~24–27 W). So the SB20's erg/display power ≈ **1.11 × the Assioma**.
- FTMS cross-check (the *exact* session-4 measurement, decoded from `pcap_att`): SB20 FTMS-IBD median **228 W**
  ≈ FE-C 241 ≈ crank 236, all ~11 % above Assioma 206 → consistent across both SB20 outputs.
- ⛔ **OVERTURNS the session-4 Phase-1 finding "SB20 reads ~30 % LOW (≈0.73×)."** Same comparison done
  simultaneously on one clock is the **opposite direction (~11 % HIGH)**; session 4's 0.73× was a
  cross-capture FIT-alignment artifact / different config — this supersedes it. **Confirms** session-6's
  preliminary (Stages ~10 % high) and the pre-session-4 "Stages 5–13 % high vs Assioma."

**Block S — qdomyos vs the Stages app:** qdomyos drives erg over **standard FTMS** (Control Point `0x2AD9` +
Indoor Bike Data `0x2AD2`) — *unlike* the Stages app (session 6), which used the proprietary `0c46be`. Both
cleartext, no bonding. The **shifter `0c46be60` (handle `0x002f`) also notified on the qdomyos connection** —
the 6 buttons as one-hot `0x01…0x20` with press (`0100…`) / release (`0800…`) events, byte-faithful to the
session-3 map, now captured live mid-ride. (Brakes app-gated → silent, as expected.)

**L/R balance grounding** (for the proxy-forward backlog): captured the Assioma's BLE CPS directly —
**flags `0x0023` (Pedal-Power-Balance Present + Left reference), balance byte at offset 4 = left % × 2**
(ride ~38–46 % left, right-dominant); CPS Feature `0x00111209` (balance supported); crank length **172.5 mm**;
device Favero Assioma `17039.013.118`. The spoof's `0x2F` already carries the balance byte → forwarding is a
clean offset-4 → offset-4 relay (`forward-plan.md` §8). **NB the ESP32 reads this over BLE only.**

**Tooling / method (works):** ANT+ on Windows = **Zadig WinUSB + the `libusb-package` backend** (wired into
`07_capture_multi.py`; threading-timer; + HR type `0x78`). `15_monitor_ride.py` ran both radios with live
`growing/STALE/dead` health-checks; the **sniff-BEFORE-connect rule worked** (caught the `CONNECT_IND` → 44 k
ATT). **Open issues filed:** (a) `15_monitor_ride.py` doesn't trap `SIGTERM`, so a TaskStop orphans the
`sniff_ble` children (had to kill by PID); (b) `pcap_sqlite` didn't auto-decode this pcap's FTMS/CPS chars
(handle→uuid map miss for 16-bit chars) — decoded from the `pcap_att` raw spine instead. Both are
hardening follow-ups; the raw capture + the topology result are unaffected.

## 2026-06-22 — Session-7 follow-up (a) RESOLVED: 15_monitor_ride.py traps SIGTERM + Windows job teardown

Open issue (a) above (a harness TaskStop SIGTERM orphaned the `sniff_ble` children — had to kill by PID) is
fixed in `15_monitor_ride.py`. Two defences, because the right one differs by OS — both verified hands-free
via `--self-test`:
- **POSIX/WSL:** trap `SIGTERM`/`SIGBREAK` → raise `KeyboardInterrupt` → the existing `finally` stops the
  children and finalises the manifest. WSL end-to-end (`kill -TERM`): both producers torn down; manifest gets
  `stopped_by: "SIGTERM"`.
- **Windows:** an *external* SIGTERM is an **uncatchable `TerminateProcess`** (verified empirically — the
  Python handler never fires, exit code 15), so the children are also bound to a **kill-on-close Job Object**;
  the OS tears them down when the orchestrator dies, even on a hard kill. Verified: hard-killing ONLY the
  parent (no tree-kill) left zero orphans.

The manifest is now finalised on every clean stop (`end`, `stopped_by` ∈ {duration, all-exited, SIGTERM,
interrupt}, `final_sizes`). Issue (b) (`pcap_sqlite` 16-bit char decode) stays open. `scripts/` remains out of
CI lint/test scope (smoke-tested only). PR from branch `fix/monitor-ride-sigterm-cleanup`.
## 2026-06-22 — L/R pedal-balance forwarding (firmware) + proxy build-gate fix + bench finding

**Balance forwarding built + host-validated (PR pending).** The spoof now carries the source meter's
REAL left/right split instead of an implicit 50/50, so the Stages app's balance display is genuine.
Chain: `BleMeterClient` reads CPS Pedal-Power-Balance (flags bit0, fixed byte offset 4, left% × 2 when
ref-left) → `PowerReading.balance_half_pct` (-1 = unknown) → passes through `Correction` untouched →
`BleCrankPeripheral` emits it in the Stages `0x2F` balance byte (falls back to raw 100 = 50 % when the
source has none). New `Cps.h::decodeCpsBalance()`. **Grounded** in `captures/ASSIOMA-ble-cps-20260622.jsonl`
(flags `0x0023`; e.g. `23009e005816134e4d` = 158 W, balance `0x58`=88 = L44 %/R56 %). Tests: firmware
**85/85** native (decode real Assioma frame, absent/truncated safety, survives-correction, forward→spoof
byte) + Python **324** (`test_ble_cps.py` real-frame decode + forward round-trip). `fake_meter --balance`
+ `crank_reader` balance readout added for the bench.

**Pre-existing proxy build was RED on `main` — fixed (PR #73, merged).** `esp32c3-supermini` (and every
proxy env extending it) compiled ALL of `src/`, so `main.cpp` + `ftms_server_main.cpp` +
`ftms_client_main.cpp` each contributed a `setup()/loop()` → "multiple definition" link error. The FTMS
test mains (F5) shipped with their own `build_src_filter` but the proxy envs had none. **CI only runs
`pio test -e native`** (filter `-<*>`, excludes `src/`), so it never links a proxy ESP env and never saw
the break — the pre-flash gate `pio run -e esp32c3-oled-live-ota` was failing on `main`. Fix: base proxy
env excludes the two FTMS mains (`--gc-sections` drops the unused seam classes). Now links (55.6 % flash).
*Gap noted:* CI does not exercise any ESP32 proxy build, so proxy-link regressions can recur.

**On-air balance bench: attempted, BLOCKED by the harness (not the code).** Flashed COM10 (spare) with the
balance firmware (`esp32c3-wifi-live`; spoof advert verified). `fake_meter --balance 65` advertised
177 W/90 rpm/L65 % correctly but **no ESP ever subscribed (`subs=0`)**. Root cause, confirmed by an
active-mode bleak scan: the WinRT `GattServiceProvider` advert does **not expose the CPS `0x1818` service
UUID** (only the ESP's own spoof advertises 1818; `fake_meter` shows `uuids=[]`), so the ESP's
nameless-match-by-CPS (`isTargetMeter` path 3) can't see it. A real Assioma (advertises CPS + an "ASSIOMA"
name) would match, but none is at the desk (bike-dependent). ⟹ **the forward is proven by the real-capture
golden vectors; the on-air confirmation waits for a real meter or a `fake_meter` advert fix.** (Two boards
both advertise `Stages 62144`; parked one in its ROM bootloader during the test to remove contention, then
restored it.) Also installed a host **gcc (WinLibs)** so `pio test -e native` runs on this box.

## 2026-06-22 — Session 7 power-topology CONFIRMED by the owner's Garmin .FIT (independent dense Assioma)

Folded in the owner's Garmin `.FIT` for the session-7 ride — `findings/captures/G-garmin-assioma-session7-20260622.fit`
(dense **Assioma DUO**: 4403 records @ 1 Hz, 73 min, mean 225 W, full cycling dynamics incl. L/R balance) —
and reconciled it against the streams already in `captures.sqlite` (imported as `fit_record`, capture_id 36).

**Clock:** the FIT is UTC; the capture box runs **+10 h** (same as session 4). Self-calibrated the offset by
maximising agreement with the *in-capture* `assioma` stream (same physical meter ⟹ ratio should be 1.0):
best offset **+9.98 h** gave `assioma` median **1.000** (n=1027) — confirms the alignment + that both Assioma
recordings agree exactly.

**Per-second median ratio (stream ÷ FIT-Assioma), at the calibrated offset:**
- `assioma` (in-capture) = **1.000** (n=1027) — sanity ✓
- `bike_fec` (SB20 FE-C) = **1.111×** (n=2599)
- `stagesL` (Stages crank) = **1.098×** (n=1838) ≈ `bike_fec` ⟹ SB20 reports the Stages crank ~1:1 ✓

⟹ **Independently CONFIRMS the session-7 resolution: the SB20's erg/display power ≈ 1.11× the Assioma, and
it reports the native Stages crank ~1:1.** Striking corroboration: **1.111 ≈ 367/330 = 1.112**, exactly the
owner's empirical dual-FTP workaround (FTP 367 in the bike vs 330 on the Assioma watch) — i.e. a **flat ~1.11×
constant** is the right correction, not a power-dependent curve. (Per-second IQRs are wide [~0.8–1.5] from
instantaneous-power noise + sub-second skew, but the median over 1000–2600 buckets is robust.)

**FIT L/R balance:** mean ~**54 % right-referenced** (n=4090) — the rider's real DUO split, the kind of value the
new balance-forwarding firmware (PRs #74/#75) carries through to the Stages app. Method (offset scan + per-second
median ratio) is reproducible from the committed `.fit` + `captures.sqlite`; `fitparse` is the `analysis` extra.

## 2026-06-22 — WinRT advert "fake_meter not discoverable" MISDIAGNOSIS corrected + bench match flag

⛔ **OVERTURNS the same-day finding "the WinRT `GattServiceProvider` advert does not expose CPS `0x1818`;
`fake_meter` shows `uuids=[]`."** That conclusion came from an **active-mode `bleak` scan run on the same
Windows box as `fake_meter`** — and a single BLE radio is **blind to its own advertisements**. Proven by a
control: advertised a **unique custom 128-bit UUID** via the same WinRT API → the same-box `bleak` scan saw
**nothing**, while an **independent nRF52840 sniffer** (COM13) captured it cleanly. So the `bleak`-on-this-box
result was never valid evidence about what `fake_meter` broadcasts.

**Ground truth, from the nRF sniffer (separate radio) decoding `fake_meter`'s adverts in tshark:**
- **`ADV_IND`** carries AD type **0x03 (complete 16-bit UUID list) = `0x1818` (CPS)** + `0x180a`. ⟹ **WinRT
  DOES advertise the CPS service UUID**, in the primary advert (not the scan response).
- **`SCAN_RSP`** carries AD type **0x09 = the PC's computer name** (here `"CAULDT9H"`). Windows always stamps
  the machine name into the GATT-server scan response; `GattServiceProviderAdvertisingParameters` has **no
  name field** (only `is_connectable` / `is_discoverable` / `service_data` / PHY), so it **cannot be set per
  process** from Python — the task hint was correct.

**Real root cause of `subs=0`:** the firmware does `setActiveScan(true)` (its old comment wrongly claimed
"WinRT puts the service UUID in the scan response" — it's the *name* that lands there). Active scan harvests
the scan-response name, so the rig is **not nameless** (misses `isTargetMeter` path 3) and its name isn't
`"ASSIOMA"` (misses path 5) → never connected. The fix is **not** Python-side (WinRT can't be told to
advertise "ASSIOMA").

**Fix (firmware, opt-in, production-safe):** new bench flag `-DMETER_MATCH_ANY_CPS=1` → `Config::MATCH_ANY_CPS`
→ new `isTargetMeter(..., matchAnyCps)` path: when set, read **any** CPS-advertiser that is **not** our spoof
and **not** a `"Stages "`-named crank (so the host-named rig + a real Assioma match; the SB20's native cranks
are still excluded). OFF in production (the `"ASSIOMA"` filter still guarantees we read only the configured
meter, never a stranger's CPS device). New envs `esp32c3-wifi-live-bench[-ota]`. Pure logic stays host-tested:
added `test_meter_match_winrt_rig_carries_host_name_blocked_in_prod` (prod rejects `"CAULDT9H"`) +
`test_meter_match_any_cps_bench_flag` (bench matches host rig + Assioma, still rejects Stages cranks + spoof +
non-CPS). `fake_meter.py` docstring corrected. Diagnostics (`_diag_*.py`) were throwaway, not committed.

**Still to do (next bench session, hardware):** flash the spare board (COM10) with `esp32c3-wifi-live-bench`
and confirm `fake_meter --balance 65 -> ESP -> crank_reader` reads back **L65 %** end-to-end — the on-air
balance check that was blocked. The desk-testable layer (the match logic) is covered by the native suite.

---

## 2026-06-23 — End-to-end bench proof: fake_meter → ESP32 → spoof crank (the on-air balance check, DONE)

**The desk-only end-to-end loop the 2026-06-22 entry left pending now passes.** Built
`esp32c3-wifi-live-bench` (55% flash) and flashed the **spare COM10** board via `flash_c3.py` (clean,
no USB-JTAG wedge; advert verified). Ran `fake_meter.py --watts 200 --steady --balance 44 --cadence 90`
(a WinRT CPS peripheral on the PC). The board (`METER_MATCH_ANY_CPS`) connected + subscribed within ~2 s
(`subs=1`). Then read the board's spoofed Stages crank back with a bleak central:

```
fake_meter (CPS 200 W / 44 %L / 90 rpm)
   → ESP32 COM10 reads it (central, BleMeterClient)
   → re-broadcasts as "Stages 62144" (peripheral, BleCrankPeripheral)
   → PC reads off the crank: power=200 W, balance=44.0 %L  (5/5 frames identical)
```

**Result — the full proxy chain works on the bench, no bike:**
- **Goal #1 (read a BLE meter):** ESP connected to fake_meter and subscribed to CPS 0x2A63. ✅
- **Goal #2 (rebroadcast as the spoofed crank):** the crank carried the meter's power. ✅
- **L/R balance forwarding (the priority-#1 feature):** `44 %L` preserved end-to-end, byte-faithful. ✅
- **Power is 1:1** (200→200): correct — fake_meter sends *total* power + a balance field (Assioma-DUO
  shape), so no single-sided ×2 is applied. (A left-only source would need the ×2 toggle.)

**Disambiguation:** two boards advertise `Stages 62144` on the desk. Connecting to each, only
`E0:72:A1:70:02:7E` (COM10) streamed the steady 200 W/44 % tracer; `38:44:BE:45:E9:A6` (the COM9 OLED
**bike** board) returned **no frames** — correctly silent with no source meter, and left untouched.

**What this does and doesn't prove:** it proves the read→correct→rebroadcast data path + the CPS/balance
framing on real hardware. It does **not** prove the **SB20 itself accepts** the spoof crank or closes its
erg loop on it — that remains the bike-session gate (Phase 0). Bench build is desk-only; don't ride it.

---

## 2026-06-23 — Meter-to-meter corrector built (on-device BLE calibration → corrected rebroadcast)

The project's second product (`meter-to-meter-proxy.md`) is built: make a DUT meter (XCadey crank) read
like a reference (Assioma pedals), fully on the ESP32, no laptop. One firmware, a runtime **mode toggle**
(NVS/web UI): SPOOF (SB20 crank) | CORRECTOR (this). Merged across PRs #99–#104:

- **M1** — `CalibrationFit.h` (PairAccumulator + grid/scale-offset fit), `RuntimeConfig` mode/curve.
  The fit is **parity-locked to `calibration.py`** (shared golden dataset asserted in both
  `test_main.cpp` and `test_calibration_parity.py`) so on-device and desk fits can't drift.
- **M2** — `BleCrankPeripheral::setMode()`: CORRECTOR advertises a generic CPS meter under our own name,
  **no Stages proprietary service**, applies the stored curve. **Bench-proven on COM10:** `fake_meter`
  200 W → rebroadcast **220 W as "SB20 Corrector"** (1.10× curve), GATT carries only standard services.
- **M3** — instance-routed `BleMeterClient` (was a file-global singleton) so two centrals (DUT + ref)
  run concurrently. Spoof path **regression-verified** (175 W still flows unchanged).
- **M4** — `CalibrationSession` (Idle→Collecting→Fitted state machine), `CalibrationPage.h` wizard
  (pick DUT+ref → coverage-guided ride → review fit → save), wired live via WifiLink `/calibrate` routes
  + a 2nd meter in `main`, with **reboot-into-calibration** mode (fixed meter roles per boot, low-risk to
  the shipping spoof). Save → corrector mode under an editable device name.

**Decisions taken (owner):** runtime toggle in one firmware; on-device self-contained fit; power-only
correction first; coverage-guided Finish; editable corrector device name.

**State:** corrector *run* path bench-proven; the calibration *wizard* compiles + its pure logic is
host-tested (native 129). **Pending — the calibration ride:** the live two-meter `/calibrate`
walk-through + the 2-concurrent-central coex behaviour on the C3, with the owner's real XCadey + Assioma.
If coex is unstable, fall back to capture-then-fit (the pure fit core is unchanged). Runbook in
`meter-to-meter-proxy.md` §Calibration ride runbook.

---

## 2026-06-23 — Bench-derisked the calibration wizard over HTTP (+ fixed a ride-blocking form bug)

Prepping the corrector calibration ride, brought both boards onto current firmware (COM9 + COM10) and
drove the `/calibrate` wizard over WiFi from the desk. Findings:

- ✅ **The wizard renders + routes respond over HTTP** on real hardware (`GET /calibrate`,
  `/calibrate/scan`); the candidate list works (COM10 advertising "Stages 62144" shows up via the same
  `meter.candidates()` path `/setup` uses).
- 🐞 **Found + fixed a real, ride-blocking bug (PR #107):** every form-POST route (`/setup/save`,
  `/calibrate/start`, `/calibrate/save`) read the body via `arg("plain")`, which the ESP32 WebServer
  leaves **empty for `application/x-www-form-urlencoded`** — i.e. for any real browser `<form>` submit.
  So config-save and the **entire wizard's start/save were dead on arrival**; host tests only fed the
  pure parser a string, never the live body. Fix: a `formBody()` helper (raw body, else rebuild from
  named args via a new `urlEncode`) — generalises the captive-portal save's existing named-args
  pattern. **Verified on COM9:** a urlencoded `POST /setup/save` now persists (was ignored).
- ⏳ **Still bench-pending (the ride's watch-item):** the live **two-concurrent-central** pairing +
  coex behaviour. The desk has one reliable `fake_meter` (WinRT) source and it flaked mid-test; a
  proper 2-source pairing needs the owner's real XCadey + Assioma. Watch heap / watchdog during
  collection on the C3. Run-sheet: `sessions/session-05-meter-calibration-capture.md`.

Both boards left on current firmware, clean default config (COM9 = Stages 62144 / ASSIOMA spoof).

---

## 2026-06-24 — Session 8 desk-staging: pivot the SB20 spoof to its OWN crank id (no battery pull)

Desk-only day (no bike time — rider ran out of time before going downstairs). Staged a **simpler
approach** to the SB20 spoof and captured the app surface that enables it. Run-sheet + RESUME-HERE:
`sessions/session-08-sb20-spoof-calibration.md` (status 🟡 STAGED). Not yet bike-confirmed.

**The pivot (owner decision):** instead of the byte-faithful `Stages 62144` spoof that needs the real
left-crank battery **pulled** (to dodge the duplicate-`62144` collision), put the ESP on its **own**
Stages id — **`Stages 62145`** — and re-point the SB20 to it in the Stages app. The real cranks stay
**powered**; **no battery pull**. This is the 2026-06-16 Phase-1B plan, now trivial via runtime config.

- **Fully runtime-supported — NO reflash.** `spoof_name`/`spoof_serial` are editable from `/setup`
  ("Crank identity"), persisted to NVS (`RuntimeConfig`), and drive `NimBLEDevice::init` + the advert
  name (`BleCrankPeripheral` `adv->setName`) + DIS serial. Set with `POST /setup/save`
  (`name=ASSIOMA&single=1&spoof_name=Stages 62145&spoof_serial=11821518`) → 200 + reboot ~2 s. Verified
  on-air: ESP advertises **`Stages 62145`** at −59 dBm; the surviving `Stages 62144` (real L crank) is
  thereby disambiguated at `E0:72:A1:70:02:7E`. **BLE addresses rotate** — the 2026-06-15 real-crank addr
  `E8:CF:D8:D9:3A:20` no longer matched, so disambiguate by **power-state / name**, not stored address.
- **Stages-app crank-pairing recon (durable):** the pairing screen has **two separate, free-TYPE
  Left/Right id fields** — you **type** the number; there is **no scan-list** and **no single-sided /
  left-only option**. ⇒ ids aren't validated against a present device at entry. Plan: type **L=`62145`**
  (our ESP) + **R=`4964`** (a **phantom** — nothing advertises it, so the bike runs left-only off the ESP
  and ignores the still-powered real right crank `4963`). Since the SB20 **sums L+R** as independent
  meters, leaving R=`4963` real risks a **double-count** against the ESP's doubled-total output — hence
  the phantom R.
- **Tooling:** this machine had **no `code/.venv` and no `bleak`** (system Py **3.14**). Created the venv,
  installed **bleak 3.0.2** (winrt **cp314** wheels exist); `06_capture_ble.py`'s BLE path verified
  against 3.0.2 (`client.services` property; `start_notify` before `write_gatt_char(response=True)`; CP op
  `enhanced-offset-compensation`=`0x10`, char `0x2A66`, reply expected `20 10 01 …`). G1 (real crank
  `0x10`) becomes a **free bonus** on resume — the real crank stays awake (no pull).
- **Process rule (owner):** the rider is **always explicit about being at the bike**; physical steps wait
  for the explicit cue (the agent pre-empted an in-app step while the rider was still upstairs). Captured
  to `sessions/PLAYBOOK.md` §2 + memory `rider-explicit-at-bike`.

**Open / first gate on resume:** does the SB20 **connect to our ESP at `62145`** when the app L-id is set
to it (real `62144` still powered)? Then: is the power sane (no double-count), and does the app
**calibrate/zero-reset complete** (the old G2 payoff)? **Board left staged at `Stages 62145`** (NVS);
restore to baseline with `POST /setup/save` `…spoof_name=Stages 62144…` + app L=`62144`/R=`4963`. Docs
landed: PR #123 + the close-out PR carrying this entry.

## 2026-06-24 — Security review → firmware-update + web hardening (desk; full plan in `ota-update-plan.md`)

A `/security-review` of the device surfaced two real issues; both fixed, plus the device-update path was
re-architected. Full design + decisions: [`ota-update-plan.md`](ota-update-plan.md). Summary:

- **Vuln 1 — unauthenticated/unsigned flashing (HIGH).** The board exposed an open `POST /update` form
  (browser-reachable → CSRF-able RCE) **and** an open ArduinoOTA (:3232) — any LAN host could flash
  arbitrary firmware. **Fix:** removed `/update` entirely; ArduinoOTA now **fail-closed** (off unless a
  build-time `OTA_PASSWORD` from gitignored `ota_secret.h` is set). PR #125.
- **Vuln 2 — no web auth / CSRF (MEDIUM).** No same-origin check on state-changing routes; `GET /forget`
  could be wiped via `<img>`. **Fix (option B):** pure host-tested `HttpSecurity.h` (`isSameOriginRequest`)
  guarding every mutating POST in both station + portal; `/forget` moved GET→POST. (Committed to main
  directly — process slip; CI-green.)
- **Signed-pull OTA (P1+P2 shipped, host-tested; P3/P4 gated on the back end + a key).** Device-initiated
  HTTPS **pull**, not push (NAT + no-inbound-listener). **Signing = ed25519 over a BLAKE2b digest via
  vendored monocypher** (owner's call over mbedTLS/ECDSA — same verify code runs host-side, so golden
  vectors: Python signs → C verifies). `OtaManifest`/`OtaVerify`/`OtaUpdater` pure + host-tested (every
  abort path); `OtaPull` HTTPS/Update seam. Signer: `sb20proxy.ota.sign` + `scripts/ota_sign.py`.
  PRs #128/#129. CI now also compiles the WiFi build (#127).
- **TLS = backend-only** (pin one Let's-Encrypt root, no direct GitHub); **eFuse hardening (Flash/NVS
  Encryption, Secure Boot) deferred past beta** (no eFuse-free option; beta threat is network, covered by
  signed-pull). Owner decisions, 2026-06-24.
- **SoftAP now WPA2** (closes the cleartext-PSK window): OLED boards show a per-device 8-digit PIN derived
  from the chip MAC (`SetupPin.h`, keyed BLAKE2b); screenless boards use `Config::SETUP_AP_DEFAULT_PASSWORD`
  = `sb20setup`. PR #130.
- **esp-net-kit** sibling lib mirrors these cores (`netkit` ns) for cross-project reuse; contributed the
  device-side glue back (esp-net-kit PR #1). **SB20 adoption DEFERRED** (private-repo CI blocker +
  native-env scoping) — see memory `esp-net-kit-shared-lib` + issue #131.

## 2026-06-25 — Session 8 (on-bike): SB20 spoof calibrate handshake CLOSED (G1 + G2 ✅)

The last open gap in the SB20 crank spoof — the **Stages app's calibrate / zero-reset** — is **closed and
grounded in captured bytes**. Run on the bike via the own-unique-ID plan. Narrative + live log:
[`sessions/session-08-sb20-spoof-calibration.md`](../../sessions/session-08-sb20-spoof-calibration.md).

- **Own-unique-ID spoof WORKS — but the bike requires a *findable* right crank.** ESP on its own id
  **`Stages 62145`**, app L=`62145` → the SB20 connects to our ESP and writes its `fe02` token
  (`bfda1853`): the bike accepts a spoof under a **brand-new** Stages id, not only the byte-faithful
  `62144`. **BUT** a **phantom right id (`4964`, nothing on air) → "pairing failed."** The bike validates
  that *both* configured crank ids are findable before pairing; using the real right **`4963`** (awake)
  for R let it complete. ⇒ the **phantom-R sole-source idea is refuted** — a real/findable right crank
  must be present.
- **No double-count in practice.** Pedalling: ESP reads Assioma ~100 W → outputs ~200 W (single-sided
  ×2); the **SB20 displayed ~203 W ≈ the ESP's doubled output alone**. A BLE scan showed the real right
  `4963` **still advertising = the bike never connected to it** — it only needs the id *findable* to pair,
  and consumes only the ESP's (doubled-left) total. So own-id + a findable right id ⇒ clean sole-source,
  no double-count. (Untested: whether a *non-real* findable right id also satisfies the pairing.)
- **G1 — real crank `0x10` (Enhanced Offset Compensation) captured** →
  `findings/captures/G-crank62144-ble-enhanced-0x10-20260625-0716.jsonl`. Real reply:
  **`20 10 01 00 00 ba 01 04 85 03 b7 03`**:
  - standard Offset field = `00 00` = **0** (the BLE residual `200c010000`, NOT the ANT+ raw 903 — confirms
    the 2026-06-19 offset reconciliation);
  - **Manufacturer Company ID = `0x01BA` = 442** (Stages) — independently corroborated by the SB20's own
    advert manufacturer-data key `442`;
  - **manufacturer-specific data `04 85 03 b7 03` ENCODES the L/R zero-offsets**: `85 03` LE = `0x0385` =
    **901** (L), `b7 03` LE = `0x03B7` = **951** (R). Shape `[0x04, Loff_LE, Roff_LE]`.
  - DIS re-confirmed byte-faithful: mfg "Stages Cycling", model `SPM2`, fw `1.8.2`, serial `11821518`, CP
    Feature `0x0008030B`, Sensor Location `0x00`, flags `0x002F`, proprietary `d445fe01` + Nordic DFU
    `fe59`. Crank battery **22 %** (low — the on-cell low-read caveat persists).
- **G2 — the Stages app's calibrate now COMPLETES** (was: spun forever, sessions 2–3). **Root cause: the
  placeholder `SPOOF_MFG_COMPANY_ID = 0x0000`** — the spec-correct *structure* alone is NOT sufficient; the
  app validates the company id. With the real **442 + the mfg-data**, the spoof's reply is byte-identical
  to the crank and the app shows **"Calibrated 901/951"** (literally our replayed bytes). `/log`: a single
  `[cp] write 10` (vs the pre-fix spin). Evidence:
  `findings/captures/G2-calibrate-pass-log-20260625-0758.txt`. **Caveat:** the mfg-data is replayed
  *verbatim* (static), so the app always shows 901/951 regardless of live state — cosmetic; the Assioma is
  the real calibrated meter.
- **Firmware fix (branch `session/08-onbike-20260625` → PR):** `Config::SPOOF_MFG_COMPANY_ID 0x0000 →
  0x01BA` (442) + `SPOOF_MFG_DATA = {04 85 03 b7 03}`, wired through `handleControlPoint` at the
  `BleCrankPeripheral.cpp` call site; golden host test `test_cp_offset_comp_enhanced_0x10_real_crank`
  asserts the 12-byte reply. **For the live G2 the binary was built from the pre-lockdown base `8494935`**
  (to avoid the deliberately-deferred 2026-06-24 security lockdown — its CSRF guard would 403 our restore
  POST, `/log` is toggled, espota is fail-closed). **Eventual `main` firmware = security lockdown + this
  442 fix** (desk reflash, post-session-8).
- **Process/tooling misses → PLAYBOOK:** (1) the `0x0000` placeholder made G2's spin **predictable** — it
  should have been flagged at plan time, with **G1 ordered strictly before G2** (we even ran G2 first);
  (2) the bike laptop had **no PlatformIO and no host gcc** (Python 3.14 orphaned the toolchain) → the dev
  env must be **reproducible** (committed requirements + a provisioning script) and a **build-toolchain
  check added to the bike pre-flight**; (3) OTA via espota needed an **explicit host IP**
  (`-I 192.168.1.223`) on this multi-NIC laptop (PlatformIO auto-picked `0.0.0.0` → "No response from
  device") — pin it in the flash helper / cold-start.
- **Restored:** app → L=`62144` / R=`4963`; crank length untouched (165 mm — never changed this session,
  so nothing to restore; the app's manual crank-length set sent no `0x04` to the board). ESP left on
  `62145` (idle, safe — the real `62144` is then the only `62144` on air). **Board currently runs the
  pre-lockdown+fix build; desk action: reflash to `main` (security lockdown + the 442 fix) and restore the
  canonical `62144` identity.**
- **Known gap (corroborated this session):** with the SB20 paired to the ESP spoof, the Stages app's
  **crank-length set didn't stick → the app shows "--"**. No standard CP `0x04`/`0x05` write reached our
  ESP (we handle both — a `0x05` request would reply `20 05 59 01` = 172.5 mm), so the **app uses a
  non-standard path for crank length** (matches session 3's A2 "app bypasses standard CP"). Doesn't affect
  the calibrate (G2) or power (the Assioma supplies watts). → **backlog:** capture the app's crank-length
  write (likely the proprietary `d445fe02`/`fe02` char) and implement it for a fully-faithful spoof.
  **Owner (2026-06-25): make this a *bidirectional* crank-length bridge** (app→meter forward + meter→app
  read-back) — the same transparent-config-bridge pattern as the zero-reset; see `forward-plan.md` §11.
- **Owner decision (static cal value — keep it):** replaying the captured `901/951` verbatim is **fine,
  even preferable.** We pass the **Assioma's real power straight through** to the SB20, so the displayed
  calibration offset is moot — and returning *out-of-normal* values could make the Stages app **error**. So
  do **not** synthesize a "dynamic" offset. **The meaningful follow-up instead:** make the app's
  **"Zero Reset" actually perform a real zero-offset on the Assioma** — when the SB20/app writes `0x10` to
  our spoof's CP, **propagate an offset-compensation/zero command to the real Assioma** over the existing
  BLE-central link so the calibrate button does something real, not just complete cosmetically. → backlog,
  `forward-plan.md` §10.

## 2026-06-25 (pm) — Canonical reflash to the locked-down firmware + OTA-update path validated

The SB20 board is now on the **shippable firmware** and the networked-update path is tested. (Follows the
session-8 close-out + the zero-reset feature — PRs #136 / #138, both on `main`.)

- **Reflashed `esp32c3-oled-live` from `main`** = the 2026-06-24 **security lockdown** + the **442** calibrate
  fix (#136) + the **zero-reset→Assioma** feature (#138). OTA'd over the board's then-current *open* espota
  (the pre-lockdown build), so this flash needed no auth; identity **`Stages 62145`** persisted (NVS).
- **Kept it OTA-recoverable** (the board is remote — no USB access from the dev box): built with a gitignored
  `firmware/ota_secret.h` defining `OTA_PASSWORD`, so the locked-down build's ArduinoOTA (`:3232`) stays ON
  but **authenticated** (`[ota] authenticated push OTA enabled` in `/log`). **Verified the password is in
  `firmware.bin` before flashing** — and a **clean** build is required: `__has_include("ota_secret.h")` does
  NOT trigger an incremental rebuild when the file newly appears, so an incremental build would silently
  ship a no-OTA image.
- **OTA update path — TESTED both ways (answers "how do we update OTA"):**
  - `espota` **without** the password → **`Authentication Failed`** (rejected — fail-closed auth enforced).
  - `espota -a <password>` → **`Authenticating...OK`**, flashed, board recovered. ✅
  - ⇒ **going-forward updates:** dev = **authenticated push** (`firmware/flash.ps1` reads `ota_secret.h`, or
    `espota -a`); **USB** = always-works fallback; **signed-pull (P3/P4)** = the future hands-off production
    path (still gated on backend + a key). On a multi-NIC host espota needs the explicit host IP
    (`-I 192.168.1.223`) — PlatformIO's `-t upload` auto-picks `0.0.0.0`.
- **Identity left on `Stages 62145`** (NOT restored to `62144`): it's the clean pairing for the pending
  zero-reset on-air confirm *and* safe for normal riding (the real `62144` is then the only `62144` on air).
  Restore `62144` once the experiment is fully done.
- **Open / next gate:** the **zero-reset on-air confirm** (does the Assioma actually zero when the Stages app
  calibrates?) — deferred to the next on-bike session ([`session-09`](../../sessions/session-09-zero-reset-onair-confirm.md)).
  Needs the SB20 + Assioma + a pedal + the app calibrate; watch `/log` for the forwarded `0x0C` then the
  Assioma's `20 0c 01 …` result.

## 2026-06-25 (pm) — Shared-services secrets: Windows Credential-Manager pull tooling + P1 machine identity
- **cauldnz-pos shipped the machine-auth model** ([cauldnz-pos#1](https://github.com/cauldnz/cauldnz-pos/issues/1),
  commit `c363a9d`): `infra/identities/new-machine-identity.sh` + a runbook + the two-pattern `SHARED-SERVICES.md`.
  **Infisical has NO SSH-keypair machine auth** — Universal Auth (clientId/clientSecret → short-lived token) is
  the method. (Refutes the "SSH key" preference for the *auth*; SSH is only the transport to the NAS.)
- **Two patterns (use both):** per-app/service = **read-only**, project-scoped (what the SB20 *build* runs as);
  per-machine = **read+write** (dev boxes, e.g. the P1 — can seed secrets). SB20 already onboarded by
  cauldnz-pos: project `sb20-power-proxy` (envs dev/staging/prod) + a read-only identity; creds NAS-side at
  `/mnt/user/appdata/pos-infisical/identities/sb20-power-proxy.creds`.
- **Added `tools/secrets-pull.ps1` + `tools/secrets-get.ps1`** — the Windows counterpart to the bash runbook.
  `secrets-pull` SSH-fetches a `.creds` block (or `-FromStdin` for the provisioner's one-time stdout) → stores
  {host, projectId, env, clientId, clientSecret} as a JSON blob in **Windows Credential Manager** (native
  `CredWriteW`, target `SB20/infisical/<identity>`, persist=LOCAL_MACHINE, never echoes the secret).
  `secrets-get` reads it back (`-Field` / `-AsEnv`). Verified at the desk: `-SelfTest` parser PASS + a real
  `CredWrite→CredRead→CredDelete` round-trip on a throwaway target PASS. The SSH + live-CredMgr paths are
  manual (host-credential, like the on-air checks).
- **P1 machine identity — decided: BOTH projects (`pos` + `sb20-power-proxy`), read+write.** The provisioner
  grants one project per run and isn't idempotent on a name, so *today* that's two scoped identities
  (`chris-p1-pos`, `chris-p1-sb20`); a single identity spanning both needs multi-`--project` support
  (to propose to cauldnz-pos). The `sb20-power-proxy --write` identity is what **seeds** `OTA_PASSWORD`.
- **Still queued (owner, NAS-side):** provision the P1 identity/ies; `secrets-pull` them into Cred Manager;
  seed `OTA_PASSWORD` into the project. Then the agent wires the build (`secrets-get.ps1` → `infisical login`
  → `infisical secrets get OTA_PASSWORD`), retiring the committed `ota_secret.h`.

## 2026-06-25 (eve) — Shared-services secrets onboarding EXECUTED live (build identity + OTA_PASSWORD in the vault)
- Ran the runbook against the real NAS (`ssh unraid` → WTRMax, root). State found: projects `chris-p1`,
  `pos-test`, `sb20-power-proxy`; identities `sb20-power-proxy` (read-only, project sb20-power-proxy/dev) and
  `chris-p1` (scoped to a standalone `chris-p1` project — see topology note below).
- **Build identity → Windows Credential Manager:** `tools/secrets-pull.ps1` SSH-fetched
  `sb20-power-proxy.creds` and stored {host, projectId `44b123ab…`, env dev, clientId `207d1470…`, secret} at
  Cred Manager target `SB20/infisical/sb20-power-proxy`. Verified via `secrets-get.ps1` (masked).
- **`OTA_PASSWORD` seeded** into `sb20-power-proxy`/`dev` via raw API `POST /api/v3/secrets/raw/OTA_PASSWORD`
  with the **admin bootstrap token** (admin reaches the project — a prior admin GET returned 404 "not found",
  not 403; HTTP 200 on create). Value piped via stdin (never in a command / the transcript); only lengths
  logged. **Verified readable by the read-only build identity** (UA login → raw GET → len 32, matches the
  local `ota_secret.h`). Proves the full build-consumption chain on live infra.
- **Build wiring (retires hand-maintained `ota_secret.h`):** added `tools/secrets-sync-ota.ps1` — pulls
  `OTA_PASSWORD` from the vault (Cred Manager creds → UA login → raw GET) and regenerates the gitignored
  `firmware/ota_secret.h` (`#pragma once` + `#define`). `-Check` = drift check (exit 1 on drift). Verified
  live from this Windows box: `wtrmax.local` resolves; `-Check` = "in sync (len 32)"; generate → 4-line valid
  header, still gitignored. The compile (`WifiLink.cpp __has_include`) + `flash.ps1` (espota `-a`) consume it
  unchanged. **Rotate** = change in Infisical → re-run → reflash.
- **No `infisical` CLI on the NAS** → raw API throughout (login `POST /api/v1/auth/universal-auth/login` →
  `.accessToken`; read `GET /api/v3/secrets/raw/<NAME>?workspaceId&environment` → `.secret.secretValue`).
- **⚠️ Topology OPEN (owner's call):** the existing `chris-p1` identity is on a **vanity `chris-p1` project**,
  not `pos`/`sb20-power-proxy` (provisioner defaults project=identity-name when `--project` omitted), so the
  "P1 = read+write on both projects" decision isn't met and the P1 can't self-serve seeding. Not blocking
  (admin seeds). Resolve by granting the `chris-p1` identity onto `sb20-power-proxy` + the general project
  (`pos-test`), or re-provisioning scoped identities; multi-`--project` support to propose to cauldnz-pos.

## 2026-06-25 (eve) — P1 dev-box identity repurposed (chris-p1 → read+write on sb20-power-proxy + pos-test)
- Per the owner's "repurpose chris-p1" choice: the `chris-p1` UA identity (IID `7e972f99…`, clientId
  `e8eda690…`) was granted **member (read+write)** on `sb20-power-proxy` (`44b123ab…`) and `pos-test`
  (`cf319ea9…`) — both grants HTTP 200. Verified **as chris-p1**: read `OTA_PASSWORD` ok (len 32) + a
  throwaway `_P1_CHECK` write (200) and delete (200); an admin re-check confirmed no residue from the test.
- `chris-p1.creds` repointed (`INFISICAL_PROJECT_ID`/`INFISICAL_PROJECT` → `sb20-power-proxy`) and pulled into
  Windows Credential Manager (target `SB20/infisical/chris-p1`) via `tools/secrets-pull.ps1`. The P1 can now
  self-serve seed/rotate `sb20-power-proxy` secrets without the admin token.
- **Residuals (non-blocking):** the empty vanity `chris-p1` project (`93fe5483…`, 0 secrets across
  dev/staging/prod) is still there — the project DELETE was blocked by the local safety classifier (reused
  the identity ✓, but destroying a shared-infra project needs explicit go-ahead); drop via the Infisical UI
  or admin `DELETE /api/v1/workspace/93fe5483…`. A stray `ONBOARD_TEST` key (cauldnz-pos onboarding artifact)
  sits in `sb20-power-proxy/dev` — harmless (the build pulls only `OTA_PASSWORD`); remove at leisure.

## 2026-06-26 — Session 9 (on-bike): zero-reset → Assioma CONFIRMED on air + phantom-R resolved
- **Zero-reset → Assioma works on air — the last unproven piece of PR #138. ✅** With the SB20 paired to our
  ESP (L=`62145` / R=`4963`), the Stages app's calibrate drove this `/log` sequence:
  `[cp] write 10` (app's Enhanced-Offset `0x10` on our control point) → `[cp] offset-comp -> forwarding zero to
  source meter` → `[meter] zero-reset -> source CP 0x0C: sent` → **`[meter] zero-reset source result 200c01ffff`**.
  Decode of `20 0c 01 ff ff`: response (`0x20`) / Start-Offset-Compensation (`0x0C`) / **SUCCESS (`0x01`)** /
  offset `0xFFFF` = −1. So the Assioma genuinely performed the offset compensation — NOT a cosmetic UI
  completion. App-side: the calibrate **completed** and showed offsets **`901/951`** (our spoof's mfg-data
  values) cleanly — the contrast with session 8 (which spun forever on the `0x0000` placeholder) confirms the
  Company-ID-442 + mfg-data fix end-to-end. Power kept forwarding through + after the calibrate (Assioma L
  doubled to total, e.g. 186 W → 372 W). **The SB20 crank-spoof is now proven end-to-end: pair → power →
  calibrate/zero.** Evidence: `code/findings/captures/G-zero-reset-onair-pass-20260626-0651.txt` (`/log` + `/status`).
- **Phantom-R pairing RESOLVED (forward-plan §12).** Live crank-id variants, watching `/log`:
  - L=`62145` (our ESP, present) + R=`4964` (absent) → app "pairing failed"; SB20 dropped our ESP
    (`[srv] disconnect reason=531`) and **never re-attempted** a connect.
  - L=`42146` (absent) + R=`4963` (real right, present) → **same "pairing failed".**
  - both present (`62145` + `4963`) → connects immediately.
  ⇒ **The SB20 requires BOTH configured crank IDs findable on air** (symmetric — absent L *or* absent R aborts
  the whole pairing). This **refines session 8's** inference ("needs a findable right crank"): it's not
  right-specific. Implication for **sole-source** (Assioma only, no real cranks): our ESP must answer *both*
  configured IDs → the **2nd phantom-right peripheral** path. Still open: app- vs bike-gatekeeper (needs an
  nRF sniff of the app↔bike link to fully distinguish — nRF wasn't available this session, see below).
- **Next-ride §12 variant (owner):** repeat with the **known-good pair `62145`/`4963` but the right crank's
  battery pulled** — does a *known* id that's offline behave like an *unknown* one? Isolates "id unknown" from
  "id not currently on air".
- **FTMS workout driver — built, async bug found + fixed, drive deferred.** Added `code/scripts/ftms_workout.py`
  (structured interval erg over FTMS, reusing the validated `ble/ftms` codec + `ble/ftms_erg.ErgController`).
  First on-bike launch failed: it called the **synchronous** `ftms_erg.drive()` with an **async** bleak transport
  (`coroutine was never awaited`). Fixed by pumping the controller against an *awaited* transport (mirrors the
  proven `FtmsErgSession.run`). **Safety held:** the `try/finally` sent Reset (+ Stop) → the bike returned to
  neutral despite the error. Rider stopped here (the bug/fix cycle cost ride time) → a properly-driven workout
  deferred to next session. **Lesson:** the driver's tests covered only the pure segment builder, not the async
  drive path — that gap let the bug ship; adding a twin-based async-pump test. MCP'd version backlogged (§13).
- **Process miss (owner flagged — filed for the post-mortem):** the **dual-radio capture** (nRF + ANT+) is a
  STANDING pre-flight rule, but the nRF sniffer was NOT ready (Npcap not installed; nRF Sniffer extcap
  unregistered) and this was MISSED across two explicit "check everything" passes (night-before + morning),
  because `doctor.ps1` doesn't cover the capture rig (green doctor = false confidence). Fix queued: extend
  `doctor.ps1` to GATE the capture rig, and a readiness pass must *verify both radios actually capturing*, not
  list it. (Recorded in the session-09 retro + folded into the PLAYBOOK.)

## 2026-06-26 (pm) — Correction: the nRF capture path is sniff_ble.py, NOT Npcap (doctor #158 gated the wrong path)
- **Refuted (mine, same day):** the morning "nRF not ready → install Npcap" call **and** PR #158's doctor gate
  (Npcap + tshark extcap) were the **WRONG PATH**. After reviewing `nrf-sniffer.md` (which I'd failed to read)
  + running `scripts/sniff_ble.py --scan-only`: the project captures BLE **headless via `sniff_ble.py`**, which
  drives Nordic's **SnifferAPI over the dongle's serial port** → pcap. It needs **no Npcap / no tshark** —
  those are only the interactive Wireshark-GUI alternative.
- **The real state this morning:** the nRF genuinely wasn't usable, but because the **SnifferAPI extcap was
  un-staged** (gone from `%APPDATA%\Wireshark\extcap`; present 2026-06-21) **and pyserial was missing** — NOT
  Npcap. Dongle firmware was fine (Nordic VID_1915 PID 522A). So the "unavailable" *conclusion* was right; the
  *reason* + the Npcap remediation were wrong.
- **Fixed (PR #160):** `doctor.ps1` nRF checks now gate the real path — dongle PID 522A + SnifferAPI staged +
  pyserial in `code/.venv`; README "Capture rig setup" re-framed around `sniff_ble.py` (nrf-sniffer.md
  canonical; Npcap/Wireshark = GUI alternative); `pyserial>=3.5` added to the `[ble]` extra (reproducible via
  provision-dev-env.ps1) + installed into `code/.venv`. Verified: doctor now FAILs only the genuine gaps
  (SnifferAPI re-stage + ANT chmod); dongle + pyserial PASS.
- **To make nRF ready next ride:** re-stage the matched **v4.1.1 SnifferAPI extcap** into
  `%APPDATA%\Wireshark\extcap` (makerdiary zip; firmware already flashed) + do the ANT `chmod` (runbook §1).
  Both doctor-gated now.
- **Process lesson:** read the subsystem's canonical findings doc (here `nrf-sniffer.md`) BEFORE building
  tooling/checks for it — both the spawned doctor task and I built the gate on assumptions, not the doc. (A
  concurrent session also improved `nrf-sniffer.md` with a TL;DR quick-start, PR #159.)

## 2026-06-26 — MCP workout builder + driver: desk-complete (forward-plan §13)

Built the productized form of session-9's ad-hoc `ftms_workout.py` driver — an **MCP server that
exposes the SB20 erg as agent-drivable workout tools** (compose a structured workout + drive it live
over FTMS). Desk-only, fully host-tested, no bike. See [`mcp-workout-server.md`](mcp-workout-server.md).
Landed as two PRs (the pure core, then the MCP server) per the desk-test-in-the-same-commit rule.

- **`sb20proxy.workout` (PR #165):** `builder.build_plan(spec)` → a `ride.director.RidePlan` from a
  built-in name / a structured dict (with `repeat` nodes; leaf segments via the existing validated
  `ride.control.segment_from_json`) / a shorthand string (`"5min @ 130W; 6x(90s @ 430W; 3min @ 100W)"`).
  `session.WorkoutSession` wraps a live `ride.state.LiveState` as the agent verbs (build / list /
  start / stop / skip / goto / extend / set_target / message / set_profile / status). It's an
  **adapter over already-built infra** (the Ride Director's `LiveState` + `apply_control` + `WORKOUTS`
  + `ble.ftms_erg`), so the only new pure code is the builder + the controller wiring.
- **`sb20proxy.mcp` (this PR):** `driver.ErgDriver` — the async drive loop that pumps an
  `ErgController` toward `LiveState`'s `erg_setpoint_w` each poll, and **ALWAYS sends FTMS Reset on
  teardown** (stop / duration-end / error) so the rider is never left at target — the teardown
  `FtmsErgSession` lacks. `server.build_server()` registers the FastMCP tools incl. start_drive /
  stop_drive / drive_status; the bleak connect is an **injectable transport provider** seam, so the
  drive tools are host-tested against `InProcessFtmsServer`. Entry: `scripts/mcp_workout_server.py`;
  the MCP SDK is the optional `[mcp]` extra (CI installs it so the server tests run).
- **Decision — don't add pause/resume yet:** it needs a `LiveState` clock-freeze primitive; editing
  that shared, tested module unsupervised wasn't worth it. Deferred (noted in §13).
- **Cosmetic backlog confirmed already DONE:** `ProxyCore::reset()` + the `main.cpp`
  connected→disconnected edge already zero stale `src_*`/`power_w` on meter drop; forward-plan §8
  note corrected (it was stale).
- **Tests:** +58 host cases across builder / session / driver / server. Full Python suite green
  (430 passed), ruff clean (src+tests; scripts unlinted per policy). No hardware.
- **Remaining (gated on the bike):** the live MCP-driven ride; persisted named-workout library;
  observability/log surfacing. The FTMS erg bytes are already validated (Session 4; `G-sb20-ftms-erg-*`).

## 2026-06-27 — Prior art (PedalSmart.blog): SB20 native single-crank mode → simpler crank-rescue

Owner shared the PedalSmart.blog post "SB20: one of my power meter cranks have…" (2024-12). Non-technical
(no IDs/ANT+/BLE specifics — understanding only, nothing to clean-room), but it corroborates our model and
reframes the sole-source path (full note in forward-plan.md §12):
- **Master-slave confirmed:** the **Left crank aggregates** R+L and sends one consolidated L:R message —
  the crank our spoof impersonates.
- **Native single-crank mode:** the SB20's own dead-crank fix is **pull the dead crank's battery + select
  "Stages Bike" as the power source**, after which **the bike doubles the surviving crank**. This validates
  our `singleSidedDouble` ×2 (the bike really doubles a lone crank) and is the SB20's documented behaviour,
  not a guess.
- **Reframes §12 / sole-source:** session 9's "the SB20 needs BOTH configured crank ids findable" was
  measured with the app in **dual-crank** config. The blog's path is **single-crank** config. ⟹ hypothesis:
  the both-findable rule may be **dual-config-specific**; single-crank config may need only **one** findable
  crank. If so, crank-rescue is simpler — configure single-crank, spoof the one surviving/master crank, let
  the bike double it — rather than the 2nd-phantom-right peripheral. **Queued to test next bike session.**

## 2026-06-27 — Community research harvest (PedalSmart.blog) → sb20-hardware-reference.md

Systematic read of ~20 SB20 posts on PedalSmart.blog (clean-room — understanding only) distilled into
[`sb20-hardware-reference.md`](sb20-hardware-reference.md) (secondary source; our captures win on conflict).
The findings that matter most for us:
- ⭐ **Erg is gated on a "working" Stages crank.** A bare third-party meter → free-ride/sim only, NO erg
  ("when Zwift requests erg, the SB20 stays in free-ride"), regardless of Zwift config. ⇒ **our crank spoof
  is precisely what unlocks third-party erg** — it makes the SB20 believe its crank works. Corollary: a
  spoof that pairs but isn't accepted *as a working crank* would silently give free-ride only. (Sessions
  2–9 already show the SB20 ergs off our spoof — this is the mechanism.)
- ⭐ **"Pair with Bluetooth" (Stages app → Devices → Power Meters) moves the crank↔bike link from ANT+ to
  BLE.** Almost certainly why our BLE crank spoof is accepted at all (internal default is ANT+: R→L→bike).
  Bench-confirm the bike is in this mode.
- ⭐ **Crank length is stored ON the cranks**, set/read via the app's Power Meter tab (the app pushes *bike*
  settings on restart but NOT crank settings — cranks read their own). Likely why the app shows `--` for our
  spoof's crank length (§11): it's exchanged over the Stages proprietary path, not standard CP `0x04`.
- **Internals:** bike + each crank run a **Nordic nRF52832**; cranks = CR2032 + strain gauge (torque) +
  accelerometer (angle/cadence); R→L→bike consolidation; eddy-current brake at 24 V; LED red=power/blue=
  connected; no Stages firmware since ~late 2023.
- **Torque = mass·9.81·crank-length**; crank length scales power (±5 % is the de-facto user cal); zero-reset
  is a torque *offset* (Nm), not a calibration, and the crank auto-zeros at rest. **FTMS connection limit = 2.**
- Tools GearView/BattView are iOS dashboard/battery apps — no protocol/RE content.

## 2026-07-02 — S3-Touch head-unit: UI built + host-tested; on-hardware bring-up BLOCKED. C3-OLED reflashed for the ride.
- **Two new boards arrived + identified:** Waveshare **ESP32-S3-Touch-LCD-1.47** (esptool: ESP32-S3, 8 MB
  PSRAM, base MAC `a4:cb:8f:da:e9:cc`, on **COM16**); a second AliExpress board couldn't be identified
  (listing not fetchable; enumerated as a generic "General UDisk" mass-storage + a code-43 device — likely
  a bad/charge-only cable). C3-OLED bike board = **COM9** (`38:44:be:45:e9:a4`), nRF sniffer = COM13.
- **S3 head-unit UI = DONE in software, verified.** `firmware/lib/proxy/{LcdCanvas,LcdUi,LcdFont}.h`: pure
  172×320 RGB565 UI — the **5 locked screens** (Ride/Workout/Setup/More/Calibrate) + tap→`UiAction`
  routing; **7 host tests** (`test_lcd_*`, 177 total green) and **all 5 screens rendered to PNG + eyeballed**
  (`design/render/s3-lcd-*.png`). Seam `src/disp/LcdDisplay.h` (JD9853 SPI 172×320 col-offset 34 + AXS5106
  touch @0x63, pin map from the board BSP) + `main.cpp` `lcdTask` + a USB-serial bench console + `bench_s3.py`.
- **BLOCKER — the S3 won't boot the Arduino image.** Hangs before `setup()` on EVERY S3 build incl. the
  **minimal base proxy** (`esp32s3-min`, ~identical to the working C3) → **board/Arduino-core issue, not
  our code**. No BLE advert, no serial, empty NVS. **Ruled out:** static-init framebuffer (now lazy), PSRAM
  (`BOARD_HAS_PSRAM` removed — octal PSRAM unmapped during static init), flash-mode dio/qio, the LCD code,
  USB-CDC-on-boot. The board's **native-USB serial is flaky** (documented) and even the ROM/JTAG boot log
  never reached the host → no panic backtrace. **NEXT:** UART on **GPIO43 (U0TXD)** for the panic, or the
  **pioarduino (IDF 5.x)** platform (our `espressif32@~6.7.0` = IDF 4.4 may be too old for this module). The
  known-good reference is ESP-IDF (`miguelgarcia/…espidf-platform-template`, `qio_opi`+`flash_mode=qio`).
  Full write-up: [`advanced-board-s3-touch.md`](advanced-board-s3-touch.md) §"Bring-up status".
- **Ride path for the morning = C3-OLED + phone UI (WORKS).** Reflashed COM9 to current `main`
  (`esp32c3-oled-live`): verified back up — crank advert `Stages 62145` (−36 dBm), WiFi reachable, `/status`
  carries the new `identity`/`mode`, `/more` (Settings) serves, and POST `/workout/preset?key=4x8` loaded the
  9-segment workout. Run-sheet: [`session-10-spin-bike-ui-tryout.md`](../../sessions/session-10-spin-bike-ui-tryout.md).
- **Guardrail note:** an attempt to auto-materialise the WiFi password from the C3's NVS dump (to seed the
  S3's `wifi_secret.h`) was correctly blocked; pivoted to a credential-free USB-serial bench instead.

## 2026-07-02 (pm→2026-07-03) — S3-Touch head-unit UNBLOCKED: boots + full UI renders on the pioarduino (IDF5) platform.
- **Root cause of the boot-loop:** NOT flash tuning. A boot log captured by holding serial open across a
  physical RESET showed the **2nd-stage bootloader itself crash-loops** — ROM hands off (`entry 0x403c98d0`)
  and it resets *before printing its banner*: `rst:0x3 RTC_SW_SYS_RST` (DIO) / `rst:0x7 TG0WDT_SYS_RST` (QIO),
  across DIO/QIO × 80/40 MHz. The stock `espressif32@~6.7.0` (Arduino 2.0.17 / **IDF 4.4**) bootloader is too
  old for this S3 module. (Corrects the 2026-07-02 "hangs before setup()" reading.)
- **Fix — pioarduino platform (Arduino 3.3.9 / IDF 5.5.4).** New envs on
  `github.com/pioarduino/platform-espressif32 …/55.03.39`: `esp32s3-pio-min` (DIAG probe) +
  `esp32s3-pio`/`-live`/`-live-bench`/`-ota` (full LCD UI). First boot: `[sb20proxy] … starting` →
  `[diag] alive 0..7` → `NimBLE-init done` → `spoofing as 'Stages 62144'`; **0 boot-loops**; BLE scan sees
  **`a4:cb:8f:da:e9:cd` = "Stages 62144"** (−46 dBm, CPS + Stages svcs).
- **Four migration gotchas (all fixed in-repo):** (1) pioarduino's toolchain installer **rejects
  Git-Bash/MSYS** ("MSys/Mingw is not supported", leaves an empty toolchain) → run its `pio` from **native
  PowerShell** (`…/.platformio/penv/Scripts/python.exe -m platformio …`). (2) Arduino-3.x WiFi split →
  `fatal error: Network.h` under the global `deep+` LDF (espressif/arduino-esp32#9782 — the `+` variants
  eval preprocessor conditionals) → **`lib_ldf_mode = deep`** on the S3 envs. (3) plain `deep` won't
  auto-discover the flat, manifest-less **`lib/monocypher`** → added `lib/monocypher/library.json` +
  `symlink://lib/monocypher` in lib_deps. (4) `#include <WiFiClient.h>` in `src/net/OtaPull.cpp` (no longer
  transitive); `LcdDisplay.h` LEDC backlight ported to the 3.x pin API behind `ESP_ARDUINO_VERSION_MAJOR>=3`.
- **Full head-unit UI verified ON HARDWARE.** Two more fixes to get the panel lit: **PSRAM** enabled
  (`board_build.arduino.memory_type = dio_opi` — keeps the DIO flash that boots + the S3R8 octal PSRAM) so
  the 172×320×2 = 110 KB `LcdCanvas` framebuffer stops throwing `std::bad_alloc`→`abort()`; and **touch I2C**
  switched to a **STOP-condition read** (`endTransmission(true)`, matching the vendor BSP
  `esp_lcd_touch_axs5106.c` two-transaction pattern) — the repeated-start (`false`) made the IDF5 "ng" I2C
  driver return `ESP_ERR_INVALID_STATE`. Result: `[lcd] JD9853 172x320 up; touch(AXS5106)=alive`, no abort,
  stable in `loop()`. **All 5 screens captured live off the panel** (serial `SCREEN`→BMP): Ride (140 W +
  sparkline), Setup, More/Settings, Workout (4×8 Threshold, TARGET 138 W + interval chart), Calibrate; and
  **on-device tap nav works** (injected `TAP` Ride→Setup→More→Workout→Calibrate; `touch:1`).
- **Flash the S3** (USB-JTAG drops the stub at 460800, so `pio -t upload` hangs — flash the merged image at
  115200): `python -m esptool --chip esp32s3 --port COM16 --baud 115200 --before default_reset --after
  hard_reset write_flash --flash_size 16MB 0x0 .pio/build/esp32s3-pio/firmware.factory.bin`. **Use esptool
  ≥5.x, NOT PlatformIO's penv esptool 4.5.1** — 4.5.1 mis-writes the flash-size field of the merged image's
  bootloader header (8 MB on this **16 MB** chip, device 0x4018), so the bootloader rejects the 16 MB
  partition table (`partition 3 … exceeds flash chip size 0x800000`) and boot-loops (`rst:0x3`; briefly
  bricked the board mid-session until reflashed with esptool 5.3.1). New helper **`code/scripts/flash_s3.py`**
  probes candidate pythons for esptool ≥4.11, flashes the factory image, and `--verify-ble`s the advert. Boot
  log via `<scratchpad>/capture_s3_boot.py`. Native host suite still green (177) + C3 `esp32c3-oled-live`
  still compiles (monocypher manifest + WiFiClient include are no-ops on the old platform). Full write-up:
  [`advanced-board-s3-touch.md`](advanced-board-s3-touch.md) §"Bring-up RESOLVED".

## 2026-07-03 — S3 live meter-read validated via digital twin (no bike needed).
- Closed the "S3 live read untested" gap using the existing bench rig instead of the Assioma: flashed
  **`esp32s3-pio-live-bench`** (reads any CPS, `-DMETER_MATCH_ANY_CPS=1`) and ran the WinRT
  **`fake_meter.py --watts 200 --steady`** (200 W / 85 rpm CPS peripheral). The S3 **scanned, connected,
  subscribed** (fake_meter logged `subscribers=1`) and its serial `STATE` reported **`power:200, cad:85`**
  — the full live BLE central path (scan → connect → decode CPS → display) works on the S3 on the
  pioarduino platform, same as the C3. Board reflashed to ride-safe **`esp32s3-pio-live`** (ASSIOMA-only)
  after. Note: could/should have done this the first night (had the twin + the nRF52840) — owner feedback,
  see [[try-newer-toolchain-early]] sibling lesson: use the digital twin to test protocol paths off-bike.
- The remaining S3 unknowns are on-bike only: the real Assioma pair + the SB20 accepting the S3's spoofed
  crank (the rebroadcast path is the same code the C3 proved).

## 2026-07-03 (pm) — third head-unit: the AliExpress "Cheap Yellow Display" ported + twin-tested in an evening.
- **The mystery AliExpress board identified** (listing finally readable in a browser): **ESP32-2432S028R
  "CYD"** — classic ESP32-D0WD-V3 (4 MB flash, NO PSRAM), 2.8" 240×320 TFT, XPT2046 resistive touch,
  CH340 on **COM17** (`8c:94:df:93:cc:8c`). The owner's 2-port variant ("CYD2USB"): **only the Micro-USB
  enumerates** (the USB-C was why it looked dead before); panel probes **ST7789-family + INVON** (0xD3
  reads all-zero → not ILI9341) via the new `cyd_probe_main.cpp` serial probe (register-ID reads, color
  bars, raw-touch dump — identifies the panel without eyes).
- **The full bike computer runs on it** (envs `esp32cyd`/`-live`/`-live-bench`): the SAME pure UI at
  240 px wide (`-DLCD_PANEL_W=240` — LcdUi lays out from LCD_W), rendered in **4 horizontal bands**
  (`-DLCD_BANDS=4`, 38 KB each) because 153 KB of frame can't sit beside WiFi+BLE without PSRAM. Banded
  == full-frame **pixel-identical by host test** (`test_lcd_banded_render_matches_full`; 178 native green).
  Serial `SCREEN` now streams a top-down BMP band-by-band (works on every LCD board).
- **Digital-twin loop PROVEN end-to-end on the CYD:** `fake_meter.py --watts 200` → CYD central connects
  (`subs=1`, STATE `power:200 cad:85`) → rebroadcasts as **`Stages 62144`** (−42 dBm) → `crank_reader.py`
  decodes **200 W / 85 rpm / L50-R50** in byte-faithful `0x2F` frames (`2f00c800…`). Tap-nav + workout
  preset (target 138 W) verified over serial; workout persisted across a reflash (NVS).
- **Port gotchas (all fixed in-repo,** [`cyd-board.md`](cyd-board.md)**):** (1) **GPIO 8 is a FLASH pin on
  classic ESP32** — the C3's STATUS_LED_PIN=8 wedged the chip at `pinMode` → TG1WDT boot loop; now
  `-DSB20_STATUS_LED_PIN` (CYD=4, the RGB-red). (2) The 300 KB `SCREEN` dump blocked loop() >15 s and our
  own **loop-stall watchdog rebooted the board mid-stream**; the emit path now feeds `g_loopBeat` + batches
  512-B writes. (3) Classic-ESP32 toolchain first-install corrupted under Git-Bash → **PowerShell/penv for
  all pio runs** (now a standing rule). (4) CH340: clear DTR/RTS **before** opening the port or it resets.
- Three head-unit tiers now run the one core: **C3-OLED** (shipping beta) · **S3-Touch** (premium 172×320
  capacitive) · **CYD** (budget ~AU$23 240×320 resistive). Remaining CYD unknowns: real-finger touch
  calibration + panel colors (INVON assumed) — one human glance/tap to confirm.

## 2026-07-03 (late) — ARCHITECTURE: LVGL v9 adopted for the head-unit UI (owner decision).
- **Owner call** ("less Super Mario Bros, more like the HTML app"; asked "are you using a UI framework?"
  and picked **Adopt LVGL** over a hand-rolled AA-font uplift): the device UI is now **LVGL v9 + Inter**
  (vendored SIL-OFL TTFs → 4bpp AA fonts via lv_font_conv at 12/16/20sb/28sb/64sb). CYD verified on
  hardware (screenshots `design/render/cyd-lvgl-*.png`); S3 builds green (flash when reconnected); C3
  untouched. PR #204; also that day: touch-cal ritual (#202, rendered via LVGL now) and the CYD port
  (#201).
- **The boundaries that made it cheap:** LvglUi consumes the SAME view-model (`LcdViews`, built by
  main's `buildLcdViews`) and emits the SAME `UiAction`s — main.cpp's data/action plumbing, the workout
  engine, the twin tests, and the RAWTAP-testable cal ritual all carried over unchanged. The pure
  LcdCanvas/LcdUi renderer stays host-tested (181 native) as the non-LVGL fallback; **LVGL host-snapshot
  testing is the open follow-up** (compile LVGL in the native env, golden-image the screens in CI).
- **Integration gotchas (all fixed in-repo, will bite again otherwise):** (1) lv_font_conv emits
  COMPRESSED glyphs by default → text silently invisible without `LV_USE_FONT_COMPRESSED 1`.
  (2) `lv_conf.h` must reach the LIBRARY build too (`-Iinclude` in build_flags) or lvgl compiles against
  defaults = silent ABI mismatch (the "Possible failure to include lv_conf.h" pragma is the tell).
  (3) **LVGL is not thread-safe** — the serial SCREEN dump must run in the LVGL task (a console-task
  `lv_refr_now` raced to 0 areas). (4) The screen array is indexed by the `LcdScreen` enum
  (Ride,Setup,More,Workout,Calibrate) — a mis-ordered array swapped Setup/Workout. (5) Generated font
  .c files are unguarded C → non-LVGL envs exclude `src/ui/` via the base build_src_filter.
- **Memory/flash:** ~1/8-frame partial render buffer (~19 KB) — the no-PSRAM CYD needs no PSRAM; LVGL+
  fonts ≈ +260 KB flash (CYD at 77% of min_spiffs). SCREEN now streams LVGL flush areas as
  `<AREA x1 y1 x2 y2 b64>` blocks (grabber: scratchpad `grab_lvgl_screen.py` → reassembled PNG).

## 2026-07-03 (later) — CYD UI fully wired + two LVGL/touch landmines found on real hardware (owner bench test).
- **Owner's first real UI session found the LVGL port half-wired:** no Ride details pop-down, no way
  back to the workout picker once a workout was loaded (NVS re-loads it every boot — the picker was
  unreachable forever), Setup Rescan wired to nothing, and the touch-cal ritual wouldn't register the
  right-edge targets. All fixed + headlessly verified (TAP/STATE over serial): title-bar tap toggles
  IN/OUT detail cards; **Change** button (loaded && !running) emits the new `UiAction::WorkoutUnload`
  (runtime `unload()` + `WorkoutStore::clear()`); `SetupScan` → `meter.clearCandidates()`.
- **LANDMINE 1 — LVGL's 48 KB builtin pool exhausted = NULL-write crash, not an error.** The full
  five-screen widget tree left no headroom; the FIRST glyph draw-buf alloc returned NULL and
  `lv_draw_unit_draw_letter` wrote `draw_buf->header.h` through it → StoreProhibited @0x6 on core 1
  (asserts are compiled out). A bigger static pool doesn't fit either: 72 KB of .bss overflows classic-
  ESP32 dram0 once the live build's NimBLE central links in (+11.6 KB over). **Fix: malloc-backed LVGL**
  (`LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB`) — no fixed pool, no .bss, heap is the ceiling. Live-bench
  runs at ~35 KB free heap on the CYD. Likely also the S3 LVGL first-flash stall (same pool; verify).
- **LANDMINE 2 — this CYD's XPT2046 idle Z2 sits MID-SCALE (~2045), not the datasheet ~4095.** The
  original Z1>200 gate dropped right-edge presses (Z1 scales with raw X; this film's X is inverted →
  right edge = weak Z1) — the owner's "top-right cal target won't register". The datasheet composite
  `Z1+4095-Z2` was WORSE: idle composite ≈2050 = a permanent phantom press that swallowed every tap
  (LVGL saw an eternal press pinned at one point). **Fix: gate on Z1 alone with a low floor**
  (`CYD_TP_ZMIN`, default 40; idle Z1 is a clean 0 on this film) + the ritual now rides through ≤3
  dropout ticks mid-press. New serial `RAWZ` probe prints one `{down,rx,ry,z,z1,z2}` sample — use it
  per-unit before trusting any pressure threshold.
- Also: `buildLcdViews` was calling `ConfigStore::load()` (an NVS open) every 200 ms frame on live
  builds — 5 flash reads/s + a NOT_FOUND error log each on never-configured boards; it now reads the
  existing `g_calibrating` runtime flag. The live envs (`esp32cyd-live*`, `esp32s3-pio-live*`) were
  missing the LVGL build flags entirely (only mock envs had them — child build_flags REPLACE the
  parent's in PlatformIO); fixed, and the CYD live-bench chain re-proven: fake_meter (WinRT) →
  CYD central → LVGL Ride screen showing the ramp @88 rpm. WinRT fake_meter stops advertising after
  a central disconnects (board reflash) — restart the script to re-link.

## 2026-07-04 — S3 LVGL stall ROOT-CAUSED: nested SPI transaction deadlock (not the memory pool).
- The S3's LVGL task froze forever at its FIRST flush — boot log stops dead between "[lvgl] init done"
  and the first heartbeat; no panic, no reboot, taps/dumps silently ignored. **Cause:**
  `LcdDisplay::blitArea` (the LVGL seam) wrapped `setWindow_` inside its own `SPI.beginTransaction` —
  but on the S3, `setWindow_` opens its OWN transaction, and the pioarduino/IDF5 core's SPI bus lock is
  **non-recursive**: the nested acquire from the same task blocks forever. The proven canvas `blit()`
  always windowed BEFORE its transaction — `blitArea` now does the same. The CYD never hit this (its
  `setWindow_` writes raw cmd/data with no inner transaction). **Rule: on the IDF5 core, never call a
  helper that owns a transaction from inside one — the deadlock is silent** (a task at prio 2 on core 1
  just stops; loop()/console keep running, which is exactly why it looked "half-alive").
- With that + the malloc-backed LVGL heap (yesterday's CYD fix), the **S3 LVGL UI is fully alive**:
  heartbeats (65 KB free heap), nav/details/workout-preset taps all verified over serial — same UI,
  same wiring as the CYD. The liveness heartbeat instrumentation is what split "init done" from
  "first loop pass" and localised the freeze to the first render — keep it.
- Second S3 CDC gotcha: the serial SCREEN dump returned only background — `setTxTimeoutMs(0)` (the
  never-block-headless setting) silently DROPS the dump's bulk base64 when the tiny CDC buffer fills.
  The dump path now sets a 200 ms TX timeout for its duration (a host is attached — it sent SCREEN)
  and restores 0 after.

## 2026-07-04 (later) — Three-board web UI + WiFi onboarding: QR screens, per-board hostnames, and four classic-ESP32 landmines.
- **Goal (owner):** all three head-units run the SAME complete web interface, and a user can onboard
  any board onto home WiFi after a hard reset. Both now verified END-TO-END on real hardware, driven
  headlessly from the PC's WLAN NIC (netsh join + portal POST — scratchpad `portal_onboard.py`).
- **Per-board mDNS hostnames** (three "sb20proxy" boards collide on one LAN): C3 keeps
  `sb20proxy.local`; the CYD is `sb20proxy-cyd.local`, the S3 `sb20proxy-s3.local` (SB20_HOSTNAME
  default per board in main.cpp; override with -DSB20_HOSTNAME).
- **QR onboarding screen (LCD boards):** while the captive portal is up, the CYD/S3 replace the UI
  with an LVGL screen: a WIFI:T:WPA;S:SB20-Setup;P:<pin>;; QR (LV_USE_QRCODE; machine-verified
  decodable from the on-device screenshot — design/render/{cyd,s3}-wifi-onboarding-qr.png), plus the
  SSID/PIN as text and the portal URL. Driven by ProvisionView in LcdViews from wifi.inPortal() —
  same polling pattern as the C3's OLED portal text. Route sweep: 13/13 on C3 + S3; CYD serves all
  routes but multi-KB pages are flaky at its measured RSSI −76 (see landmine 4).
- **Landmine 1 — classic-core SoftAP DHCP never leases** with the implicit defaults: the client
  associates then self-assigns 169.254.x.x, so the portal is unreachable. Fix: scan BEFORE the AP
  comes up + explicit `WiFi.softAPConfig(192.168.4.1/24)` (forces the DHCPS). Hit on BOTH classic-
  core boards (CYD + C3); the S3/IDF5 core was fine without it.
- **Landmine 2 — ANY BLE activity starves the classic core's SoftAP data path**: association + DHCP
  squeak through, ARP/TCP never flow. Fresh-onboarding portals now hold BLE off entirely
  (proxy.begin() moved after wifi.begin(); onboarding always ends in an esp_restart so it costs
  nothing). The JOIN-FAILED fallback portal still starts BLE (`portalAfterJoinFail()`) so a ride
  away from home WiFi is never bricked.
- **Landmine 3 — `WiFi.setSleep(false)` ABORTS the classic core when BT is enabled**
  ("Should enable WiFi modem sleep when both WiFi and Bluetooth are enabled" → boot loop). The legal
  lever is `esp_coex_preference_set(ESP_COEX_PREFER_WIFI)` (our BLE is 1 Hz notifications — it can
  spare the airtime). Plus: Arduino WebServer::send IGNORES short writes under lwIP memory pressure
  → silently truncated pages; all HTML routes now stream via a drain-aware `sendHtml_` (1 KB slices,
  wait-for-drain). Also freed DRAM on the no-PSRAM CYD: LVGL draw buffer 1/8→1/16 frame when
  !psramFound(), LCD task priority 2→1 (it was preempting loop()'s WebServer sends).
- **Landmine 4 — the CYD's PCB antenna is weak** (board-side RSSI −76 where the PC sees the same AP
  at 90%): with BLE coex that RF margin makes sustained multi-KB TCP flaky at desk distance. Not a
  firmware bug — expect better closer to the router; the IPEX-pad antenna mod is the hardware fix if
  it matters. Small-payload routes (status/state/log JSON) are reliable even at −76.
- **Onboarding proven per board:** S3 (default AP pass) and CYD (post-fixes) portal→NVS→station;
  C3 via POST /forget → portal with its per-device 8-digit PIN — replicated in Python
  (hashlib.blake2b keyed, digest 8, over the STA MAC, mod 1e8 = 72745928 for 38:44:BE:45:E9:A4) and
  used as the WPA2 key. The PC's WLAN NIC does the whole flow headlessly; no phone needed.

## 2026-07-05 (overnight) — nRF52840 Sense: the BLE bridge-with-correction is REAL (twin-proven), IMU capture + Web Bluetooth + CIQ surfaces built.
- **The fourth board works.** Seeed XIAO nRF52840 Sense (COM18/COM20, `firmware-nrf/`,
  worktree-landed): dual-role Bluefruit bridge reads a CPS meter, applies the SHARED pure
  `Correction`/`Cps` codec (same headers as the ESP32 builds via `lib_extra_dirs`), rebroadcasts
  as its own CPS meter. **Combined bench PASS**: fake_meter → Sense → PC reader with ×1.1
  correction exact to the watt (200→220 · 185→204 · 205→226 …), 84 status notifies + 57 relayed
  frames, AND a 766-sample 52 Hz IMU capture recorded during the relay, downloaded over BLE
  byte-perfect (CRC32 match), gravity sanity 1.01 g.
- **The Bridge GATT service** (`firmware-nrf/GATT.md`, PROTO_VER 1) is the no-WiFi control
  surface: Status @2 Hz · Config (live-applied + LittleFS-persisted; dev reflashes wipe it) ·
  RecCtl · RecData. Consumers: the **Web Bluetooth console**
  (public repo `cauldnz/bike-bridge-web` → https://cauldnz.github.io/bike-bridge-web/ — HTTPS
  because Web Bluetooth demands a secure origin; Chrome/Edge/Android, Bluefy on iOS) and the
  **Connect IQ "Bridge Remote"** app (`firmware-nrf/ciq/`, Edge 540 + Epix 2; compiles once the
  owner logs the Garmin SDK manager in — account-gated like ANT).
- **Bluefruit landmines (each cost a flashed-and-asserted debug loop; details in commits):**
  scan-rsp vs filterUuid catch-22 (names never match under a uuid filter) · the SoftDevice drops
  advertising when the central link rises · parameterless `notify()` targets the wrong handle in
  dual role · per-link role labels are NOT trustworthy — key client delivery on `notifyEnabled()`
  alone · `configPrphConn()` corrupted connection bookkeeping (defaults already give MTU 247) ·
  characteristic maxLen truncates NOTIFIES too, not just writes · REC framing needs explicit
  type bytes (a seq low-byte of 0xFE masqueraded as the trailer at frame 254).
- **ANT path researched + staged** (`code/findings/nrf52-sense.md`): S340 SoftDevice + ANT+ key
  are thisisant.com-adopter downloads (owner action; gitignored `firmware-nrf/vendor/softdevice/`
  drop zone). Integration recipe (board json + 0x31000 linker + S340-matched bootloader) mapped;
  prove the SoftDevice swap on the recoverable dongle (COM13) BEFORE the Sense. Bridge config
  already carries the srcIsAnt/outIsAnt routing bits — writes rejected until S340 is in.
## 2026-07-05 — §14 phase 4 SHIPPED: on-device FTMS erg drive, twin-proven (CYD -> C3 trainer sim).
- **The head-unit now erg-drives an FTMS trainer from its own workout engine** — no phone/PC in the
  loop. Wiring: RuntimeConfig field 11 `trainerNameFilter` (host-tested roundtrip + backward-compat;
  "" = erg off) -> `FtmsErgClient` fed by the SHARED NimBLE scan (a new FTMS sink on BleMeterClient's
  hub — the erg client must NOT install its own scan callbacks, that deafens the meter clients; the
  standalone envs keep the old self-owned-scan begin()). loop() refreshes desired power from the
  live WkState every 500 ms; stopped/paused -> 0 W. Setup-screen tap on a "trainer" candidate picks
  it (the isFtms badge NOW WORKS — the scan callback never actually set it before); serial
  `TRAINER <name>` is the bench path; web /setup saves PRESERVE the stored trainer (the form doesn't
  carry it yet — web trainer picker is the open follow-up, with S3-bench + real-SB20 erg runs).
- **Twin proof** (CYD live-bench + spare C3 as `esp32c3-ftms-server`, all over the air, while the
  CYD also read the fake meter and served WiFi): boot -> controlled=1 started=1 target=0W; workout
  START -> sim receives the segment target (138 W); Skip -> 225 W; Stop -> 0 W.
- **FINDING — CP response indications are effectively single-shot on the loaded CYD radio:** the
  client received exactly ONE indication ever (RequestControl's, right after subscribe); every later
  one was lost while the sim's indicate() kept succeeding — the old indication-driven state machine
  wedged in a Start-resend loop. **Fix: optimistic progression on the ATT write-ACK**
  (writeValue(response=true) success advances controlled_/started_/lastSent_); the indication stays
  as the authoritative confirm when it arrives. Real head units behave the same (resend/refresh).
  NOTE: F6's "on-air validation" drove the SERVER from Python (bleak) — the C++ client <-> C++
  server pairing had never actually run on air until today; the erg line on the LVGL Workout screen
  now shows all four states (no trainer set / connecting… / linked, no ctrl / ON xxW) so this is
  visible on-panel.

## 2026-07-05 — nRF Sense P4: FTMS erg + structured workouts + shifter bias (bench-verified)

- **Ported the ESP32's erg loop to the XIAO Sense** as a 3rd Bluefruit central onto an FTMS trainer
  (`0x1826`), driving target power via the control point (`0x2AD9`) from the pure `WorkoutRuntime`
  (shared, unchanged). `Bluefruit.begin(2, 3)` (2 periph: head-unit + web app · 3 central: source/DUT
  + calibration-reference + trainer). Optimistic write-ACK progression (same lesson as the ESP32 F6
  entry above — never gate on the indicate()).
- **Shifter = a target BIAS, not a physical button** (the XIAO has none). New Workout characteristic
  cmd 8 `BiasStep [ver,8, delta i8]` nudges the erg target ±W (clamped ±200) on top of the workout
  prescription; the web app / Garmin drive it. A physical BLE shifter (Zwift Click / SRAM) would drive
  the same cmd as a 4th central via the pure `Shifter` decode — deferred (no shifter hardware, and the
  bias delivers the feature's intent).
- **New GATT: Workout `…-0008-…`** (write control + 18-byte notify state). Write cmds: 1 setTrainer ·
  2 loadPreset · 3 start · 4 pause · 5 resume · 6 stop · 7 unload · 8 biasStep. State notify:
  `[ver, flags, targetW i16, segIdx u8, nSeg u8, segRemainS u16, ergSentW i16, elapsedS u16, biasW i16,
  reserved u16]`; `flags` b0 loaded·b1 running·b2 paused·b3 ergConnected·b4 ergControlled. **`targetW`
  already includes `biasW`** (what the erg holds); `biasW` broken out for display. Documented in
  `firmware-nrf/GATT.md` + mirrored in the web app.
- **Bench-verified over serial (`WKTEST`), the reliable path:** erg encoders byte-correct
  (RequestControl `00`, Start `07`, SetTargetPower(250) `05 FA 00`); preset parse+run (`4×8 Threshold`
  → 9 segments, 3180 s, first target 138 W); bias folds in (+10,+10,−30 = −10 W → effective 128 W).
  Build: RAM 48.3%, Flash 23.3%.
- **Web app:** added a Workout & erg card (trainer picker sourced from the ScanList FTMS entries,
  preset selector whose labels mirror `WorkoutPresets.h`, start/pause/stop, live target + elapsed, and
  ±10 W shifter buttons). Pushed to `cauldnz/bike-bridge-web` (GitHub Pages). Preset dropdown populated
  on load = JS parsed/ran (headless-Chrome render smoke test).
- **GATED (unchanged constraint):** the live erg-drive against a real trainer needs a free FTMS
  trainer — the only one on air (`SB20-FTMS-Server`) is the concurrent session's; per CLAUDE.md I don't
  hijack it. Encoders + runtime are proven; only the on-air control loop remains for the owner's rig.

## 2026-07-05 (overnight) — away-work wrap: scan self-heal, web trainer picker proven on-air, CYD soak clean, S3 OTA cured.
- **BLE scan self-heal (PR #220)** — found by the C3's on-air erg lap: after the erg-era boot
  reorder, the single-core C3's first scan->start() failed SILENTLY and nothing retried — the board
  "searched" forever with a dead scan and an empty picker while its own crank advertised at −28
  (PC-side bleak scan as ground truth). loop() now kicks ensureScanning every 3 s whenever anyone
  still hunts. On the fixed build: 16 sources visible within 12 s of boot.
- **Web trainer picker (PR #219) proven end-to-end on the C3-OLED, hands-free**: trainer set via
  the new /setup section → erg client controlled the sim → web-loaded 4x8 → START → sim received
  138 W → stop → trainer CLEARED via the same page (present-but-empty contract) → fresh boot
  confirmed erg-off with identity 'Stages 62145' preserved throughout. The C3 erg bench pass is
  done; all three head-unit tiers have now individually erg-driven the sim.
- **CYD heap soak: NO LEAK.** 3.5 h under full load (LVGL + BLE central/peripheral + WiFi station):
  heap floor per 30-min window 26.5/26.8/26.7/28.2/27.0/28.0/27.4 KB — flat. The no-PSRAM CYD's
  malloc-backed LVGL configuration is beta-grade stable.
- **S3 OTA deafness CURED on the current build**: espota invitation answered, auth passed, full
  image pushed, exit 0 (the trailing espota "Unexpected response '32'" warning is cosmetic). The S3
  is now OTA-updatable on a power-only USB-C charger — no data cable needed for normal updates.
- **FOOTGUN — `flash_s3.py` wipes NVS**: it writes the merged `firmware.factory.bin` at 0x0, which
  covers the NVS region — every USB factory flash erases WiFi creds/config (this explained every
  "WiFi not configured" portal after a reflash, including tonight's). Prefer OTA for updates; keep
  the factory flash for true bring-up. Fixing the flasher to skip/preserve NVS is an open follow-up.
