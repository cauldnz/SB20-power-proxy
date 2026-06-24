# 🚴 Session 8 — SB20 spoof calibration handshake (G1 + G2)

**Status: 🟡 PAUSED (2026-06-24) — desk-staged, awaiting bike time** · tracked in [`sessions/README.md`](README.md). Run via [`PLAYBOOK.md`](PLAYBOOK.md)
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
