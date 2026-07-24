# Session 12 — the erg workout ride: prove the stack while you actually train

**Status:** 🟢 READY (planned 2026-07-25) · **Bike:** Stages SB20 (`E4:AA:5A:D6:0E:D4`) ·
**Board:** C3-OLED bike board `sb20proxy.local` / **192.168.1.165** (COM9) · **Branch:** `main`
(everything is merged — no feature branch) · **Budget: ~80 min, 3 must-dos** (≈25 min of gates, then a
**~53-min workout** that doubles as the soak test).

> **Design principle for this one:** the erg gate *is* the workout. If the SB20 takes our target power,
> you ride a real structured session driven by our own stack, and that ride simultaneously soaks the
> spoof, the workout engine, coex/heap, the captures and the web UI. You get training; we get evidence.

**The rider's job is the hardware and the pedals. The agent runs every tool** (PLAYBOOK §"You run the
tooling"). Don't paste commands — say what you see, the agent drives.
**Tell the agent explicitly when you are AT THE BIKE.** It must never infer that.

---

## 1. Why now

A very large amount shipped between 2026-07-10 and 07-24 and **almost none of it has touched a real
SB20**. The single highest-value unproven thing is *erg drive against the real bike*
([`forward-plan.md`](../code/findings/forward-plan.md) §14 phase 5) — and it is also the thing that
turns a test session into a training session.

Banked already, **do not re-plan**: sessions 8/9 (spoof pairing + calibrate + zero-reset), session 7
(power topology), session 11 **G1** (OBC transmit + decode, PASS 2026-07-08).

---

## 2. Desk pre-stage — done 2026-07-25, before you woke up

| Check | Result |
|---|---|
| Python suite | ✅ 471 passed, 1 skipped |
| ESP32 host tests | ✅ 245 cases · nRF host tests ✅ 40 cases |
| Firmware links | ✅ `esp32c3-wifi` (Flash 62.3%), `xiao-sense`, `feather-nrf52840` |
| CI on `main` | ✅ green (and now genuinely compiles both nRF board envs) |
| **nRF sniffer** | ✅ **proven live capturing on this desk** — `sniffer on COM13 @ 1000000 baud`, 24 advertisers seen |
| `sniff_ble.py --help` | ⚠️ **was crashing** (UnicodeEncodeError on a Windows console) → **fixed + reverified** (`52a4aca`) |
| `import_workout.py` | ⚠️ **fails on system Python** (`ModuleNotFoundError: sb20proxy`) → **use the venv**, see below |
| **CYD board (COM17)** | ⚠️ **`esp32cyd-live` on `main` boot-looped** (Watty Birds frame = a 150 KB global ctor OOM on the no-PSRAM CYD) → **found + fixed on hardware**, PR #281; CYD now boots clean (`[lcd] CYD 240x320 up`, `[lvgl] alive`). **The bike board is the C3-OLED — it was never affected.** |
| Bike board (`192.168.1.165`) | ❌ **unreachable from this desk** (off/away) — **could not be pre-flashed**. G0 recovers + flashes it at the bike. |
| ANT+ stick | ❌ **not present on the desk machine** — could not be pre-verified. See §7 risk |

> **Note on PR #281 (CYD boot-loop fix):** merge it before flashing a **CYD** as the head-unit. It does
> **not** touch the C3 ride path (that board has no LCD and never hit the bug), so it is not a blocker
> for the ride itself — but the CYD is unusable as a display until it lands.

> **Use the venv for every Python tool:** `code\.venv\Scripts\python.exe` — the package is installed
> there (editable) and `bleak` is present. System Python does **not** have `sb20proxy`.

---

## 3. Bring-list

- The **SB20**, powered on. **Fresh CR2032 in the real LEFT crank** if you have one (`62144` has read
  12–14 % for three sessions).
- The **C3-OLED bike board** powered near the bike (it joins WiFi as `192.168.1.165`).
- **Phone** with the **Stages app** (pairing) and a browser for the web UI.
- The **laptop** on the same WiFi (this is where the agent runs).
- Optional/stretch: the **Assioma** pedals (enables the MeterCompare gate), the **nRF sniffer dongle**,
  an **ANT+ stick**, the **XIAO nRF52840**.

## 4. Restore-list — write these down before touching any pairing

> Real pair **`Stages 62144 (L) : 4963 (R)`** · crank length **165 mm** ·
> ANT+ zero-offset **L 903 / R 951**.
> (The spoof's **BLE** zero-offset is **0** — a different representation of the same calibration. Don't
> "fix" one with the other.)

