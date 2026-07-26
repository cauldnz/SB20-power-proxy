# Running a physical (on-bike) session — the playbook

On-bike sessions spend the scarcest resource in this project: **the rider's time and patience**. The
agent directs; the human is the hands and eyes. Treat every session as expensive, and aim for each to be
**better-run than the last**. Three rules above all the detail below:

1. **Never send the human to do something you haven't verified is ready.** Everything desk-testable is
   done and green *first*; the bike is only for what genuinely needs the hardware.
2. **One step at a time, with an explicit pass/fail.** The human should never have to improvise or judge —
   you give one instruction, state what they should see, they narrate, you confirm against the data, next.
3. **Every session leaves a record *and* a retro.** Capture the bytes, write down what happened, and turn
   what went wrong (and right) into a concrete change *before* the next session.

This pairs with CLAUDE.md → *Session plans & the session ledger* (the doc/status/ledger mechanics) and
*Git & branch hygiene*. Read this before directing any session.

---

## 1 · Plan (at the desk, before the rider is involved)

- **Desk-derisk everything first.** Compile, host-tests, software loopback, capture-tool smoke test,
  fixtures from real captures — all green before the session. The rider's time is *not* for catching
  something a `pytest`/compile/loopback would have caught.
- **Pre-stage turnkey.** Firmware built **and flashed and verified on the actual board**; tools
  smoke-tested; every command ready to **paste** (no improvising at the bike); restore values written
  down. The rider should arrive to a session that just runs. *(Sessions 2–3 went in pre-flashed and
  desk-derisked against the Python harness — a WinRT CPS peripheral + a bleak central stand in for the
  SB20 — so the bike only had to answer "does the real SB20 accept it?", not "does the firmware run?")*
- **Front-load the gates.** Order steps so the **highest-information go/no-go comes first** — while the
  rider is fresh, the battery full, the meter awake. Don't spend patience on low-value steps early; a
  go/no-go that fails early saves the rest of the session. *(In session 2 "does the SB20 display the
  relayed Assioma watts at all?" was the first thing run — once it passed, the protocol-capture work
  was all upside; had it failed, the rest of the plan was moot.)*
