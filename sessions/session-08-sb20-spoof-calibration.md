# 🚴 Session 8 — SB20 spoof calibration handshake (G1 + G2)

**Status: ✅ DONE (2026-06-25) — on-bike day 2.**
**Outcome:** both goals met. **G1** captured the real Stages crank's `0x10` Enhanced-Offset reply (`20 10 01 00 00 ba 01 04 85 03 b7 03` → Manufacturer **Company ID 442** + mfg-data `04 85 03 b7 03` that encodes the L/R offsets 901/951). **G2** — the Stages app's calibrate **now COMPLETES** against our spoof (the `0x0000` placeholder company-id was the cause of the spin); confirmed **live** with the byte-faithful 442+mfg-data fix, flashed from the pre-lockdown base `8494935`. Bonus findings: own-unique-ID spoof (`62145`) pairs **only if the configured right crank id is findable** (phantom R fails) and then shows **no double-count**. · tracked in [`sessions/README.md`](README.md). Run via [`PLAYBOOK.md`](PLAYBOOK.md)
(record actuals inline, **⏱ timestamp from the start**, retro at the end). Carries forward the **G1/G2**
items deferred from [session 4](session-04-enhanced-offset-and-brake-levers.md) (the board's WiFi/OTA
was down that day — it isn't now).

**Goal:** close the last gap in the SB20 spoof — the **Stages app's zero-reset / calibrate**. Our
spoof answers the Cycling Power control point, but the app's *Enhanced* Offset Compensation (`0x10`)
reply carries a manufacturer company-id we've never captured (`Config::SPOOF_MFG_COMPANY_ID` is a
flagged `0x0000` placeholder). **G1** captures the *real* crank's `0x10` reply to ground it; **G2**
tests whether our spoof's calibrate now **completes** in the app (it spun before — `decisions.md`
session 3/4). This is independent of the corrector ride (session 5).

**~25–35 min** (G1 ~10 · G2 ~10 · iterate buffer ~10). G1 **first** — it grounds the fix G2 verifies.

## What's already done (desk — verify, don't redo)
- **The proxy board is flashed with current firmware** (COM9 / `sb20proxy.local` / `192.168.1.165`),
  which already emits the **spec-correct Enhanced `0x10`** reply (`encodeEnhancedOffsetCompResponse`,
  `Config::SPOOF_MFG_COMPANY_ID` — placeholder until G1). So **G2 can run immediately**; a reflash is
  only needed if G1 yields new bytes to plug in.
- Board health: `python code/scripts/route_smoke.py --ip sb20proxy.local --no-post` → all routes PASS.

## Bring / set up
- The **SB20** + the **real Stages L crank `62144`** + a **fresh CR2032** (the real crank has read
  12–14% low on an old cell; G1 needs it awake, and a flat cell can corrupt the offset).
- A **phone with the Stages app**. The **proxy board** powered near the bike (on WiFi).
- Coin/small screwdriver for the crank battery door.

## Pre-flight (desk, ~2 min — the gate)
> **Power-cycle the board first** (unplug/replug, or it happens when you move it to the bike). Heap drops
> over long uptime — a board up ~20 h showed **~28 KB free**, which made the (session-5-only, **unused
> here**) `/calibrate` route time out in `route_smoke`; a power-cycle restores ~125 KB and clears it.
> `route_smoke` should then show all PASS; for session 8 only `/status` · `/setup` · `/diag` · `/log` matter.
```bash
curl http://sb20proxy.local/status      # 200 + low "ms" (fresh boot), source:searching
```
Open a rolling `/log` (the live instrument) in a second terminal:
```bash
while true; do curl -s http://sb20proxy.local/log; sleep 3; done
```

