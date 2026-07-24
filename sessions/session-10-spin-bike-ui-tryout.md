# 🚴 Session 10 — the three-board fleet on the bike: real-erg drive + tester UX (2-day plan)

**Status: 🟢 READY** · tracked in [`sessions/README.md`](README.md). Run via [`PLAYBOOK.md`](PLAYBOOK.md)
(record actuals inline, ⏱ from the start, retro at the end).

**Goal:** move everything built this week from *twin-proven* to *rider-proven*. Two things have never
been tested with a human on the bike: (1) the on-device **FTMS erg drive** pointed at the **real SB20**
(not the C3 trainer sim), and (2) the **tester UX** — hard-reset WiFi onboarding (QR), the filtered
device picker, and the shared web SPA — on all three head-units. Split across two short days so the
rider's time is spent on the bike, not waiting on the desk.

> **Why now:** the desk work is done and green on `main` — three head-unit tiers share one core
> (C3-OLED / S3-Touch / CYD), erg drive is twin-proven (CYD↔C3-sim, `decisions.md` 2026-07-05), the
> web UI is one shared SPA served both from GitHub Pages (Web Bluetooth) and each ESP32 at `/app`
> (HTTP), onboarding is QR-driven on `172.29.4.1`. The bike is the only thing left that a fake meter
> and a sim can't prove.

---

## Fleet + roles (all on current `main`)

| Board | Port / addr | Role this session |
|---|---|---|
| **C3-OLED** | COM9 · `sb20proxy.local` | **the proven ride path** — reads the Assioma, spoofs `Stages 62145`. Start here every day. |
| **CYD** (240×320 resistive) | COM17 · `sb20proxy-cyd.local` | budget touch head-unit — onboarding + picker + erg UX |
| **S3-Touch** (172×320 capacitive) | COM16 · `sb20proxy-s3.local` | premium touch head-unit — same UX; **marginal RF, watch for it** |
| **spare C3** (no OLED) | COM10 | desk only — the FTMS trainer sim if you want a safe erg dry-run first |

Hardware on the bike: the **Assioma** pedals, a **phone**, and the **SB20** (with its erg loop).

---

## ⚙️ Desk pre-stage (do this the night before — the rider never waits on it)

- [ ] `git pull` → all three boards flashed/OTA'd to current `main`; each `curl http://<addr>/status`
      returns 200 with the right identity. (C3=`62145`, CYD/S3=`62144`.)
- [ ] Each board's **stored WiFi is your ride network** (not the bench's Donnie Boon if that differs) —
      re-provision now, at the desk, so onboarding is a *demo* on the bike, not a *dependency*.
- [ ] Print/screenshot the **per-device setup PIN** for each board (OLED shows it; LCD boards show the
      QR). Only needed if you re-onboard.
- [ ] Confirm the **`.zwo` import path** works desk-side once:
      `python code/scripts/import_workout.py <some.zwo> --ftp <FTP> --post http://sb20proxy.local`.
- [ ] **Battery-out the real crank** you'll displace (session 8/9 rule) staged and ready.

---

## Day 1 — the headline: real-SB20 erg drive (~25 min on the bike)

**This is the one genuinely unproven, genuinely valuable thing.** Everything before it is a gate.

### Gate (off the bike — don't pedal until green)
1. Power the **C3-OLED**. OLED shows **Connecting → its IP**. Phone opens `http://sb20proxy.local/` →
   **Ride** screen loads.
2. **Setup** → your **Assioma** is in the (now filtered) picker; tap it → Save. Pedal: **power tracks**
   on Ride, IN→OUT title shows Assioma → Stages.
3. **Pair the SB20** in the Stages app — **NO battery pull** (the session-8 own-id pivot). Two
   free-type L/R fields: **L = `62145`** (the C3/ESP), **R = `4963`** (your **real right crank**, left
   powered). Sessions 8/9: the SB20 needs both ids *findable* (a phantom R failed; the real 4963
   works) and there's **no double-count** — the bike consumes only the ESP's doubled-left. May already
   be remembered-paired → just reconnects. SB20 then shows your corrected watts.
   *Restore-after (if you ever revert): real pair `Stages 62144 (L) : 4963 (R)`, length 165 mm,
   zero-offset L 903 / R 951.*