- **Audit placeholders/unknowns at plan time; order gates so the grounding capture comes first.** If a
  value the test depends on is a flagged placeholder or "unconfirmed" (e.g. a spoof field hard-set to
  `0x0000`), the dependent test is *predetermined to fail until a capture grounds it* — running a "does it
  work?" gate before that capture only confirms the obvious and burns rider patience. List every such
  unknown in the spoof/correction surface; schedule the **grounding capture strictly before** any gate
  that consumes it, and frame the dependent gate as a *post-reflash confirmation*, not a standalone live
  test. *(Session 8: `SPOOF_MFG_COMPANY_ID = 0x0000` made the G2 calibrate-spin foreseeable, yet G2 was
  run before the G1 capture that grounded it — a wasted cycle on the rider's clock.)*
- **Batch by physical setup.** Group everything that needs the same rig state (a battery pulled, a given
  pairing) so the rig changes as few times as possible. Each battery swap / re-pair is a patience tax.
- **Write each step as Plan**: *goal · exact action · expected result (explicit pass/fail) · what to
  capture.* Mark **must-do vs optional/stretch**, and give an **honest time budget up front** ("~45 min,
  3 must-dos").
- **A step that can *fail* is investigation, not verification — budget it as such.** Session 3's "verify
  the firmware fixes" read as quick, but two of four steps failed and became **~30 min of diagnosis
  against a planned 20**. When a step's pass/fail is genuinely unknown, pad it, and treat a fail as the
  start of a capture-and-iterate loop, not a retry. (The biggest, most persistent estimate misses come
  from calling investigation "verification".) *(BIKE-SESSION-3 retro.)*
- **A fallback per gate.** For each likely failure (pairing rejected, OTA drops, meter bounces), the next
  move is already written — the rider never waits while you think.
- **Bring-list + restore-list.** What the rider must have on hand (battery, tools, the right app), and the
  exact state to restore afterwards — written before they start. *(Session 3's restore line is the model:
  "`Stages 62144 (L) : 4963 (R)` · crank length 165 mm · zero-offset L 903 / R 951" — written down
  **before** touching the pairing, because only the L id changes for the spoof and the R crank stays in.)*

### Pre-flight checklist (the must-dos every bike session has needed)

These are not aspirational — each is here because skipping it cost a real session. Walk it before the
rider starts:

- [ ] **Fresh CR2032 in the real LEFT crank.** It has read **12–14%** before *three* separate sessions
  (`62144`) — low enough to drop out mid-capture, and a weak battery can also skew power. The spoof test
  needs the *real* crank quiet, but a restore at the end needs it alive. Bring the coin/screwdriver too.
- [ ] **Firmware confirmed current with `origin/main` and flashed + verified on the board.** Flashing a
  superseded branch wastes the session and can ship a wrong value (the ANT+ offset `903` once leaked onto
  the BLE crank, which must answer `0`). Local ESP32 compile is the pre-flash gate; `curl /` should show a
  **low uptime** after the flash (it just rebooted) and `source:searching`.
- [ ] **The build + flash toolchain actually RUNS on THIS machine — verify by building + flashing once, at
  the desk.** Work moves between the desk and the bike laptop (different machines), and a Python upgrade can
  silently orphan the toolchain. Confirm `pio`/`platformio` runs, an ESP32 toolchain is cached, a host
  compiler exists for `pio test -e native`, **and** an OTA upload reaches the board — *before* the rider is
  at the bike. **Now committed:** `tools/provision-dev-env.ps1` provisions the pinned toolchain (PlatformIO
  + the BLE venv; pins in `tools/dev-env.lock`) and `tools/doctor.ps1` is the pre-flight gate — green it at
  the desk. **It now also gates the dual-radio capture rig** (Npcap + nRF Sniffer extcap + dongle + the ANT+
  WSL claim), so a green doctor means we can build, flash, *and* capture — see the dual-radio item below.
  *(Session 8: the bike laptop had no
  PlatformIO — Py 3.14 orphaned it, empty `~/.platformio` — and no `gcc` → ~30 min of mid-session bring-up;
  the exact "does the tool run?" failure step 1 exists to prevent.)*
- [ ] **Board near the AP — check RSSI before OTA.** OTA gets unreliable below ~**−72 dBm** on the C3
  (`WiFi -XX` on the OLED / `rssi` in `curl /`). `flash.ps1` warns and retries; if it still drops, move
  the board closer or USB-flash. Don't burn rider time on a flaky OTA you could see coming.
- [ ] **Restore values written down** (pairing ids, crank length, zero-offsets) **before** changing any
  pairing — see the bring-/restore-list rule above.
- [ ] **Meter awake + source pinned.** Pedal a few strokes so `curl /` shows `source:connected` with
  `src_power_w` tracking the Assioma. Note: the Assioma presents as **two** BLE devices (`ASSIOMA17039L`
  *and* `ASSIOMA22428R`) and the real Stages cranks also advertise CPS — pin the source by address or the
  client bounces (see Lessons §BLE).
- [ ] **A rolling `/log` window open** in a second terminal — you watch it through the whole session.
- [ ] **Always-on DUAL-RADIO capture for the WHOLE session — pre-staged, agent-run (standing rule).**
  The rider's time is the budget, so leave the radios capturing the entire ride (**ANT+ always; nRF when
  free** — details below) and mine the "exhaust" at the desk — it turns one bike trip into replayable data,
  and lets a later question (e.g. §12, a missed handshake) be answered from the capture instead of another
  trip. Run them agent-side (the radios live on the bike machine; the rider never manages them):
  - **nRF BLE sniffer → pcap (default-on, but YIELDS).** Single dongle, follows **one** link at a time — so
    if the ride needs the nRF for another purpose, that wins and the background sniff steps aside (ANT+ still
    covers you). When it IS sniffing it captures the BLE air — app↔SB20, SB20↔crank, our ESP↔SB20 — and you
    must **start it BEFORE anything connects** (the nRF can only *follow* a connection whose `CONNECT_IND` it
    caught; power-cycle / re-pair the target as the sniff goes live and pick the link that matters;
    started-after-connect = adverts only, the loss in session 6). *(see §Passive BLE sniffing below.)*
  - **ANT+ capture → JSONL (ALWAYS on — guaranteed, no excuse).** There are **≥3 ANT sticks** on hand, so one
    is always free to dedicate to passive capture even if another's used elsewhere — this runs *every* ride.
    The independent power+cadence reference (Assioma + cranks + SB20 FE-C) on a *separate* radio (zero
    contention with the BLE work) — the ground-truth every gate reconciles against, because the bike's own
    number can't validate itself (the Stages cranks read **~5–13 % high vs the Assioma, cadence-dependent**).
    Pin the Assioma by its explicit ANT id; never wildcard.
  - **Pre-stage BOTH at the desk** — they are the two biggest mid-ride time-sinks: the ANT+ stick over WSL +
    `usbipd` (`wsl-capture-runbook.md`; native Windows is BLE-only — ride 1 lost ~25 min to bring-up) and the
    nRF dongle + Wireshark/tshark. **Verify both are *actually capturing* before the rider is at the bike** —
    a sniffer you think is running but isn't is worse than none.
  - **⚠️ HARD RULE (session 9, 2026-06-26 — learned the painful way):** `doctor.ps1` MUST **gate the capture
    rig**, not just build/flash — assert Npcap/tshark present + the nRF Sniffer extcap registered + the dongle
    on its COM port + the ANT+ WSL claim (a libusb open). And **any "check everything" / readiness pass MUST do
    a few-second live test capture on EACH radio** — never just tick the box. *(Session 9: the nRF sniffer was
    NOT ready — Npcap uninstalled, extcap unregistered — and it slipped through TWO explicit "check everything"
    passes, night-before AND morning, because `doctor.ps1` only checked build/flash. A green doctor MUST mean
    the captures actually work.)* **Now implemented:** `doctor.ps1` default-gates the rig — Npcap (`wpcap`
    loads) + the nRF Sniffer extcap registered + the dongle on its COM port + the ANT+ WSL libusb claim — and
    FAILs (non-zero exit) with the exact fix when a piece is missing (`-NoNrf`/`-NoAnt` only when a radio is
    *intentionally* absent; `-NoCaptureRig` for a build-only run). So the readiness procedure is now: **(1)**
    run `.\tools\doctor.ps1` at the desk and get the **capture rig green**, then **(2)** take a **few-second
    live test capture on each radio** — nRF: `tshark -i <nRF-Sniffer-iface> -a duration:3 -w test.pcapng`;
    ANT+: a short `02_capture_assioma.py` — and confirm frames/pages actually land. Standing the nRF path up
    at the desk (Npcap install, extcap register, dongle firmware): `tools/README.md` > "Capture rig setup".
    **Never tick the capture box without doing both.**
  - **Commit the captures** (pcap + ANT JSONL) at close-out — canonical + lossless.
  - **Analyse by QUERYING `captures.sqlite`, never by dumping raw captures into context.** Parse both into the
    SQLite index (BLE via `tshark`→`pcap_sqlite.py`; the ANT JSONL likewise) and run SQL for the exact rows a
    question needs. This is the **token-lite** path (cheap for the agent) *and* the **capture-once, query-
    forever** path (no re-riding to recover data) — it protects BOTH the agent's token budget AND the rider's
    time/sanity (not having to re-do things), which is the whole reason to capture-everything-and-index.
    *(Owner standing ask, 2026-06-25: nRF + ANT running **any** time we're on the bike, parsed **token-lite
    to SQLite** for later exploration — ANT+ always (≥3 sticks), nRF when free; supersedes the Session-4
    ANT-only ask.)*

## 2 · Execute (live, you driving)

- **The rider tells you, explicitly, when they're AT THE BIKE — never infer it from the conversation.** Direct a physical step (pedal, pair, pull a battery, press a button) **only after** an explicit "I'm at the bike"; until then, treat every request as **desk prep**. The rider routinely asks for prep *before* heading downstairs ("set the ESP id", "stage the capture") and then says, explicitly, when they've arrived — so staging is **not** permission to start the bike steps. Pre-empting it ("now type L=… in the app") wastes the moment and cuts across their workflow. Stage everything desk-side; **hold the physical steps for the explicit cue.** *(Session 8: the agent issued an app-pairing step while the rider was still upstairs; owner: "I will always be explicit about being at the bike.")*
- **Feed one step; wait for the narrated result; confirm against the data** (`curl` / `/log` / the capture
  file) **before advancing.** Never assume a step passed because it "should have." *(In session 2 the
  SB20's interactive protocol — control-point writes, the proprietary `fe02` token, disconnect reasons —
  was invisible to any sniffer; the **only** thing that saw it was our own spoofed crank logging to
  `/log`. The diagnostic endpoint is how you confirm, not the rider's read of the app UI alone.)*
- **You run the tooling; the rider's hands are only for the hardware.** The BLE/ANT+ radio and your shell
  are on the **same** bike machine, so launch every capture/flash command **yourself** (use
  `run_in_background` so it doesn't block you), and leave the rider only the physical actions — pedal, pull
  a battery, press a button, narrate. **Don't make them paste commands**; that was an unnecessary tax in the
  earlier run sheets. *(Session 4: `capture_ftms.py --erg` was run agent-side in the background; the rider
  only pedalled.)*
- **The capture scripts are line-buffered (`buffering=1`) — tail the JSONL *live*.** `Read` the output file
  **as it is being written** and confirm pass/fail in real time (the control-point ACKs, the power
  tracking), feeding the rider live confirmation instead of waiting for the run to finish — and stop the run
  early (`TaskStop`) once the result is in, to free the radio for the next step. Both `capture_ftms.py` and
  `06_capture_ble.py` open with `buffering=1`; **verify a new script flushes per line before relying on
  this.** *(Session 4 §C: read the erg `80 05 01` ACKs + Indoor-Bike-Data power off
  `G-sb20-ftms-erg-*.jsonl` live, called the PASS before the run ended, then `TaskStop`ped it.)* **Launch a
  live-walk capture only when the rider is ready to *act*, and stop it the moment the actions are done** —
  don't let it run through an off-bike discussion. *(Session 4 §B burned a 420 s shifter-probe window
  mid-chat capturing only idle; the button-4/5 map + gestures had to be re-captured.)*
- **Forking off out-of-scope work mid-ride — use a background sub-task, NOT `/btw`.** When the rider says
  "we should also look at X," capture it in ONE move and keep the ride going: spawn a **background subagent**
  (the spawn-task chip / "run this in the background" / Ctrl+B) for self-contained work, or drop a one-liner
  into the backlog (`code/findings/forward-plan.md`). **`/btw` is the wrong tool for this** — it's a
  *read-only side chat* (full context, **zero tool access**) for quick clarification questions only, so
  asking it to "add to the backlog" makes it *describe* the action without doing it (the "it got confused
  and did a bunch of other stuff" the owner hit). *(Session 4: the owner's "investigate SQLite" → a
  background subagent that built **and PR'd** the layer without touching the live ride; reviewed + merged at
  close-out.)* **Gotcha:** a forked task that depends on *this session's* artifacts can land not-ready —
  Session 4's SQLite PR went CI-red because its tests read a capture not on `main` until the ride's
  close-out merged. Note the dependency and verify/merge the fork **after** the session's captures land.
- **State the expected result *before* they act** — "you should see `source:connected` and ~200 W; tell me
  what you see." Make pass/fail something the human **observes**, not **interprets**.
- **Record actuals inline as you go** — `✅`/`❌`/`⚠️` + the observed bytes/values/`/log` lines, written
  into the session doc, not just chat (the Plan/Actual rule). Memory is lossy; the rider is busy.
- **Timestamp every step (wall-clock, in the doc).** Note the local time (`HH:MM`) when each section and
  key step *starts*, beside its result — so **actual** durations are recorded, not reconstructed from
  memory. The agent has been an unreliable time estimator (bike *and* desk work); logged actuals are the
  only thing that fixes that. One clock-read per step turns every session into calibration data for the
  planned-vs-actual review (§4). Get a timestamp at the start of the session and at each section boundary.
  **Don't trust the agent's shell clock blindly** — when a capture is running, the file's `iso_time` (written
  by the real, un-sandboxed capture process) is ground truth; anchor the doc's times on it (a `Get-Date` with
  the sandbox disabled also reads true). *(Session 4: the agent's sandboxed shell `Get-Date` read **a day +
  ~6 h** off the real clock; the capture `iso_time`s were correct, so the whole log was re-based on them.)*