---

## 5. The pairing recipe (the single most load-bearing rig fact)

Pair in the **Stages app** — **NO battery pull** (the session-8 own-id pivot). Two free-type L/R fields:

- **L = `62145`** — our C3/ESP spoof
- **R = `4963`** — your **real right crank** (left crank powered)

Sessions 8/9 established: the SB20 needs **both ids findable on air** (a phantom R failed; the real
`4963` works), and there is **no double-count** — the bike consumes only the ESP's doubled-left. It may
already be remembered-paired, in which case it just reconnects.

---

## 6. The gates — in information order

### G0 · Board alive + current (agent-run, before you're on the bike) — **must-do**, ~6 min
- **Goal:** the board on the LAN is running current `main`, not a 16-day-old build.
- **Action (agent):** `GET http://192.168.1.165/status` → check `fw`/identity; if stale, OTA-flash
  `esp32c3-oled-live-ota` (RSSI must be better than ≈ −72 dBm; pass the explicit host IP `-I`).
- **Expected / PASS:** `/status` answers, identity `Stages 62145`, mode `spoof`, `/stats` free-heap
  > 100 KB, `reset_reason` clean.
- **FAIL → fallback:** board unreachable → power-cycle, wait **25 s** (a fresh boot needs that before
  `/`, `/status`, `/log` rebind) and retry. Still dead → USB: `flash_c3.py --env esp32c3-oled-live-ota
  --port COM9` (**not** `flash.ps1 -Mode usb`). Wedged USB-JTAG → HOLD BOOT, TAP RESET, RELEASE BOOT.

### G1 · Pair + corrected power on the bike — **must-do**, ~8 min
- **Goal:** re-confirm the proven spoof path end-to-end before anything depends on it.
- **Action (rider):** pair per §5; pedal easily ~30 s.
- **Expected / PASS:** the **SB20 head-unit shows watts** that track your pedalling, and the board's
  `/log` shows `source:connected` with power flowing. Agent captures `/status` + `/diag`.