### The erg run
4. **Set the SB20 as the erg trainer.** On the phone `/setup` → **Trainer / erg** section → the SB20's
   FTMS machine should appear in the trainer list → pick it → Save (board reboots, ~5 s).
   - *If the SB20 doesn't expose an FTMS 0x1826 service, that's the finding — capture what it DOES
     advertise (`/diag`), and fall back to the trainer-sim dry-run (below). The erg client is
     twin-proven; the open question is purely the SB20's real FTMS surface.*
5. **Load + start a workout.** `/workout` → **4×8 Threshold** (or a short one) → **Start**.
6. **Watch for erg control:** the Workout screen's erg line should go **`erg: ON <target>W`**, and the
   **SB20's resistance should change to hold the segment target** as you pedal. Skip a segment → the
   target (and the felt resistance) changes. Stop → resistance releases.
7. **Record the felt behaviour** — this is subjective rider feedback the bench can't give: does erg
   feel responsive, laggy, hunting, stable? Note the numbers the phone/SB20 show vs what you feel.

### Day-1 pass / record
- **Primary:** the SB20's resistance tracks the workout target under real pedalling. ✅/❌ + notes.
- If ❌ at step 4 (no FTMS): the **safe dry-run** — point erg at the **spare-C3 sim** on the desk
  (`/setup` trainer = `SB20-FTMS-Server`) and confirm the whole *UI* path (pick → ON → target → skip)
  end-to-end. Proves everything but the SB20's own FTMS.
- **Bike safety:** if erg misbehaves (session 9 had an async erg bug), **Stop** releases it; worst case
  power-cycle the board — the SB20 falls back to its own resistance.

---

## Day 2 — tester UX across the touch boards (~20 min, mostly off the bike)

The onboarding + picker + shared-SPA experience a *beta tester* will actually hit. You're the first
real user of these on hardware.

### A. Hard-reset onboarding (the out-of-box experience) — pick ONE touch board
1. On the **CYD** (or S3): `POST /forget` from the phone (or hold BOOT on the CYD) → it reboots into
   the **setup portal**.
2. The panel shows the **QR onboarding screen** (Wi-Fi setup · scannable code · SSID/PIN · `172.29.4.1`).
   **Scan it with your phone camera** → it should auto-join `SB20-Setup`.
3. Phone opens the portal → pick your network → Save. Board reboots and rejoins. **Time it** — this is
   the tester's first five minutes; note any confusion.

### B. The filtered device picker
4. **Setup** on the panel → the list should show **only meters/cranks/trainers** (labelled
   `meter -NN` / `crank` / `trainer`), not the room's pet feeders and phones. Confirm your Assioma +
   the SB20 (if FTMS) are there and tappable, and a tap picks the row you see.

### C. The shared web SPA (`/app`)
5. Phone → `http://sb20proxy-cyd.local/app` (or `-s3`). The **shared Bike Bridge SPA** loads
   same-origin — same UI the GitHub-Pages/Web-Bluetooth version gives. Walk Ride / Setup / Workout /
   Calibrate; confirm live status polls, the picker works, a workout drives.
6. **Portable profile (optional):** if you have a corrector curve, export it from one board's SPA and
   load it onto another — the cross-device calibration-profile path.

### D. Ride each touch board briefly (S3 + CYD as head-units)
7. Power each near the bike, pedal, confirm **POWER tracks** on the panel and the SB20 accepts its
   crank. Walk the 5 screens by tapping. **S3 RF caveat:** it's marginal per-boot — if it won't join
   or read, reboot once; if still flaky, that's the antenna, note it and move on.

### Day-2 pass / record
- Onboarding: QR scan → joined → provisioned, and roughly how long.
- Picker: only real devices, correctly labelled, taps land right.
- SPA: loads at `/app`, drives the board.
- Per-board: which head-unit felt best to actually use, and why (the pre-beta hardware decision).

---

## 🎯 What we most want out of this (the feedback that steers the next week)

1. **Does real erg work, and does it feel good?** — decides whether erg is a shippable feature or a
   longer project.
2. **Is onboarding tester-proof?** — the make-or-break of the beta program.
3. **Which touch board earns the pre-beta slot?** — CYD (cheap) vs S3 (premium), by feel.
4. Anything that looked wrong on a real phone/panel (fonts, truncation, tap targets, latency) — all
   desk-fixable since the render/parse is host-tested → a follow-up PR each.

## Retro (fill in at the end — [`PLAYBOOK.md`](PLAYBOOK.md) §4)
- **Went well:**
- **Went wrong / slow / confusing (+ root cause):**
- **Changes before next session (process / run-sheet / tooling):**
- **Next gate:**