- **Record every mid-session instruction change, not just the planned steps — the capture depends on it.**
  When you deviate from the written plan live (an ad-hoc instruction like "now press LEFT-up **ten
  separate times**", a changed order, an improvised probe), **write the new instruction AND what the rider
  physically did into the doc**, beside its timestamp. A capture is only interpretable later if you know
  what action produced each moment — a BLE frame burst is genuinely ambiguous (one long *hold*? ten *taps*?
  a *double-tap*?) without the narration logged. *(Session 3: an unrecorded "do 10 separate clicks"
  improvisation was later misread from the capture as a single long hold; the deviation log would have
  caught the mistake — and without it, the behaviour had to be re-tested on the bike.)*
- **Protect the rig.** Write current state down before changing anything; restore it at the end; confirm
  normal operation before they leave.
- **Know when to stop ratholing.** If a step is stuck, don't loop the rider through blind retries — grab
  the diagnostic (`/log`, a capture), have them pause/hold, and either move to the next item or iterate
  off the captured data at the desk. Blind retries burn patience for zero information. *(Session 2's
  zero-reset spun forever because the firmware logged the `0x10` write but never answered it; the right
  move was to **capture the unanswered write and move on** — that one `/log` line was the entire spec for
  the PR #5 control-point responder. Looping the rider through more calibrate taps would have taught us
  nothing the first capture didn't.)*
- **Keep the rider oriented** — "2 of 3 gates done; this next one's the big one." They can't see your plan.

## 3 · Document (immediately after, while it's fresh)

- **Mark the session `✅ DONE` with a one-line Outcome** atop its doc + update the ledger row.
- **Promote durable findings** to `code/findings/decisions.md` (append-only) and **commit every capture**
  to `code/findings/captures/` — canonical and lossless. Interpretation can happen later; the bytes can't
  be re-collected without another trip. *(The day-1 calibration handshake was caught only on the **third**
  C-0 attempt — `C0-ack-dryrun-20260614-164426.jsonl` has the 8 cal pages the first two missed on timing;
  committing it meant the offset 903/-950 and the `01 AC FF FF FF FF <offset>` byte layout were nailed
  down for good. And the analysis — Stages reads ~13% high at 60 rpm vs ~5% at 100 rpm — came out of the
  JSONL **days later**, not at the bike.)*
- **Leave the next gate explicit:** what this result unblocks, and what **desk work must precede** the next
  visit (so the next session is also pre-staged turnkey).
- **Retarget the cold-start to the next READY ride(s).** `BIKE-SESSION-READY.md` is the first thing the
  bike-machine assistant reads — if it still names the session you just closed, the next rider gets walked
  through finished work. Closing a session is not done until the cold-start (and the ledger's `Latest done`
  header) point at the next 🟢 READY session(s), with current device coordinates + restore values. *(2026-06-23:
  the cold-start sat on session 4 for days after it went DONE while sessions 8 + 5 became the live rides — PR #120.)*

## 4 · Retro (the part that compounds — never skip it)

End every session with a short **Retro** block in its doc. This is the mechanism that makes each session
better than the last:

- **What went well** — name it so we keep doing it.
- **What went wrong / was slow / was confusing** — and the **root cause**, not just the symptom.
- **The change** — turn each into a concrete fix to the **process, the run-sheet format, or the tooling**,
  *before* the next session: a new pre-flight check, a flash helper, a clearer step, a captured fallback.
  *A retro item without a resulting change is just a complaint.* *(The repeated C3 flashing pain became
  `firmware/flash.ps1` — RSSI pre-flight, OTA auto-retry, the USB-JTAG BOOT/RESET recipe baked in; the
  ~25 min of WSL/USB gremlins on the first ride became `run_capture.sh` + a 60-second pre-flight in
  `wsl-capture-runbook.md`. Each is a retro item that turned into tooling.)*
- **Did we reduce future trips?** The best outcome is needing **fewer/shorter** sessions next time — batch
  more, derisk more, capture enough to iterate at the desk instead of on the bike.
- **Planned vs actual — tabulate it from the timestamps.** Lay each section's *planned* budget beside its
  *actual* wall-clock (from the per-step timestamps), note the delta and its cause, and carry those deltas
  into the next session's Plan. The estimate only stops being wrong if the miss is written down; an
  unrecorded over/under just repeats. (This project's running weak spot is the agent's time estimates.)
- **Fold the lessons back into THIS playbook — every session, not "someday".** The retro's durable
  lessons (a new gotcha, a process gap, a tooling fix, a better-prepared run-sheet shape) belong in
  `sessions/PLAYBOOK.md` so the *next* session inherits them — the rules above and the §Lessons section
  **grow each session**. A lesson left in one session's retro is re-learned on the rider's time.
  **Reviewing the retro and updating this playbook is a required step of closing a session** — the
  playbook is a living, compounding doc, and "is it better-prepared than last time?" is answered here.

Suggested stub to drop at the bottom of each session doc:
```
## Retro
- Went well:
- Went wrong / slow / confusing (+ root cause):
- Planned vs actual (per section, from the timestamps; + the delta & why):
- Changes to make before next session (process / run-sheet / tooling):
- Next gate + desk work that must precede it:
```

---

## Lessons from real sessions so far (grounded)

Every item below is attributable to a specific event — a session, a `decisions.md` entry, a committed
capture, or a logged symptom. They are the texture the rules above are abstracted from; read them so you
don't re-learn each one on the rider's time. Citations are in *(parens)*.

### Passive BLE sniffing (nRF dongle)

- **Start the sniff BEFORE the target connects — the nRF sniffer can only *follow* a connection if it catches
  the `CONNECT_IND`.** If the link is already up when the sniff starts you get **adverts only** — no ATT, no
  characteristic data — even though the device is plainly connected and working. *(Session 6: `Block S`
  started before the app connected → 636 ATT ops, the full erg conversation; the `sweep`/`zero` sniffs started
  after the SB20 was already connected → ~4979 frames of pure advertising, zero connection data, and the
  crank-CPS reconcile was lost. It was **not** encryption.)* ⟹ **power-cycle the SB20 / toggle the pairing as
  the sniff goes live**, then connect — and confirm non-zero ATT (a `CONNECT_IND`) before trusting the capture.
- **Follow the RIGHT device — and demand a positive in-flight signal, not a healthy-looking process.** A
  "connected fitness device" is often SEVERAL BLE personalities (the SB20 is ≥3: the bike's FTMS at
  `E4:AA:5A:D6:0E:D4`, the crank addresses "Stages 62144", an **"SB20 Bridge"** `de:f2:ed:c4:f3:fd`), and a
  controller app may connect to a different one than the docs assume. Target by **address**, never by name
  substring (`--name SB20` would have matched "SB20 Bridge"). Then verify DURING the capture: **within ~1 min
  of the app connecting, the followed device's adverts must STOP** (a connected peripheral goes quiet) — a
  climbing packet counter is NOT evidence of capturing the conversation; adverts alone climb just as happily.
  Generalize this: every long capture needs a defined **"the interesting data is actually flowing" check**
  (adverts stop / ATT frames appear / the JSONL grows past discovery), run in the first minute while
  re-targeting is still cheap. *(2026-07-06 qdomyos ride: a 30-min follow of `E4:AA:5A` looked healthy at
  20k packets — all adverts; qdomyos drove resistance over a different link the whole time. The 1-minute
  adverts-stopped check would have caught it at 07:45. `decisions.md` 2026-07-06.)*
- **Capture headless with `code/scripts/sniff_ble.py` — NOT Wireshark/Npcap.** It drives Nordic's `SnifferAPI`
  straight over the dongle's serial port (sniffer firmware, USB PID `522A`) → pcap; **no Npcap, no tshark**
  (those are only the interactive GUI alternative). `tools/doctor.ps1` gates the rig (dongle on sniffer fw +
  the `SnifferAPI` extcap staged + pyserial in `code/.venv`). **Read
  [`code/findings/nrf-sniffer.md`](../code/findings/nrf-sniffer.md) before touching any of this** — and note the
  `SnifferAPI` extcap can go **un-staged between sessions** (it did, 06-21 → 06-26), silently killing the path.
  *(Session 9: the morning "nRF not ready → install Npcap" call was the WRONG path — the real blockers were the
  un-staged extcap + missing pyserial, which doctor.ps1 now catches. `decisions.md` 2026-06-26.)* **Root cause
  now known (2026-07-06): a Wireshark upgrade WIPES `%APPDATA%\Wireshark\extcap`** — so "verified N days ago"
  means nothing across an upgrade. The durable copy lives in the makerdiary checkout; the no-restage fallback is
  `--extcap-dir C:\repos\nrf52840-mdk-usb-dongle\tools\ble_sniffer\extcap`.
- **Parse pcaps with `tshark`, never by hand.** Wireshark's CLI natively dissects the `LINKTYPE_NORDIC_BLE`
  pcaps down to ATT/GATT; `analysis/pcap_sqlite.py` shells out to it → SQLite (and `fit_sqlite.py` does the
  Garmin FITs → the same index, so a reconcile is a JOIN). A pure-Python Nordic-BLE parser is a tar-pit — a
  subagent stalled on it before tshark made it a non-problem. *(Session 6; `decisions.md` 2026-06-21.)*

### Flashing & OTA (the recurring time-sink)

- **Weak-signal OTA drops below ~−72 dBm on the C3.** OTA to the board gets unreliable in the drop zone;
  the fix is move the board nearer the AP and retry. This recurred enough to be baked into `flash.ps1`
  (RSSI pre-flight + auto-retry) and `firmware/BENCH-FLASH.md`. *(decisions.md 2026-06-17; `flash.ps1`
  lines 40–59. Conversely, a desk OTA at −73 dBm **succeeded** — the threshold is real but close, so
  check, don't assume.)*
- **espota needs an explicit host IP on a multi-NIC machine.** PlatformIO's `-t upload` espota integration
  auto-picked host_ip `0.0.0.0` on a laptop with WiFi + WSL/Hyper-V vEthernet + link-local interfaces → the
  board had nowhere to connect back to → **"No response from device"** (the UDP invitation is sent, but the
  reverse TCP never lands). Fix: call `espota.py` directly with `-I <host-lan-ip>` (the interface on the
  board's subnet); `flash.ps1` should pin it rather than relying on auto-detect. *(Session 8:
  `espota.py -i 192.168.1.165 -I 192.168.1.223 -p 3232 -f firmware.bin` succeeded where
  `pio run -e …-oled-live-ota -t upload` failed.)*
- **The C3 doesn't roam between WiFi APs — power-cycle it to re-associate with the nearest one.** It stays
  bonded to whatever AP it first joined; if the board or the rider then moves into another AP's coverage, it
  clings to the now-distant one — RSSI craters (**−89 dBm** seen), `/` and `/log` time out, OTA is hopeless,
  and any live `curl`-based reference (e.g. reading `src_power_w` off `/`) is lost. **Pre-flight + recovery
  step: if `rssi` (in `curl /`) is low or the board is unreachable, power-cycle the ESP** so it re-scans and
  joins the closest AP — and re-check `rssi` after any time the board (or rider) changes floors/rooms.
  *(Session 4: board sat on the upstairs AP after the rider went downstairs → −89 dBm, all Assioma polls
  timed out, the live cross-check was lost; power-cycle + move-nearer restored it. **Owner re-confirmed:**
  two floors down dropped to **−92 dBm**; the **Reset button** reconnected at **−68** on the near AP — the
  C3 picks its AP only at (re)boot, never roams.)*
- **USB-JTAG bootloader wedge.** Symptoms: "No serial data received" / "Unable to verify flash chip
  connection" — the C3's native-USB auto-reset didn't enter the bootloader. Recover with **HOLD BOOT,
  TAP RESET, RELEASE BOOT**, re-run the upload, then **power-cycle**. Upload speed must be pinned to
  115200 (the C3 USB-JTAG drops the stub at the 460800 default). *(`firmware/BENCH-FLASH.md`; memory
  [[esp32-c3-flashing]]; the 2026-06-16 bench session lived this — "Gonna power cycle it now and try and
  put it in bootloader mode".)*
- **Native-USB serial is unreliable on the C3 Super Mini — that's *why* `/log` exists.** Debugging over
  the cable is flaky, so the firmware serves its serial log over HTTP at `GET /log` (a RAM ring buffer).
  This is the single most useful live-session instrument; the whole "rider narrates, agent reads `/log`"
  model depends on it. *(decisions.md 2026-06-15 §5 `/log`; 2026-06-18 PR #5.)*
- **The board can hang under coex load and self-reboot.** During bench-flash the rider reported "the LED
  stops flashing, screen stops updating, then it restarts and appears to reconnect (failsafe?)" — that's
  the boot-guard `esp_timer` failsafe firing on a loop-stall, not a flashing fault. The root cause was the
  OLED I2C render blocking the hot loop (see Time/patience savers). *(decisions.md 2026-06-16; transcript
  2026-06-16.)*
- **A freshly-rebooted board needs ~25 s before its HTTP server rebinds — don't mistake it for dead.** After
  a reflash/reset, ping and BLE come up first; `/`, `/status`, `/log` time out for ~25 s while the WiFi +
  web server re-init. Wait it out (or re-check) before declaring the board hung and power-cycling it again.
  `doctor.ps1 -BoardIp <ip>` prints this reminder when `/status` is unreachable. *(Session 9: ~25 s of
  "looks dead; wasn't" after a board restart.)*

### BLE pairing & the SB20's quirks

- **Duplicate `Stages 62144` advertiser.** The real L crank **and** the ESP both advertise the name
  "Stages 62144", so the SB20 (or a capture tool, or a sibling board) can grab the wrong one. For spoof
  tests, **pull the real L-crank battery** so the ESP is the only `62144`; for tools, disambiguate by
  `--address`. *(decisions.md 2026-06-17, 2026-06-18; BIKE-SESSION-2/3 setup step.)*
- **The SB20 needs BOTH configured crank IDs findable on air to pair — symmetric.** App set to
  L=`62145`(present) + R=`4964`(absent) → "pairing failed", and the SB20 **dropped + never re-attempted** our
  ESP (`disconnect reason=531`, no `[srv] connect`); L=`42146`(absent) + R=`4963`(present) → the *same* fail;
  both present → connects immediately. So an absent **left *or* right** id aborts the whole pairing — it is
  **not** "needs a right crank" (session 8's narrower read). Implication for a sole-source rig (Assioma only,
  no real cranks): the ESP must answer *both* configured ids (the 2nd-phantom-right-peripheral path). App- vs
  bike-gatekeeper is still open (needs an nRF sniff of the app↔bike link). *(Session 9; forward-plan.md §12;
  `decisions.md` 2026-06-26.)*
- **The SB20 terminates the link if a control-point write goes unanswered.** Session 2 logged
  **`disconnect reason=531`** (NimBLE `0x0213` = HCI `0x13`, "remote user terminated") every time a CP
  procedure wasn't answered — and a side effect was the *next* write (Set Crank Length `0x04`) never even
  landed because the link dropped first. **Every CP write must be answered**: zero-reset is Enhanced Offset
  Compensation `0x10`, crank length is `0x04` set / `0x05` request. *(decisions.md 2026-06-18;
  `session2-log-20260618-0713.txt`.)*
- **Re-advertise on disconnect, or the rider is stuck.** After an SB20-terminated drop the ESP did *not*
  resume advertising → the app sat at "searching" and **only an ESP reboot recovered it**. The SB20
  remembers the pairing and reconnects on its own once we re-advertise (and re-sends its `fe02` token).
  PR #5 made re-advertise-on-disconnect a firmware requirement. *(decisions.md 2026-06-18.)*
- **Meter-source bouncing.** With several CPS advertisers live — `ASSIOMA17039L`, `ASSIOMA22428R`, and
  the real `Stages 4963` — the meter client bounced between them and at one point relayed the **real right
  crank** to the SB20. Pin the read source by address (`Config::METER_ADDRESS`). (Silver lining: that
  accidental `4963` relay demonstrated the single-surviving-right-crank use case.) *(decisions.md
  2026-06-18; forward-plan.md §8.)*
- **Minimal spoof ≠ accepted spoof.** Session-G Part B: the SB20 **paired but showed 0 W** to a minimal
  flags-`0x20` crank. It only accepted power once the firmware became **byte-faithful** to the real crank's
  full surface — flags `0x002F` 11-byte frame, CP Feature `0x0008030B`, Sensor Location `0x00`, DIS
  model `SPM2` / FW `1.8.2`, and the proprietary `d445fe01` service present. Golden-vector host tests were
  built from the captured bytes, not invented. *(decisions.md 2026-06-17; `G-crankL-ble-recon-20260617.jsonl`.)*
- **903 vs 0 — verify the cal offset against the real capture.** The crank's zero-offset reads **903 over
  ANT+** (raw zero-offset, page 0x01 / 0xAC) but **0 over BLE** (`200c010000`, the post-compensation
  residual). The ANT+ value `903` was almost shipped on the BLE crank's `SPOOF_CAL_OFFSET`; the fix was
  `903 → 0`, byte-matched to the captured `200c010000`. Different representations of the same calibrated
  state — confirm which protocol you're answering before trusting a number. *(decisions.md 2026-06-19;
  `G-crank62144-ble-zero-20260615-070353.jsonl`.)*
- **Zero-reset "spun forever" until the firmware ACKed it.** In session 2 the app's calibration spun and
  the rider cancelled, because the ESP logged the `0x10` write but sent no indication back. Fixed in PR #5
  (ACK `0x10` with a synthetic success — the Assioma is the real calibrated meter, nothing to zero on the
  ESP). Session 3 step A.1 verifies the fix on the bike. *(decisions.md 2026-06-18; BIKE-SESSION-3 §A.)*
  **Session 9 confirmed the next layer on air:** the app's `0x10` is now *forwarded* as a Start-Offset-
  Compensation `0x0C` to the real Assioma, which returns **`200c01ffff`** (SUCCESS, offset −1) — a genuine
  zero, not just a synthetic ACK; the app completes + shows `901/951`. *(decisions.md 2026-06-26;
  `G-zero-reset-onair-pass-20260626-0651.txt`.)*

### Capture discipline

- **Capture first; interpret at the desk.** You can't re-collect bytes without another trip. The
  calibration-offset surface (ratio cadence-dependence), open-question #7 (pass-through), the shifter
  bitmap — all came out of committed JSONL *after* the ride, not live. *(decisions.md 2026-06-14 calibration
  result, 2026-06-15 #7; captures `A-stagesL-steady-*`, `QUICK-multi-20260615-064037.jsonl`.)*
- **Timing-sensitive captures need the event to land inside the window.** The C-0 calibration handshake was
  missed on the **first two** attempts (rider still pedalling at window end) and only caught on the third,
  with the zero-reset done early. Design the cue so the artefact is centred in the capture, not at its edge.
  *(decisions.md 2026-06-14 ride results; `C0-ack-dryrun-20260614-164426.jsonl`.)*
- **A clean confirmation removes self-deception.** To prove Assioma → ESP → SB20 with zero real-crank
  involvement, **both** crank batteries (L `62144`, R `4963`) were pulled — leaving the Assioma as the only
  possible source — and the SB20 still showed power + cadence. Design the proof so the result can only mean
  one thing. *(decisions.md 2026-06-18; `session2-confirm-assioma-20260618-0742.txt`.)*
- **The captured-bytes spec drives the firmware, line for line.** The session-2 `/log` dump became the
  literal to-do list for PR #5 (`fe02 = bfda1853` constant token, `0x10`/`0x04`/`0x05` CP ops,
  re-advertise). Run the dump through `sb20proxy.logparse.parse_log` for a clean spec. *(decisions.md
  2026-06-18; `code/findings/shifter-ble-protocol.md`.)*
- **At plan time, verify what a named capture script actually RECORDS — don't infer from its filename.**
  A run-sheet step is only as good as its tool's real output: read the script's output schema or smoke-run
  it and look at what lands in the file. *(2026-07-06: the run-sheet used `16_scan_ant.py --output` as the
  ride-long "ANT+ capture", but it's a **discovery scanner** — device IDs only, no data stream; a ride
  wanting ANT+ payloads needs `01_capture_stages.py`/`07_capture_multi.py`. Caught mid-ride when the JSONL
  stopped growing after discovery.)*
- **When a run-sheet and a tool's canonical findings doc disagree, the canonical doc wins — re-read it at
  execution time.** Run-sheets are written ahead and drift or simply err; the executing session must re-open
  the canonical doc for each tool it's about to run (the CLAUDE.md find-the-doc invariant, applied at the
  moment of use, not just at planning). *(2026-07-06: the run-sheet said start the sniffer "right after
  qdomyos connects"; `nrf-sniffer.md` says the sniffer must witness the `CONNECT_IND` — start BEFORE.
  Re-reading the doc pre-§2 caught it; obeying the run-sheet would have lost even the advert timelines.)*

### Time / patience savers

- **Pre-flashing + desk-derisking before the trip.** Sessions 2–3 were validated end-to-end against the
  Python harness (a WinRT CPS peripheral as the meter, a bleak central as the consumer) before the bike, so
  the only open question at the bike was "does the *real* SB20 accept it?". The harness proves the firmware;
  the bike proves the bike. *(decisions.md 2026-06-17, 2026-06-18; `code/scripts/BLE-LOOP.md`.)*
- **Never build or debug NEW tooling on the rider's clock — pre-build AND pre-*validate* it at the desk.**
  The bike is for the **on-air proof of already-validated software**, never the first run of new code. *(Session
  9: the FTMS workout driver was written live (background agent) and its first on-bike launch hit an async-
  wiring bug — a synchronous state-machine fed an async BLE transport, coroutine never awaited — and the
  build→bug→fix loop ate the rider's warm-up; they rightly called it off. Two takeaways: (1) validate a
  hardware-driving path hermetically against the in-process twin BEFORE the ride, and use a **low-watt warm-up
  segment as the live validation window** before any hard effort; (2) the driver's `try/finally` Reset-on-exit
  DID keep the bike safe through the crash — keep that pattern on anything that commands resistance.
  `decisions.md` 2026-06-26.)*
- **The WSL/USB operational gremlins are solved — use the runbook.** The first live ride lost ~25 min to a
  zombie capture holding the ANT+ stick ("Resource busy"), root-only USB perms after `usbipd attach`, and a
  self-kill footgun (`pkill -f` matching the controlling shell). All now encoded: `run_capture.sh`
  (release-stick + detached + retry, no self-kill), `os._exit`-on-setup-failure in the capture scripts, and
  a 60-second pre-flight in `wsl-capture-runbook.md`. A pre-flight + the launcher would have avoided
  essentially the whole mess. *(decisions.md 2026-06-14 "hard-won WSL/USB/process operations lesson".)*
- **ANT+ stick bring-up here = WSL + usbipd (verified steps + a permission gotcha).** ANT+ runs in WSL
  (Ubuntu-24.04); BLE is native Windows. Steps: `usbipd list` → the `0fcf:1008 ANTUSB2` is already
  bind-`Shared`, so `usbipd attach --wsl --busid <id>` (no admin once shared; reaches all WSL2 distros) → it
  appears in WSL `lsusb` and the udev rule `42-ant-usb-sticks.rules` (MODE 0666, vendor 0fcf) is present →
  env `python3 -m venv ~/sb20-ant-venv && ~/sb20-ant-venv/bin/pip install -e "/mnt/c/.../code[ble]"` (gets
  openant 1.3.4) → `02_capture_assioma.py --device-id 17039` (**use the Assioma's explicit ANT id — its
  BLE-name serial, `ASSIOMA17039L`; the Stages are `62144`/`4963`, so NEVER wildcard or you grab the wrong
  meter**). **⚠️ GOTCHA (Session 4): the capture dies `[Errno 13] Access denied` despite the udev rule** —
  WSL doesn't apply udev rules unless **systemd is enabled**, and the usbipd-attached device post-dates the
  last trigger. Fixes (pre-stage at the desk!): enable systemd (`/etc/wsl.conf` → `[boot] systemd=true`, then
  `wsl --shutdown` + reopen), **or** `sudo udevadm control --reload-rules && sudo udevadm trigger` after
  attach, **or** run under `sudo` — but **passwordless sudo was NOT configured**, so a mid-ride agent can't
  self-fix it. *(Session 4: stick attached + visible + openant installed live in ~3 min, but `[Errno 13]`
  blocked the live capture; the ESP `src_power_w` + the Garmin `.FIT` covered the Assioma instead. Bring the
  ANT+ path up AND verified — a libusb claim test — **before** the next ride.)*
- **The off-loop OLED fix is a measured win — don't regress it.** The OLED I2C render (~94 ms) was blocking
  the hot loop and causing the freeze. Moving the render to its own task cut **loop_max 96 → 12 ms** and
  **stalls (>50 ms) 161 → 0** over a loaded soak, heap flat (no leak). Render-on-change keeps it near-zero
  when idle. *(`code/findings/perf-results.md`, 2026-06-17 row 563c60b.)*
- **A health signal the rider can read at a glance.** The "slow LED blink (1 flash / 0.5 s) = connected and
  healthy" indicator lets the rider confirm board state without a terminal — owner explicitly asked for it
  during bench-flash. Cheap, high-value for a hands-busy rider. *(transcript 2026-06-16; `decisions.md`
  WiFi/boot-guard entries.)*

### What worked well (keep doing it)

- **The "agent runs the tooling, rider narrates, agent reads `/log` + captures live" model.** The rider
  says "battery out", "pairing now", "pressed LEFT-up" while the agent reads `/log` and the JSONL off the
  machine and confirms against the data. This is the working model precisely *because* interactive terminal
  input at the bike was unusable (`read failed 5: I/O error`, focus bouncing) — the agent drives from
  outside, the rider just narrates in chat. **Session 4 sharpened it: the agent also *launches* the capture
  commands itself (the radio is on the same machine) and tails the line-buffered JSONL live, so the rider's
  hands never leave the bars except for genuine hardware actions.** *(decisions.md 2026-06-14 §6,
  2026-06-18; BIKE-SESSION-2/3 framing; Session 4 §C.)*
- **Opportunistic bonus captures pay off.** The 2-minute "does the SB20 emit BLE on a shifter press?" probe
  at the end of session 2 discovered the SB20 **broadcasts gear state over BLE** (char `0c46be60`, one-hot
  gear bitmap) — now a headline future feature (emulate a Zwift Click). Budget a cheap stretch capture when
  the rig is already set up. *(decisions.md 2026-06-18; `SHIFTER-probe-20260618.jsonl`;
  `code/findings/shifter-ble-protocol.md`.)*
- **Front-loading the highest-information gate.** Proving power-acceptance first (session 2) meant
  everything after it was upside; a fail there would have ended the session cheaply rather than after an
  hour of protocol poking. *(decisions.md 2026-06-18.)*
- **The opportunistic passive-ride session.** When the rider is doing a real workout anyway (their own app,
  their own plan), a run-sheet of strictly-passive captures turns the ride into free protocol data at
  near-zero rider cost: pre-flight + wake-the-bike + timestamped narration ("pairing now", "stopping now",
  "a second rider's meter is on air") is all that's asked of them. Sequence the connection-order-sensitive
  pieces around *their* ride (uncontended GATT dump before their app connects; sniffer armed before they
  pair), and hold every physical step for the explicit at-the-bike cue. Even with the 2026-07-06 sniff
  missing its prize, the ride still banked the FTMS dump, the ANT inventory, a topology negative-result,
  and two env fixes — for ~3 minutes of rider attention. *(CAPTURE-qdomyos-sb20-passive.md.)*
- **The clean isolation proof (both batteries out).** See Capture discipline — it's also a "what worked":
  it converted "the SB20 shows power" into "the SB20 shows power *that can only be the Assioma*". *(decisions.md
  2026-06-18.)*

---

## The human-in-the-loop contract

- The rider's **time and patience are the budget.** Optimise for **maximum learning per minute and per
  unit of frustration** — not for covering the most steps.
- A session that discovers "the firmware was wrong" or "the tool doesn't run" **at the bike** is a process
  failure on the **agent's** side — that belongs to step 1, not the rider's time.
- Default to **fewer, denser, better-prepared sessions**, not more frequent ones. Every trip you can avoid
  by derisking or capturing-for-later at the desk is a win.


## Reason Bayesian-ly — state your certainty, and update your priors

*(Added after session 13, 2026-07-26, where the agent made four confident causal claims that the next
measurement contradicted. The measurements were sound every time; the interpretations were premature.
Owner: "Let's be Bayesian together. We gather evidence to improve our confidence." and "Explicitly
reason over your certainty/uncertainty. Where you have priors, review them and update.")*

**The rule: never state a cause as fact. State it as a hypothesis with a confidence and a test.**

Before asserting *why* something happened, say:
1. **The claim**, and **how confident** you are — and why that number. n=1 correlation is not a cause.
2. **The competing hypotheses** you have not excluded. Name them explicitly, including the boring ones
   (nobody was pedalling; the process didn't restart; the instrument is lying).
3. **What would falsify it** — and if that test is cheap, *run it before asserting anything*.

**Priors worth holding (learned the hard way):**

- **A shipped device obeying its spec is more likely than your hasty decode being right.** Session 13:
  the agent claimed the SB20's FTMS flags contradicted its payload. They didn't — the bike was sending
  a *degraded frame set*, honestly flagged. Prefer "my decode is wrong" until proven otherwise.
- **The rider holds priors you cannot observe** — their own rig, habits, and history. Session 13: the
  agent asserted the watch and the C3 were competing for the pedals; they use **different radios**
  (ANT+ vs BLE) and the owner already knew this. **Surface hypotheses to the rider early**; they will
  often kill a bad one in one line.
- **Validate the instrument before interpreting it.** Session 13's phantom "double-fire" was a log that
  prints every line twice. Also: orphaned processes competing for a radio, and a capture lost to the
  wrong venv. **Check what you are measuring with before you trust what it says.**
- **Correlation observed once, during a session where three other things changed, is close to
  worthless.** Change one variable at a time, or say plainly that you cannot attribute the result.

**When you are wrong, say so plainly and immediately**, name what the evidence actually shows, and
update the record (the session doc, the issue, `decisions.md`). A wrong claim left standing in an
issue costs the next reader more than the original error cost you.
