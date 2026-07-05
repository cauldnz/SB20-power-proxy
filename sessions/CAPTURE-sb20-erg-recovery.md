# 🎯 SB20 erg — the recovery capture (after the 2026-07-06 qdomyos ride missed the control channel)

**Status: 🟢 READY** — ~5–10 min of rider time. Small, focused follow-up to the 2026-07-06 passive ride
(`CAPTURE-qdomyos-sb20-passive.md`), which captured the bike's FTMS surface cleanly but **missed the
control channel**. Read the two desk-analysis entries in `code/findings/decisions.md` (2026-07-06 desk
analysis) first — they change what's worth doing.

## What we already know (don't re-capture)

- **The real SB20's FTMS is fully erg-capable** (task-1 GATT dump): Fitness Machine Control Point `0x2AD9`
  (write+indicate), Target-Setting **Power(erg) + Resistance + Sim**, Supported Power Range **0–4000 W**.
  Device `E4:AA:5A:D6:0E:D4`, advertises **unnamed** (filter by address, not `--name SB20`).
- **qdomyos writes control to an Elite proprietary service (`347b…`) the SB20 doesn't have** → its
  `stagesbike` driver can't move the SB20's resistance (pinned bug #1649). So **sniffing qz's control is
  low-value** — it's writing into the void. Our own `FtmsErgClient` writes the **standard FTMS Control
  Point** the SB20 *does* expose, so our path is the sound one.
- The 2026-07-06 sniff was **polluted** by our own boards (the nRF "SB20 Bridge", the S3/C3 spoofed cranks
  all got connected). De-pollute before any sniff.

---

## ⭐ Option A (RECOMMENDED — the real §14-phase-5 gate): capture OUR erg drive against the bike

This directly answers the only open question — *does the SB20 actually move resistance when WE send a
Set Target Power?* — and captures the **golden erg vectors** we've lacked since F1 (the `SPEC_VECTORS`
in `ftms.py` are still spec-derived). It's our own controlled, guarded, bounded test — no qz involved.

**This is an ACTIVE write (`--erg`), not passive** — but it's *our* deliberate erg recon, done solo, on
a bike no one else is driving. Bounded targets, 25 s each; `capture_ftms.py --erg` is the tool built for
exactly this (see `ftms-protocol.md` §"What remains" and `capture_ftms.py` header).

1. **Clear the air:** power OFF every one of our spoof boards (C3-OLED, CYD, S3) and the nRF "SB20 Bridge"
   so nothing else holds or writes to the bike. Close qdomyos / any app that talks to the SB20.
2. **Wake the bike**, confirm it advertises: `python code/scripts/sniff_ble.py --scan-only --duration 12`
   → the unnamed `E4:AA:5A:D6:0E:D4` should be there (and now the *only* SB20-ish device).
3. **Run the guarded erg recon while pedalling steadily** (the rider must pedal throughout so the Indoor
   Bike Data shows whether resistance actually tracks):
   ```powershell
   $ts = Get-Date -Format yyyyMMdd-HHmm
   python code/scripts/capture_ftms.py --address E4:AA:5A:D6:0E:D4 --duration 180 `
       --erg --erg-targets 120,180,90 --erg-hold 25 `
       --output code/findings/captures/G-sb20-ftms-erg-$ts.jsonl
   ```
   It does Request Control (0x00) → Start (0x07) → Set Target Power (0x05) at 120 → 180 → 90 W, logging
   every Control Point indication (`0x80 <op> <result>`) and the Indoor Bike Data throughout.
4. **Rider observes + notes:** does the felt resistance step up at 180 W and down at 90 W? Does the bike's
   own display show the target? (subjective, but it's the ground truth the bytes can't fully give.)

**Pass = the Control Point indications are `result 0x01` (success) AND the pedalling effort visibly tracks
the targets.** That closes §14-phase-5's core question and pins the golden erg vectors. If the CP returns
`0x05` (control-not-permitted) or resistance doesn't move, that's the finding — capture it and we adapt.

---

## Option B (only if curious about qz specifically): de-polluted passive sniff of qz's connection

Lower value now (we know qz can't control the SB20), but it confirms *which* device qz connects to and
*which driver* it uses (CPS `stagesbike` vs FTMS `ftmsbike`). Strictly passive.

1. **De-pollute** as in A.1 (power down all our boards + the nRF bridge).
2. In qdomyos, connect to the bike; **read the connected-device name off the qz UI** (Settings/console).
3. Arm the sniffer on **that** device's address (extcap staged elsewhere after a Wireshark upgrade):
   ```powershell
   python code/scripts/sniff_ble.py --device <ADDR-FROM-QZ-UI> --duration 300 `
       --extcap-dir C:\repos\nrf52840-mdk-usb-dongle\tools\ble_sniffer\extcap `
       --output code/findings/captures/QDZ-recovery-$ts.pcap
   ```
4. **Live sanity check (the lesson from last time):** within **~1 min** of qz connecting, the followed
   device's adverts must **STOP**. If they don't, you're following the wrong device — Ctrl-C, re-read the
   qz UI, re-arm. Then reopen qz + nudge resistance once so a control write is on the wire.

---

## Wrap up (either option)

- Commit the raw capture(s): `git add code/findings/captures/… && git commit -m "capture: SB20 erg recovery …" && git push`.
- Analyze at the desk (never hand-parse a pcap): JSONL erg log → decode the Control Point indications +
  Indoor Bike Data (see the 2026-07-06 desk-analysis decode in `decisions.md`); pcap → tshark/sqlite.
- If Option A passed, promote the erg round-trip to `decisions.md` + flip the `ftms-protocol.md` gate
  (the `--erg` round-trip is the last spec-built caveat) and label the golden vectors real.