## ⏱ Actuals — live log (2026-06-24, times NZ/AEST)
- **06:14 — Session start.** Synced `main`→`origin/main` `79a964d` (99 commits, overnight docs); working branch `session/08-sb20-spoof-cal`. Rider has the **SB20** set up.
- **06:14 — Pre-flight GATE ✅ GREEN.** `/status` 200, `source:searching`, heap ~125 KB, uptime ~5 min, **rssi −72** (borderline for OTA only; HTTP/captures fine). `route_smoke --no-post` → all routes PASS (`/`, `/calibrate`, `/setup`, `/diag`).
- **06:15 — G1 tooling derisked off-rider.** No `code/.venv` existed; system Py **3.14** had no `bleak`. Created `code/.venv`, installed **bleak 3.0.2** (winrt cp314 wheels OK). Verified the capture script's BLE API matches 3.0.2 (`client.services` property, `start_notify` *before* `write_gatt_char(response=True)`, scanner props present); CP op `enhanced-offset-compensation`=`0x10`, char `0x2A66`, reply expected `20 10 01 …`. Scan smoke-test: radio works; SB20 visible at `E4:AA:5A:D6:0E:D4`.
- **06:15 — ⚠️ Duplicate `Stages 62144` live (known gotcha).** With ESP on, **two** advertisers on air: `38:44:BE:45:E9:A6` and `E0:72:A1:70:02:7E`. Historical real-crank addr `E8:CF:D8:D9:3A:20` (2026-06-15) no longer matches → **BLE addresses rotate** → for G1, disambiguate by **power-state** (ESP off ⇒ the surviving `Stages 62144` is the real crank) and **capture by name**.
- **~06:25 — ⤴ STRATEGY PIVOT (owner).** Try the **own-unique-ID** approach instead of the byte-faithful `62144`-with-battery-pull: put the ESP on its own Stages id (`62145`), re-point the SB20 to it in the Stages app, **leave the real cranks powered (no battery pull).** Confirmed **firmware-supported at RUNTIME, no reflash:** `/setup` → "Crank identity" → `spoof_name`/`spoof_serial` → NVS → drives `NimBLEDevice::init` + advert name + DIS serial ([BleCrankPeripheral.cpp:126](../firmware/src/ble/BleCrankPeripheral.cpp), [main.cpp:185-187](../firmware/src/main.cpp); `POST /setup/save` [WifiLink.cpp:277](../firmware/src/net/WifiLink.cpp)). This is the **2026-06-16 Phase-1B plan** (decisions.md ~L803). Side effect: G1 (capture real crank `0x10`) becomes a **free bonus** — the real crank stays awake, no pull needed.
- **~06:25 — Open Q (owner):** change **L-only** (leave R=`4963` real) or **both L&R**? Stages sums L+R as independent meters → L-only-with-real-R risks **double-count**; clean sole-source likely needs R→phantom id (`4964`) or a single-sided/left-only option. **DECISION (owner): change BOTH** — L→`62145` (ESP), R→phantom `4964` so the bike runs left-only off the ESP and ignores the still-powered real right crank (no battery pull).
- **~06:32 — ESP identity set → `Stages 62145` (RUNTIME, no reflash).** `POST /setup/save` `name=ASSIOMA&single=1&spoof_name=Stages 62145&spoof_serial=11821518` → 200 Saved → reboot ~2s. `/diag` confirms `spoof_name=Stages 62145`; source=ASSIOMA / single / serial all preserved; `source=searching`. **On-air scan confirms the radio advertises it:**
  - `38:44:BE:45:E9:A6` **`Stages 62145`** rssi −59 ← our **ESP** (strong/near)
  - `E0:72:A1:70:02:7E` `Stages 62144` rssi −82 ← the **real L crank** (now disambiguated — the surviving 62144)
  - `E4:AA:5A:D6:0E:D4` `Stages Bike 0105` rssi −94 ← the SB20 (far from this PC's BT; irrelevant to the bike↔crank link)
  - real R `4963`: asleep · phantom `4964`: absent → **safe to use as phantom R**.
- **🔧 ESP RESTORE (end of session):** `POST /setup/save` `name=ASSIOMA&single=1&spoof_name=Stages 62144&spoof_serial=11821518` (back to pre-session identity). App restore: **L=`62144`, R=`4963`**, crank length **165 mm**, ANT+ offset **L 903 / R 951**.
- **~06:36 — App pairing-screen recon (owner) — durable finding.** Stages app crank pairing = **two separate free-TYPE id fields (Left / Right)**; you **type the number**, NOT pick from a scan list; **no single-sided / left-only option**. ⇒ ids aren't validated against a present device at entry, so L=`62145` / R=`4964` should be accepted as typed; the bike then attempts to connect to those ids. (Promote to decisions.md at close-out.)
- **~06:40 — Protocol clarified (owner) + agent course-correction.** The agent jumped to directing the in-app pairing while the rider was **still upstairs** (inferred readiness from conversational momentum). **Owner rule** (captured to [`PLAYBOOK.md`](PLAYBOOK.md) §2 + memory `rider-explicit-at-bike`): *the rider is ALWAYS explicit about being at the bike; never assume — physical steps wait for the explicit cue; prep requests before heading down are desk-only.* Stopped the rolling `/log` poller. **ESP staging (`Stages 62145`) persists in NVS — ready for when the rider is at the bike.**

## ⏸ Paused 2026-06-24 ~06:45 — out of bike time; RESUME HERE
No bike time today (rider ran out of time before going downstairs). **All desk staging is done and persists — next visit is turnkey:**
- **ESP staged as `Stages 62145`** (NVS-persisted; advertising, verified −59 dBm). Real cranks untouched & powered.
- **BLE tooling ready:** `code/.venv` with **bleak 3.0.2** (Py 3.14, winrt cp314 wheels); `06_capture_ble.py` API-verified against 3.0.2. Battery-read helper staged at `%TEMP%\batt_read.py` (re-create if cleared).
- **App recon (durable):** Stages crank pairing = two **free-type** L/R id fields · **no** scan-list · **no** single-sided option.
- **The agreed plan:** rider AT BIKE → app **L=`62145`, R=`4964`** (phantom) → agent watches `/log` for the SB20 connecting to the ESP → rider pedals (wakes Assioma) → compare SB20 watts vs Assioma for **double-count** → then test the app **calibrate/zero-reset completes** (old G2 payoff) + grab the real crank's `0x10` as a free bonus (G1 — crank stays awake, no pull).
- **Tonight's normal ride:** leaving the ESP on `62145` is **safe** (real `62144` is then the only `62144` on air → SB20 pairs to it cleanly). **Do NOT restore the ESP to `62144` for a normal ride** — that recreates the duplicate-`62144` collision. Zero-noise option: power the ESP off.
- **ESP restore (only when the experiment is fully done):** `POST /setup/save` `name=ASSIOMA&single=1&spoof_name=Stages 62144&spoof_serial=11821518`; app back to **L=`62144` / R=`4963`**.

> ### ▶ READ THIS — which plan to follow
> **PRIMARY = the staged own-unique-ID plan (RESUME HERE, above): ESP stays on `62145`, app → L=`62145` /
> R=`4964` (phantom), NO battery pull.** Because the ESP is `62145`, the **real crank is the only `62144`
> on air** — so **G1 needs no ESP power-off** and **G2 needs no battery pull**. The per-step commands +
> pass-criteria below still apply; treat the *"power the ESP off"* / *"pull the L-crank battery"* lines as
> the **FALLBACK**, used only if the own-ID approach doesn't connect/behave on the bike (then you're back to
> the byte-faithful-`62144` + battery-pull route). Don't pull the battery unless the pivot fails.