- **FAIL → fallback:** if the SB20 won't pair, check **both** ids are advertising (the symmetric rule);
  the real left crank must be awake for `4963`… (it's the *right* crank id — left powered). If pairing
  still fails, fall back to the restore pair in §4 and we ride manual — **the session continues**.

### G2 · **Erg go/no-go** — the highest-information gate — **must-do**, ~5 min
- **Goal:** does the SB20 actually move resistance when *we* write FTMS Set Target Power? This decides
  whether your workout is erg-driven or manual, so it runs **before** the workout, not during it.
- **Action (agent):** the guarded probe from
  [`CAPTURE-sb20-erg-recovery.md`](CAPTURE-sb20-erg-recovery.md) Option A —
  `capture_ftms.py --erg --erg-targets 120,180,90 --erg-hold 25` against `E4:AA:5A:D6:0E:D4` while you
  pedal steadily.
- **Expected / PASS:** resistance **noticeably steps** at each target change and the control point
  **answers** (not just "no error"). Agent banks the golden erg vectors.
- **FAIL → fallback:** *A no-answer is a legitimate, valuable result, not a bug in our stack* — the qz
  maintainer reported the SB20 "doesn't answer at all" (#1649). If it doesn't answer: **skip erg**, ride
  the workout in **manual/free mode** (you set the resistance by feel to the on-screen target), and we
  still validate the whole power path, OBC, UI, and coex. Log the negative result — it settles an open
  question either way.
- **Safety:** Stop releases erg. Worst case, power-cycle the board — the bike returns to normal.

### G3 · **The workout** (the soak) — **must-do**, ~53 min
- **Goal:** you train; the ride simultaneously soaks everything.
- **Default workout: `4×8 Threshold`** — 10 min warm-up, 4 × 8 min @ 99 % FTP with 2 min recoveries,
  5 min cool-down = **53 min** (verified against the on-device preset `4x8`:
  `600 + 4×480 + 3×120 + 300` s). Chosen because the sharp 99 % ↔ 50 % transitions are the *best* erg
  validation. **Easier swap:** `Endurance 45` (`endur45`, 45 min @ 68 %) or `Sweet Spot 3×12` (`ss3x12`,
  longer — too long today). All three are **built-in firmware presets** (`WorkoutPresets.h`) — the agent
  just picks one on `/workout`, **no file import needed**. Say which you want.
- **⚠️ FTP:** the presets assume **`ftp_w` 250**. If yours differs, tell the agent **before** loading —
  it will import a scaled copy (`import_workout.py --ftp <yours>`, **run from the venv**) and POST it.
- **Action:** agent sets the SB20 as the erg trainer via `/setup`, loads the preset on `/workout`, you
  Start and ride.
- **What the agent watches, live, while you pedal** (no extra rider effort):
  - `erg: ON <target>W` and whether resistance holds each segment
  - `/stats` free-heap + `reset_reason` — the **coex soak** (single-core C3 running BLE central + BLE
    peripheral + WiFi). A watchdog reset here is *a real result*, recorded not hidden.
  - power/cadence continuity through segment transitions
- **In-ride extras — zero stop required:**
  - **OBC paddles:** press each SB20 paddle once per interval; agent watches `obc_reader.py` for the
    mapped action. This is session 11 **G2** folded in for free.
  - **Web UI smoke:** agent loads `http://192.168.1.165/app` — confirms the **regenerated bridge codec**
    (shipped yesterday, `d56b554`) actually serves and drives a real board, plus the spoof/corrector
    selector renders and Scale/Offset are correctly **hidden** on this ESP32 (curve-only).
- **PASS:** you finish the workout; power tracked throughout; no unexplained reset.

### G4 · Stretch — only if you're fresh and time remains
Pick at most one; each is genuinely optional.
- **S1 · nRF R3** (~10 min) — the **nRF** BLE Stages spoof against the real SB20 (pair, power, the
  calibrate/zero handshake with the 442 + mfgData reply). The nRF path has *never* met a real SB20.
- **S2 · MeterCompare with two real meters** (~10 min) — put the **Assioma** on as meter B and open
  `/compare`. Today every number in that feature is **fabricated** (`B := A × 1.11`); this is the only
  way to answer the session-7 question "is the ~11 % flat or torque-dependent?".
- **S3 · qz on-air** — only if a **runnable qz build** is already staged at the bike (Linux desktop,
  from the fork branch `feat/obc-listener-upstream`). Would upgrade the upstream PR from "compiles" to
  "verified on air" — the exact steps, and what we may/may not claim, are in
  [`qz-upstream-contribution.md`](../code/findings/qz-upstream-contribution.md) §6.
  **Do not build qz on the rider's clock.**

---

## 7. Risks & landmines (designed around, not discovered at the bike)

- **ANT+ capture could not be pre-verified** — no stick on the desk machine. If you bring one, treat the
  first capture as *investigation*, not verification; don't let it block the ride.
- **Coex wedge** on the single-core C3 is the known risk of BLE-central + BLE-peripheral + WiFi at once.
  Watch `/stats`. Ride-mode WiFi-off exists if we need it.
- **The C3 never roams** — it bonds to one AP at boot. Low RSSI or a `/log` timeout → power-cycle.
- **A fresh boot needs ~25 s** before HTTP rebinds. Don't call it dead early.
- **JSON POSTs need `-H "Content-Type: application/json"`** or the ESP WebServer eats the body as form
  fields (the #239 lesson).
- **Sniffer (if used): start the capture BEFORE the app connects**, and verify within ~1 min that the
  followed device's **adverts stop** — a climbing packet count alone proves nothing (you may be
  following the wrong SB20 personality; it has at least three).
- **Power scale reminder:** the Stages cranks read ~5–13 % high vs the Assioma, cadence-dependent. The
  bike's own number cannot validate itself.

## 8. Cleanup

- Erg released (Stop), or power-cycle the board.
- If you touched pairing, restore per §4.
- OBC devmode/sink off if S3 was attempted.

---

## 9. Actual — fill in as we go

> Timestamp every gate (local `HH:MM`) beside its result. Record observed values/`/log` lines here, not
> just in chat. Log every mid-session change of plan.

| Gate | Start | Result | Observed |
|---|---|---|---|
| G0 board alive |  |  |  |
| G1 pair + power |  |  |  |
| G2 erg go/no-go |  |  |  |
| G3 workout + soak |  |  |  |
| G4 stretch |  |  |  |

**Deviation log:**

**Captures banked:**

## 10. Retro — mandatory before close-out

- Went well:
- Went wrong + root cause:
- Planned vs actual per section (from the timestamps):
- Changes before the next session:
- Next gate + the desk work that must precede it:

---

*Close-out (PLAYBOOK §close-out): flip Status to ✅ DONE + one-line Outcome, add/refresh the row in
[`sessions/README.md`](README.md), promote durable findings to
[`decisions.md`](../code/findings/decisions.md), commit captures, and retarget
[`BIKE-SESSION-READY.md`](../BIKE-SESSION-READY.md) at the next READY session.*
