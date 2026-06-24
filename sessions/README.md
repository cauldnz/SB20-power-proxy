# 🚴 Physical-interaction session ledger

The single index of every on-bike / hardware session we **plan and run**. History lives here — each row
links to that session's **Plan + Actual** doc, and completed sessions stay as the record. Convention:
CLAUDE.md → *Session plans & the session ledger* (record Actual against Plan, status header, mark DONE).

> **Latest done: session 8 ✅ (2026-06-25)** — the SB20 crank-spoof's **calibrate / zero-reset handshake is
> CLOSED**. **G1** captured the real crank's Enhanced-Offset `0x10` reply (`20 10 01 00 00 ba 01 04 85 03 b7 03`):
> Manufacturer **Company ID 442** + mfg-data `04 85 03 b7 03` that *encodes* the L/R zero-offsets (901/951).
> The old `Config::SPOOF_MFG_COMPANY_ID = 0x0000` placeholder was exactly why the Stages app's calibrate spun
> (sessions 2–3). **G2:** with the real 442 + mfg-data, the app's calibrate **completes** (confirmed live — it
> shows "Calibrated 901/951", literally our replayed bytes). Also: the own-unique-ID spoof (`62145`) pairs
> **only if the configured right crank id is findable** (a phantom R `4964` → "pairing failed"; real `4963`
> works), and then shows **no double-count**. Fix on branch `session/08-onbike-20260625` → PR; board temporarily
> on a **pre-lockdown+fix build** (desk to reflash `main` + the 442 fix). **Next 🟢 READY ride: session 5** —
> the on-device meter-to-meter `/calibrate` ride (track bike).
> · cold-start: [BIKE-SESSION-READY.md](../BIKE-SESSION-READY.md)

| # | Date | Status | Session (Plan + Actual) | Outcome |
|---|------|--------|--------------------------|---------|
| 8 | 2026-06-25 | ✅ DONE | [SB20 spoof calibration handshake (G1 + G2)](session-08-sb20-spoof-calibration.md) | **Both goals met.** Own-id spoof (`62145`) pairs — but needs a **findable right crank** (phantom R `4964` → "pairing failed"; real `4963` works) — and then shows **no double-count** (the bike consumes only the ESP's doubled-left). **G1:** real `0x10` = `20 10 01 00 00 ba 01 04 85 03 b7 03` → **Company ID 442** + mfg-data encoding L901/R951. **G2:** the Stages app's calibrate **COMPLETES** with the real 442+mfg-data (the `0x0000` placeholder was the spin). Firmware fix + golden test on `session/08-onbike-20260625`; built/flashed live from pre-lockdown base `8494935`. → `decisions.md` 2026-06-25. Board on pre-lockdown+fix build (desk: reflash `main` + the fix). |
| 7 | 2026-06-22 | ✅ DONE | [comprehensive passive ride monitor (qdomyos training ride)](session-07-comprehensive-monitor.md) | **Power topology RESOLVED:** one-clock ANT+ — **SB20 = Stages crank 1:1**, both **~11% high vs Assioma** (≈1.11×); **overturns session-4's ~30%-low**. **Block S:** qdomyos ergs over **standard FTMS** (vs the app's proprietary `0c46be`) + shifter captured. **Assioma BLE L/R balance** grabbed (proxy-forward grounding). Sniff-before-connect caught the `CONNECT_IND` (44k ATT, 39k ANT records). → `decisions.md` 2026-06-22 |
| 6 | 2026-06-21 | ✅ DONE | [sniff the app↔SB20 erg conversation + power-topology Phase 2](session-06-sniff-and-power-topology.md) | **Block S ✅:** app↔SB20 erg is **cleartext** (no `btsmp`/bond) over the **Stages-proprietary `0c46be`** char (handle 0x0039, `02 00 <u16> 00 00`), **not FTMS**; the `<u16>` is an app-side load setpoint (≠ watts). **Pipeline delivered:** `pcap_sqlite`+`fit_sqlite` (tshark→SQLite, both sniffs & FITs; suite 322 green). **Phase 2 ❌ blocked** — sweep/zero sniffs adverts-only (started after connect → no `CONNECT_IND`); topology still open (FIT preliminary: Stages ~10% high, *conflicts* w/ Phase 1). **Lesson: sniff before connect.** |
| 5 | 2026-06-23 | 🟢 READY | [meter-to-meter calibration ride (XCadey → reads like Assioma)](session-05-meter-calibration-capture.md) | **Re-scoped to the on-device wizard** (replaces the old ANT+ capture→desk-fit plan): a single `/calibrate` session does it all. Desk-derisked 2026-06-23 — wizard renders + routes work over WiFi, **ride-blocking form-POST bug found + fixed** (PR #107), both boards on current firmware. **Bike proves:** 2-meter coex on the C3 + the real fit. Opportunistic — track bike on a trainer. |
| 4 | 2026-06-21 | ✅ DONE | [ground Enhanced-Offset (`0x10`) + FTMS erg + brake-lever probe](session-04-enhanced-offset-and-brake-levers.md) | **§C FTMS erg ✅ PASS** (SB20 holds 3rd-party Set-Target-Power; codec validated) — but surfaced a **power-topology finding** (SB20 erg reads ~½–⅔ of the Assioma, likely single-sided → [`sb20-power-topology.md`](../code/findings/sb20-power-topology.md)). **§B shifter fully mapped** (brakes app-gated; 6 buttons, 4/5≡1/2; chord/double-tap/hold-vs-tap). ANT+ stick up + documented. **G1/G2 + §D deferred** (no flash) |
| 3 | 2026-06-19 | ✅ DONE | [verify PR #5 fixes + map the shifters](../BIKE-SESSION-3.md) | A3 reconnect + A4 handshake ✅; A1 zero-reset ❌ + A2 crank-length ⚠️ (Stages app bypasses standard CP — desk fix); **full 6-button shifter map** captured (one-hot `0c46be60`, **stateless** → Zwift-Click-ready); silent chans likely aero-remote pods |
| 2 | 2026-06-18 | ✅ DONE | [does the SB20 read the faithful spoof? + capture handshake](../BIKE-SESSION-2.md) | SB20 accepted power **and** cadence, crank-free; control-point / reconnect bugs captured → fixed in PR #5 |
| 1 | pre-session-2 | ⛔ SUPERSEDED | [ANT+ Phase-1B pairing run-sheet](../NEXT-BIKE-SESSION.md) | Folded into sessions 2–3; kept for the un-run ANT+/Phase-1B steps |

## How to run a session

**The full playbook — plan → execute → document → retro — is [`PLAYBOOK.md`](PLAYBOOK.md). Read it
before directing a session.** In short:

1. **Read** the active session's doc (and its cold-start, for a bike session).
2. **Guide it live and write each step's result back into the doc** — `✅` pass / `❌` fail / `⚠️` partial,
   plus the observed bytes / values / UI / `/log` lines. Don't leave the result only in chat.
3. **Close it out:** set `Status: ✅ DONE (date)` atop the doc with a one-line Outcome, update this
   ledger's row, **promote durable findings** to `code/findings/decisions.md` (append-only) and commit
   any captured bytes to `code/findings/captures/`.
4. **Next session:** add a new row here and put its doc in this `sessions/` folder.

> *Legacy note:* sessions 1–3 live at the repo **root** (`BIKE-SESSION-*.md`, `NEXT-BIKE-SESSION.md`)
> because the append-only `decisions.md` links them there; this ledger tracks them in place. New session
> docs (session 4+) live in `sessions/` to keep the root clean.
