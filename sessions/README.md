# 🚴 Physical-interaction session ledger

The single index of every on-bike / hardware session we **plan and run**. History lives here — each row
links to that session's **Plan + Actual** doc, and completed sessions stay as the record. Convention:
CLAUDE.md → *Session plans & the session ledger* (record Actual against Plan, status header, mark DONE).

> **Latest: session 3 ✅ DONE (2026-06-19).** Next: **session 4 🟡 PLANNED** —
> [ground the Enhanced-Offset `0x10` format + brake-lever probe](session-04-enhanced-offset-and-brake-levers.md).
> **A1 desk fix landed** (spec-correct `0x10` Enhanced reply; the exact mfg-id/data are session-4's G1
> capture). · bike-machine cold-start: [BIKE-SESSION-READY.md](../BIKE-SESSION-READY.md)

| # | Date | Status | Session (Plan + Actual) | Outcome |
|---|------|--------|--------------------------|---------|
| 4 | 2026-06-19 | 🟡 PLANNED | [ground Enhanced-Offset (`0x10`) + FTMS erg + brake-lever probe](session-04-enhanced-offset-and-brake-levers.md) | — *(G1 capture real crank `0x10` → grounds A1; G2 retest spoof; **C FTMS erg-acceptance** → gates the shifter-erg feature; B brake levers)* |
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