## ⏱ Actuals — 2026-06-25 (bike day 2, resume; times AEST)
- **06:16 — Desk prep / resume (off rider's clock).** Synced `main`→`origin/main` **`dcbcacf`** (was `8494935`; +21 commits — the OTA/security-lockdown work, **deliberately post-session-8**, doesn't touch the spoof's `0x10`). Fresh branch `session/08-onbike-20260625`.
- **06:16 — Board health re-verified ✅.** `/status`: `source:searching`, **heap ~125 KB**, **uptime ~5 min**, **rssi −66/−68**. ⚠️ Briefed as "powered ~20h / heap low," but the board is already at ~5 min uptime + healthy heap → it self-rebooted or was already power-cycled; either way it's in the fresh-boot state the gate wants. `route_smoke --no-post` → **all routes PASS** (`/status`,`/`,`/calibrate`,`/setup`,`/diag`).
- **06:16 — Spoof identity intact through the reboot ✅.** `/diag`: `spoof_name=Stages 62145`, `spoof_serial=11821518`, `source_name_filter=ASSIOMA`, `single_sided_double=yes`, `source=searching`. NVS held the staging.
- **06:16 — BLE tooling ready ✅.** `code/.venv` present; **bleak 3.0.2** imports clean. `06_capture_ble.py` API-verified (prior session).
- **06:16 — Awaiting rider's explicit "I'm at the bike"** before any physical/in-app step (PLAYBOOK §2 / memory `rider-explicit-at-bike`). Binding pre-flight (RSSI in final position) to be re-confirmed on arrival — the C3 picks its AP at boot and doesn't roam.
- **06:47 — Rider AT BIKE. Pre-flight gate ✅ GREEN (final position).** Board power-cycled on the move to the bike (first `/status` caught it **mid-boot** → one transient "unreachable", then fine). Re-polled `/status` + `/diag`: `source:searching`, **rssi −61/−62**, heap ~126 KB, fresh boot, `spoof_name=Stages 62145` / source `ASSIOMA` / single-sided all intact. Validates the C3-doesn't-roam lesson — re-confirm in final position is mandatory. → proceeding to in-app pairing.
- **~07:00 — App pre-change ids confirmed L=`62144` / R=`4963`** (matches restore values). Then set **L=`62145`, R=`4964`** (the staged phantom-R plan) → app: **❌ "pairing failed"**, persisted across an app restart. Diagnosis (BLE scan + `/log`): our ESP `Stages 62145` advertising fine (−62, discoverable); real **right** `Stages 4963` awake (−69); phantom **`4964` absent**; real left `62144` asleep. `/log` showed the ESP's *source* side connected to the Assioma but **NO peripheral connection from the SB20** → the bike aborted before reaching the left crank. ⇒ **phantom/absent right id blocks pairing.**
- **07:04 — Gate 1 ✅ PASS (board-confirmed): the SB20 connects to our ESP at `62145`.** Changed only R→`4963` (real right, on air) keeping **L=`62145`** → app: **"connected"**. `/log`: `[srv] connect from e4:aa:5a:d6:0e:d4` (the SB20, `Stages Bike 0105`) + `[prop fe02] write bfda1853` (its proprietary token — **identical to sessions 2-3**). `/status` source:connected ASSIOMA17039L, forwarded 752, rssi −61.
  - **⭐ Durable finding (promote):** the SB20 **accepts our ESP under a NEW unique Stages id (`62145`)**, not only the byte-faithful `62144` — the own-unique-ID spoof works for the LEFT. **BUT** it **requires a *findable* right crank** to complete pairing; a phantom/absent right id (`4964`) → "pairing failed". ⇒ the **phantom-R sole-source approach is refuted**; the real right crank must be present and powered ⇒ a **double-count** is then in play (the ESP doubles single-sided L *and* the real right contributes). Double-count is irrelevant to the G2 calibrate-handshake test, so we proceed; sole-source-without-double-count is a separate problem (can't just drop the right — that breaks pairing).
- **07:08 — Gate 2 ✅ power+cadence flow; NO meaningful double-count (better than the plan feared).** Pedaling steady: ESP reads Assioma `src_pw ~98-110 W` → outputs `out_pw ~196-220 W` (**exactly 2×** — single-sided doubling); cadence **~80-82 rpm**, `src_cad`=`out_cad`; balance ~46-49; `forwarded` incrementing; link solid. **SB20 display = `203 W` ≈ the ESP's doubled output alone** → the bike is **not** summing the real right crank.
  - **⭐ Mechanism (BLE scan @07:10):** real right `Stages 4963` **still advertising (−71) → the bike never connected to it**; it only required the id to be *findable* to validate pairing. Our ESP `62145` is **absent** from the scan = connected to the SB20. ⇒ **own-id spoof + a findable right id ⇒ the bike pairs and uses ONLY the ESP's doubled-left as total; no double-count in practice.** (Open Q, untested: would a *non-real* findable right id also satisfy the pairing, or must a real device answer?)
  - **Bonus for G1:** the real LEFT crank `Stages 62144` woke from pedaling and is on air — addr **`E8:CF:D8:D9:3A:20`** (the 2026-06-15 address recurred; rotating-but-recurring), **rssi −81** (weak-ish). With our ESP on `62145`, it's the only `62144` on air ⇒ **G1 capture is now possible by name, no collision, no ESP power-off.** Keep it awake (intermittent pedaling).
- **~07:13 — Gate 3 / G2 ❌ STILL SPINS (placeholder company-id `0x0000` insufficient).** Rider tapped the LEFT-crank (`62145`) zero-reset. `/log`: **2×** `[cp] write 10` then **stops** (NOT a retry-storm — contrast session 2's endless re-send). Our ESP auto-replies (firmware logs only the inbound write — `BleCrankPeripheral.cpp:27`) with the spec-correct **7-byte** Enhanced shape `20 10 01 00 00 00 00` = offset 0 + **company-id `0x0000`** (`Config::SPOOF_MFG_COMPANY_ID`, the flagged placeholder). **App spins ~1 min — no completion, no error.** ⇒ the spec-correct *structure* is necessary but **NOT sufficient**: the app validates the **manufacturer company-id** and rejects `0x0000`. **This grounds the need for G1**, exactly as the doc anticipated. (Owner asked live "are we replying with a correct Stages company-id?" — no, `0x0000`; that's the gap.)
- **07:14:40–55 — G1 attempt 1 ❌ (real LEFT crank `62144` had fallen asleep).** `06_capture_ble.py --name "Stages 62144"` (capture `G-crank62144-ble-enhanced-0x10-20260625-0712.jsonl`): 15 s scan saw only `Stages 4963` (right, −69, +proprietary `d445fe01`) and `Stages Bike 0105` (−49) → "no matching device found." The left slept in the ~4 min since it woke. **Retry after re-waking it.** Note: capturing `0x10` performs a zero-offset on whichever crank we hit, so target the LEFT `62144` (already the planned disturb-and-restore target) rather than the right `4963` (meant to be left alone) — even though the company-id itself is manufacturer-level.
- **07:19 — G1 ✅✅ SUCCESS — real crank `0x10` captured (the missing byte).** Re-woke the left crank (rider spun it); `06_capture_ble.py --name "Stages 62144"` → connected to `E8:CF:D8:D9:3A:20`. DIS reads confirm it's the real L crank: mfg **"Stages Cycling"**, model **SPM2**, serial **11821518**, fw **1.8.2**. Capture: **`code/findings/captures/G-crank62144-ble-enhanced-0x10-20260625-0716.jsonl`**. The Enhanced Offset Compensation (`0x10`) indication:
  - **`raw_hex = 2010010000ba01048503b703`** → `20 10 01` (response / req-0x10 / success) · offset **`00 00` = 0** (matches the known BLE offset 0, `200c010000`) · **Manufacturer Company ID `ba 01` = `0x01BA` = 442** · manufacturer-specific data **`04 85 03 b7 03`** (5 bytes).
  - **⭐⭐ THE GROUNDING (promote to decisions.md):** `Config::SPOOF_MFG_COMPANY_ID` must be **442 (`0x01BA`)** — not the `0x0000` placeholder — and the real reply carries **5 bytes of mfg-specific data** our current 7-byte reply omits. **442 is corroborated** as the Stages company-id by the SB20's own advertisement manufacturer-data key `442` (capture lines 5/6/10). ⇒ to be byte-faithful the spoof's `0x10` reply should be **`20 10 01 00 00 ba 01 04 85 03 b7 03`** (12 bytes).
  - **Byte-faithful surface re-confirmed (free):** CP Feature **`0x0008030B`** (`0b030800`), Sensor Location `0x00`, CPS measurement flags **`0x002F`**, proprietary service **`d445fe01`** (+ chars `fe02`/`fe03`) and Nordic DFU `fe59` present. **Crank battery 22%** (low — fresh CR2032 at restore).
  - **⚠️ Tooling bug to fix at desk:** `06_capture_ble.py`'s enhanced-`0x10` parser reported `"offset": 951` (it misreads the trailing `b7 03`); the spec-correct offset is the `00 00` at bytes [3..4]. Fix the parser + add a golden-vector test from this capture.
  - ⇒ **G2's spin is fully explained and the fix is grounded.** Final G2 confirmation (does the app's calibrate *complete* once we reply with company-id 442 (+ mfg data)?) requires a reflash — decision recorded below.
- **~07:25 — Decision (owner): LIVE reflash + G2 retry, WITHOUT pulling the deferred 2026-06-24 security lockdown.** Verified the lockdown's reach in `WifiLink.cpp`: CSRF/same-origin guard on state-changing routes (would **403 our restore `POST /setup/save`**), `/log` behind an on/off toggle, espota fail-closed. ⇒ build the live binary from the **pre-lockdown base `8494935`** (last commit before any `security/*` PR; security PRs #125–#130 all landed after it on 2026-06-24) + ONLY the `0x10` fix, so runtime behaviour matches the running 2026-06-23 board.
  - **Fix (on branch `session/08-onbike-20260625`):** `Config::SPOOF_MFG_COMPANY_ID = 0x01BA` (442) + `SPOOF_MFG_DATA = {04 85 03 b7 03}`, wired through `handleControlPoint` at the `BleCrankPeripheral.cpp` call site; golden host test `test_cp_offset_comp_enhanced_0x10_real_crank` asserts the exact 12-byte reply `20 10 01 00 00 ba 01 04 85 03 b7 03`. (Applied identically in the build worktree `C:\repos\cauldnz\SB20-fix-build` @ `8494935`.)
  - **⚠️ TOOLING BLOCKER (RETRO ITEM):** PlatformIO is **not installed** on the bike machine — the **Python 3.14 upgrade orphaned it** (`pio`/`platformio` absent everywhere searched; `~/.platformio` empty → no toolchain cache). The cold-start (`BIKE-SESSION-READY.md` §2b) still prescribes `python -m platformio run` — **stale**; the firmware build path was never re-verified after the Py-3.14 upgrade. A "tool not ready at the bike" process miss (PLAYBOOK §1) that cost rider time. **Owner chose to stand up PlatformIO now and push through** (~20–40 min toolchain bring-up). Installed PlatformIO **Core 6.1.19** into dedicated venv `C:\repos\cauldnz\pio-venv` (works on Py 3.14 ✅); building `esp32c3-oled-live` in the worktree. **Desk action:** re-provision + verify PlatformIO and **fix the cold-start flash command**.

### 🗣️ Live retro feedback (owner, captured during the session — FOLD INTO PLAYBOOK.md at close-out)
> The session was painful for the rider. Two root-cause misses, both on the planning/agent side:
1. **A predictable failure wasn't flagged at design/plan time.** The placeholder manufacturer company-id (`SPOOF_MFG_COMPANY_ID = 0x0000`) in the `0x10` reply was a *known unknown* — G2 spinning on it was foreseeable. It should have been called out in planning that G2 was essentially **predetermined to fail until G1 grounded the company-id**, so G1 must run strictly first and G2 be reframed as a *post-G1-reflash confirmation*, not a standalone test. Instead a calibrate-spin cycle was spent confirming the predictable. **Fix:** at plan time, audit every flagged placeholder / unknown value in the spoof surface and order the gates so the **grounding capture precedes any test that depends on it**.
2. **Environment wasn't verified or reproducible across machines.** PlatformIO was missing because work moves between the **desk and the bike laptop** (different machines/sessions) and the build toolchain was never checked or defined. **Fix:** define the dev/bike environment in **committed config** (pinned requirements/lock + a provisioning script for PlatformIO + the BLE venv) and add a **build-toolchain check to the bike pre-flight** (verify `pio` runs AND a cached ESP32 toolchain exists) so "can we build a firmware?" is answered at the desk, never discovered at the bike.
> _(more feedback to follow — owner)_

- **07:58 — Gate 3 / G2 ✅✅ PASS — the Stages app's calibrate COMPLETES.** OTA-flashed the pre-lockdown+fix build; SB20 re-paired (`[srv] connect from e4:aa:5a:d6:0e:d4` + `[prop fe02] write bfda1853`); `spoof_name=Stages 62145` preserved through the OTA. Rider re-ran the app calibrate → **"Perfect. Calibrated 901/951"** — **COMPLETED, no spin.** `/log`: a **single** `[cp] write 10` (vs the pre-fix spin), answered by our byte-faithful reply, then `[srv] disconnect reason=531` + immediate clean reconnect (the calibrate procedure cycles the link; re-advertise-on-disconnect recovers it). Evidence: `code/findings/captures/G2-calibrate-pass-log-20260625-0758.txt`.
  - **⭐⭐⭐ "901/951" IS OUR REPLAYED BYTES — byte-perfect confirmation.** Those offsets live inside our mfg-data `04 `**`85 03`**` `**`b7 03`**: `85 03` LE = `0x0385` = **901**, `b7 03` LE = `0x03B7` = **951**. ⇒ the Stages crank's `0x10` Enhanced reply encodes the **L/R zero-offsets in the manufacturer-specific data** (shape `[0x04, Loffset_LE, Roffset_LE]`), with the standard Offset field = `0`. We captured the real crank's bytes (G1) and replayed them verbatim, so the app reads `901/951` straight back. **The blocker was the placeholder company-id `0x0000` — replying with the real `442` + this mfg-data makes the app complete.** (901 ≈ the noted real-L 903 — a fresh zero; 951 = real-R exactly.)
  - **⚠️ Caveat (static replay):** we replay the *captured* mfg-data verbatim, so the app will show **901/951 on every calibrate regardless of live state** — cosmetic/frozen (fine for completing the handshake; the Assioma is the real calibrated meter). Note for the product.
  - **Build/flash that delivered it:** `esp32c3-oled-live` from worktree @ `8494935` (pre-lockdown) + the fix, PlatformIO **6.1.19** in `C:\repos\cauldnz\pio-venv` (build ~4:45). OTA via `espota.py` **needed explicit `-I 192.168.1.223` (host IP)** — PlatformIO's espota auto-picked `0.0.0.0` on this multi-NIC laptop (WiFi + WSL/Hyper-V vEthernet + link-local) → "No response from device"; explicit host IP fixed it. **Retro/tooling note:** the flash helper / cold-start should pin the host IP for OTA on multi-NIC machines.
- **~08:10 — Known-gap corroborated (owner, for the notes): crank-length set/display does NOT work against the ESP spoof.** Paired to the ESP (`62145`), setting crank length in the Stages app **didn't save** — it fell back to showing **"--"** (no value). `/log` showed **no standard CP `0x04` (set) or `0x05` (request) write** to our ESP during the attempt — yet our firmware *does* handle both (`handleControlPoint`; a `0x05` request replies `20 05 59 01` = 172.5 mm). ⇒ the Stages app uses a **non-standard path for crank length** that our spoof doesn't implement → it can't read a value back → "--". **Corroborates session 3's "A2 crank-length ⚠️ — app bypasses standard CP."** Does **not** affect G2 (calibrate completes) or power (the Assioma supplies watts; the SB20 doesn't need our crank length to compute power). **Backlog:** capture what the app actually writes for crank length (likely the proprietary `fe02`/`d445fe02` char) and implement it for a fully-faithful spoof.

## G1 · Capture the REAL crank's 0x10 Enhanced-Offset reply ⭐ (do FIRST)
Writes `0x10` to the **real** crank and logs its indication — the bytes that ground
`SPOOF_MFG_COMPANY_ID` (+ any manufacturer data). With the ESP on `62145`, just capture by name
`Stages 62144` — the only `62144` on air is the real crank (no ESP power-off needed; that's the fallback).
1. *(FALLBACK only)* if disambiguation fails, **power the ESP spoof OFF** so the only `Stages 62144` is the
   **real** crank. Either way: **leave the real L-crank battery IN** (fresh cell), keep the cranks **still**.
2. Run (native-Windows venv):
   ```bash
   code/.venv/Scripts/python.exe code/scripts/06_capture_ble.py \
     --name "Stages 62144" --duration 120 \
     --control-point enhanced-offset-compensation \
     --output code/findings/captures/G-crank62144-ble-enhanced-0x10-$(date +%Y%m%d-%H%M).jsonl
   ```
   *(If it grabs the wrong device, target the real crank by `--address <addr>` — its address is in
   `findings/captures/G-crank62144-ble-zero-20260615-070353.jsonl`.)*

**✅ Pass:** the JSONL has a `ble_cp_indication` whose `raw_hex` starts `2010 01…` — the real Enhanced
reply. **Commit the JSONL**, tell me the bytes; I ground `SPOOF_MFG_COMPANY_ID` (+ mfgData) in them,
update the golden test, and we reflash for G2. **If the write is rejected / needs bonding:** note it —
the spec-structure reply we already ship may satisfy the app on its own (test in G2 regardless).

## G2 · Re-test our spoof's 0x10 — does the app's calibrate complete? (the payoff)
1. **PRIMARY (own-ID, no battery pull):** in the Stages app set the crank ids to **L=`62145`** (the ESP) +
   **R=`4964`** (phantom). Watch `/log` for the SB20 connecting to the ESP; pedal a few strokes (wake the
   Assioma) → confirm power + cadence, and sanity-check the watts aren't double-counted.
   *(FALLBACK: if the SB20 won't pair to `62145`, restore the ESP to `62144` via `/setup/save` and **pull
   the real L-crank battery** so the only `62144` is the ESP, then pair as before.)*
2. Stages app → **calibrate / zero-reset.** Watch `/log` for `[cp] write 10` then our reply.

**✅ Pass:** the calibrate UI **COMPLETES** (no longer spins), the link holds, `/log` shows our
`20 10 01 …` Enhanced reply. → promote to `decisions.md` (the A1 zero-reset is GROUNDED). **❌ Still
spins:** paste `/log` + what we tried; if G1 captured real bytes, I plug them into `Config` + reflash
(`flash_c3.py --env esp32c3-oled-live --port COM9`, ~3 min) and we retry — that's the iterate loop.

## ✅ Pass / record
- G1: real `0x10` bytes captured + committed (or "write rejected / needs bonding" noted).
- G2: the Stages app calibrate **completes** against our spoof (or the exact failure + `/log`).
- → `decisions.md`: the grounded `SPOOF_MFG_COMPANY_ID` and whether the app accepts our zero-reset.

## Notes / scope
- **§D (ANT+ power-topology grid)** from session 4 is **out of scope** here unless the ANT+ stick is up
  and there's time — keep this ride tight on G1/G2.
- Realistic-time (session-3 lesson): G2 is a *verify* step that can become *investigation* — the
  iterate buffer + the desk reflash loop are budgeted for exactly that.

## Retro (partial — desk-only day, no bike time; see [`PLAYBOOK.md`](PLAYBOOK.md) §4)
- **Went well:** desk-derisk caught a real gap **off the rider's clock** (no `code/.venv`, no `bleak` on Py 3.14) — installed bleak 3.0.2 + API-verified `06_capture_ble.py` before any bike ask. Strategy pivot to **own-unique-ID** (`62145` + phantom R `4964`) found to be **fully runtime-supported, no reflash** — removes the battery-pull entirely. App-pairing recon nailed the mechanism (free-type L/R fields). Identity change staged + verified on-air (−59 dBm).
- **Went wrong / slow (+ root cause):** agent directed an in-app pairing step while the rider was **still upstairs** — inferred bike-presence from conversational momentum. Root cause: no explicit "at the bike" gate. **Fix landed:** [`PLAYBOOK.md`](PLAYBOOK.md) §2 first execute-rule + memory `rider-explicit-at-bike`. (Minor: re-tripped the `pkill`-self-match footgun killing the `/log` poller — filter matched its own command line; harmless but note `$PID` exclusion next time.)
- **Planned vs actual:** planned ~25–35 min **bike**; actual = **0 min bike** (out of time) + ~30 min **desk** staging. Net win: next session starts fully staged, so the bike portion should be shorter.
- **Changes before next session:** none blocking — staging persists in NVS. Consider promoting the `62145`+phantom-R approach into the doc **body** as the primary path (demote byte-faithful-`62144`+battery-pull to fallback) once the bike confirms it.
- **Next gate + desk work that must precede it:** none — purely "rider at the bike." **First gate next time:** does the SB20 connect to the ESP at `62145` when the app L-id is set to it (with the real `62144` still powered)?

## Retro — on-bike day 2 (2026-06-25)
- **Went well:**
  - The **capture → ground → fix → confirm** loop closed *on the bike*: G1 captured the real `0x10`, we read company-id 442 + the offset-bearing mfg-data straight out of the bytes, fixed the firmware, flashed, and the app completed — with the displayed **"901/951" literally being our replayed bytes** (`0x0385`/`0x03B7`). About as clean a byte-level confirmation as exists.
  - The "agent runs all tooling, rider narrates" model held through a complex live build+flash; diagnostics (BLE scans + `/log`) explained every step (phantom-R fails, no double-count, the calibrate spin, the post-flash re-pair).
  - Honored the owner's constraint precisely — built the live binary from the **pre-lockdown base `8494935`** so the deferred 2026-06-24 security lockdown never touched the session.
  - Re-confirming the pre-flight in the board's **final position** caught the C3-doesn't-roam reboot (first `/status` hit it mid-boot); mandatory, and it paid off.
- **Went wrong / slow (+ root cause) — the session was painful for the rider:**
  - **A predictable failure was run un-ordered.** The `SPOOF_MFG_COMPANY_ID = 0x0000` placeholder made G2's spin foreseeable, yet we ran **G2 before G1** and spent a calibrate-spin cycle confirming the obvious. *Root cause:* the plan never flagged that G2 was *predetermined* to fail until G1 grounded the company-id. **(owner feedback #1)**
  - **The build toolchain was absent on the bike laptop.** No PlatformIO (Python 3.14 orphaned it; empty `~/.platformio`) and no host `gcc`. *Root cause:* the dev environment isn't reproducible/verified across the **desk↔bike-laptop** split, and the cold-start's `python -m platformio run` was stale. Cost ~30+ min of bring-up on the rider's clock. **(owner feedback #2)**
  - **OTA didn't connect first try.** PlatformIO's espota picked host_ip `0.0.0.0` on this multi-NIC laptop (WiFi + WSL/Hyper-V + link-local) → "No response from device"; fixed by calling `espota.py` with explicit `-I 192.168.1.223`.
- **Planned vs actual:** planned ~25–35 min bike; actual ≈ **70 min** at the bike (≈06:47–07:58) **+** a long unattended toolchain build. The overrun was almost entirely the **toolchain bring-up** (unplanned) + the predictable-G2 detour — process misses, not the bike work itself (Gates 1–3 + the G1 capture were quick once tooling was up).
- **Changes to make before next session (→ fold into PLAYBOOK.md):**
  1. **Plan-time placeholder audit + gate ordering:** enumerate every flagged-unknown/placeholder in the spoof surface; order gates so the *grounding capture precedes any test that depends on it*; reframe such a test as a post-reflash confirmation, not a live gate.
  2. **Reproducible env + a build-toolchain pre-flight check:** committed requirements/lock + a provisioning script (PlatformIO + the BLE venv + a host compiler); add "can we **build AND flash** a firmware?" to the bike pre-flight (a trivial `pio run` / cached-toolchain check) — answered at the desk, never at the bike.
  3. **OTA host-IP pinning:** the flash helper / cold-start must pin the host IP for espota on multi-NIC machines (`espota.py -I <lan-ip>`), or pick the interface on the board's subnet.
- **Next gate + desk work that must precede it:** the **canonical desk reflash** — rebuild on **current `main`** (security lockdown) **+ the 442 fix**, flash the board, restore the `62144` identity, and re-confirm the calibrate still completes on the locked-down firmware. Then **session 5** (meter-to-meter `/calibrate` ride) is the next 🟢 READY bike ride.
- _(More owner feedback pending — fold in on arrival.)_
