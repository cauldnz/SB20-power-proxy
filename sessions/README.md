# 🚴 Physical-interaction session ledger

The single index of every on-bike / hardware session we **plan and run**. History lives here — each row
links to that session's **Plan + Actual** doc, and completed sessions stay as the record. Convention:
CLAUDE.md → *Session plans & the session ledger* (record Actual against Plan, status header, mark DONE).

> **Latest done: session 7 ✅ (2026-06-22)** — comprehensive passive monitor through a qdomyos training ride
> (ANT+ Assioma + Stages-crank + SB20-FE-C + HR, **+** nRF on qdomyos↔SB20 FTMS, one clock). **Power topology
> RESOLVED:** the SB20 reports the **Stages crank 1:1**, and both read **~11 % HIGH vs the Assioma** (≈1.11×)
> — **overturns session-4's ~30%-low** (a cross-capture FIT-alignment artifact). Also: qdomyos drives erg
> over **standard FTMS** (vs the Stages app's proprietary `0c46be`, session 6); the shifter came through; and
> we grabbed the Assioma BLE **L/R balance** (grounding for the proxy-forward backlog). **Open next:**
> **G1/G2** (needs a USB flash) + the opportunistic **session 5** (track bike — paired XCadey+Assioma).
> · cold-start: [BIKE-SESSION-READY.md](../BIKE-SESSION-READY.md)

| # | Date | Status | Session (Plan + Actual) | Outcome |
|---|------|--------|--------------------------|---------|
| 7 | 2026-06-22 | ✅ DONE | [comprehensive passive ride monitor (qdomyos training ride)](session-07-comprehensive-monitor.md) | **Power topology RESOLVED:** one-clock ANT+ — **SB20 = Stages crank 1:1**, both **~11% high vs Assioma** (≈1.11×); **overturns session-4's ~30%-low**. **Block S:** qdomyos ergs over **standard FTMS** (vs the app's proprietary `0c46be`) + shifter captured. **Assioma BLE L/R balance** grabbed (proxy-forward grounding). Sniff-before-connect caught the `CONNECT_IND` (44k ATT, 39k ANT records). → `decisions.md` 2026-06-22 |
| 6 | 2026-06-21 | ✅ DONE | [sniff the app↔SB20 erg conversation + power-topology Phase 2](session-06-sniff-and-power-topology.md) | **Block S ✅:** app↔SB20 erg is **cleartext** (no `btsmp`/bond) over the **Stages-proprietary `0c46be`** char (handle 0x0039, `02 00 <u16> 00 00`), **not FTMS**; the `<u16>` is an app-side load setpoint (≠ watts). **Pipeline delivered:** `pcap_sqlite`+`fit_sqlite` (tshark→SQLite, both sniffs & FITs; suite 322 green). **Phase 2 ❌ blocked** — sweep/zero sniffs adverts-only (started after connect → no `CONNECT_IND`); topology still open (FIT preliminary: Stages ~10% high, *conflicts* w/ Phase 1). **Lesson: sniff before connect.** |
| 5 | 2026-06-19 | 🟡 PLANNED | [paired XCadey + Assioma capture (meter-to-meter calibration)](session-05-meter-calibration-capture.md) | — *(track bike on a trainer; structured power×cadence blocks → fit the XCadey→Assioma correction; opportunistic — runs whenever the track bike is available)* |
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
