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
- **Batch by physical setup.** Group everything that needs the same rig state (a battery pulled, a given
  pairing) so the rig changes as few times as possible. Each battery swap / re-pair is a patience tax.
- **Write each step as Plan**: *goal · exact action · expected result (explicit pass/fail) · what to
  capture.* Mark **must-do vs optional/stretch**, and give an **honest time budget up front** ("~45 min,
  3 must-dos").
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

## 2 · Execute (live, you driving)

- **Feed one step; wait for the narrated result; confirm against the data** (`curl` / `/log` / the capture
  file) **before advancing.** Never assume a step passed because it "should have." *(In session 2 the
  SB20's interactive protocol — control-point writes, the proprietary `fe02` token, disconnect reasons —
  was invisible to any sniffer; the **only** thing that saw it was our own spoofed crank logging to
  `/log`. The diagnostic endpoint is how you confirm, not the rider's read of the app UI alone.)*
- **State the expected result *before* they act** — "you should see `source:connected` and ~200 W; tell me
  what you see." Make pass/fail something the human **observes**, not **interprets**.
- **Record actuals inline as you go** — `✅`/`❌`/`⚠️` + the observed bytes/values/`/log` lines, written
  into the session doc, not just chat (the Plan/Actual rule). Memory is lossy; the rider is busy.
- **Timestamp every step (wall-clock, in the doc).** Note the local time (`HH:MM`) when each section and
  key step *starts*, beside its result — so **actual** durations are recorded, not reconstructed from
  memory. The agent has been an unreliable time estimator (bike *and* desk work); logged actuals are the
  only thing that fixes that. One clock-read per step turns every session into calibration data for the
  planned-vs-actual review (§4). Get a timestamp at the start of the session and at each section boundary.
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

### Flashing & OTA (the recurring time-sink)

- **Weak-signal OTA drops below ~−72 dBm on the C3.** OTA to the board gets unreliable in the drop zone;
  the fix is move the board nearer the AP and retry. This recurred enough to be baked into `flash.ps1`
  (RSSI pre-flight + auto-retry) and `firmware/BENCH-FLASH.md`. *(decisions.md 2026-06-17; `flash.ps1`
  lines 40–59. Conversely, a desk OTA at −73 dBm **succeeded** — the threshold is real but close, so
  check, don't assume.)*
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

### BLE pairing & the SB20's quirks

- **Duplicate `Stages 62144` advertiser.** The real L crank **and** the ESP both advertise the name
  "Stages 62144", so the SB20 (or a capture tool, or a sibling board) can grab the wrong one. For spoof
  tests, **pull the real L-crank battery** so the ESP is the only `62144`; for tools, disambiguate by
  `--address`. *(decisions.md 2026-06-17, 2026-06-18; BIKE-SESSION-2/3 setup step.)*
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

### Time / patience savers

- **Pre-flashing + desk-derisking before the trip.** Sessions 2–3 were validated end-to-end against the
  Python harness (a WinRT CPS peripheral as the meter, a bleak central as the consumer) before the bike, so
  the only open question at the bike was "does the *real* SB20 accept it?". The harness proves the firmware;
  the bike proves the bike. *(decisions.md 2026-06-17, 2026-06-18; `code/scripts/BLE-LOOP.md`.)*
- **The WSL/USB operational gremlins are solved — use the runbook.** The first live ride lost ~25 min to a
  zombie capture holding the ANT+ stick ("Resource busy"), root-only USB perms after `usbipd attach`, and a
  self-kill footgun (`pkill -f` matching the controlling shell). All now encoded: `run_capture.sh`
  (release-stick + detached + retry, no self-kill), `os._exit`-on-setup-failure in the capture scripts, and
  a 60-second pre-flight in `wsl-capture-runbook.md`. A pre-flight + the launcher would have avoided
  essentially the whole mess. *(decisions.md 2026-06-14 "hard-won WSL/USB/process operations lesson".)*
- **The off-loop OLED fix is a measured win — don't regress it.** The OLED I2C render (~94 ms) was blocking
  the hot loop and causing the freeze. Moving the render to its own task cut **loop_max 96 → 12 ms** and
  **stalls (>50 ms) 161 → 0** over a loaded soak, heap flat (no leak). Render-on-change keeps it near-zero
  when idle. *(`code/findings/perf-results.md`, 2026-06-17 row 563c60b.)*
- **A health signal the rider can read at a glance.** The "slow LED blink (1 flash / 0.5 s) = connected and
  healthy" indicator lets the rider confirm board state without a terminal — owner explicitly asked for it
  during bench-flash. Cheap, high-value for a hands-busy rider. *(transcript 2026-06-16; `decisions.md`
  WiFi/boot-guard entries.)*

### What worked well (keep doing it)

- **The "rider narrates, agent reads `/log` + captures live" model.** The rider says "battery out",
  "pairing now", "pressed LEFT-up" while the agent reads `/log` and the JSONL off the machine and confirms
  against the data. This is the working model precisely *because* interactive terminal input at the bike was
  unusable (`read failed 5: I/O error`, focus bouncing) — the agent drives from outside, the rider just
  narrates in chat. *(decisions.md 2026-06-14 §6, 2026-06-18; BIKE-SESSION-2/3 framing.)*
- **Opportunistic bonus captures pay off.** The 2-minute "does the SB20 emit BLE on a shifter press?" probe
  at the end of session 2 discovered the SB20 **broadcasts gear state over BLE** (char `0c46be60`, one-hot
  gear bitmap) — now a headline future feature (emulate a Zwift Click). Budget a cheap stretch capture when
  the rig is already set up. *(decisions.md 2026-06-18; `SHIFTER-probe-20260618.jsonl`;
  `code/findings/shifter-ble-protocol.md`.)*
- **Front-loading the highest-information gate.** Proving power-acceptance first (session 2) meant
  everything after it was upside; a fail there would have ended the session cheaply rather than after an
  hour of protocol poking. *(decisions.md 2026-06-18.)*
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
