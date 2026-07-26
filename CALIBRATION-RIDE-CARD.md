# 🚴 Calibration Ride Card — session 2

> ⛔ **SUPERSEDED — historical.** A point-in-time operational card, kept for provenance because
> [`code/findings/decisions.md`](code/findings/decisions.md) links it. Do not follow it as a procedure.
> The current equivalents are **[`sessions/PLAYBOOK.md`](sessions/PLAYBOOK.md)** (how to run an on-bike
> session), the ledger **[`sessions/README.md`](sessions/README.md)**, and
> **[`PROJECT-MAP.md`](PROJECT-MAP.md)**.

**Two objectives, one capture:**
1. **Close open-question #7** — does the SB20 rescale crank power? (Decides whether
   "feed Assioma → erg targets land on Assioma watts" is literally true, or needs a
   one-number bike correction.)
2. **Map the Stages↔Assioma offset surface** and find empirically how few cells a
   real calibration needs (research / optional — see `decisions.md`; the proxy
   doesn't strictly need this, but it's good science and a blog result).

Everything is captured in **one same-clock multi-source recording** — Stages crank +
Assioma + the bike's own FE-C output, all on one stick. **No watch this time.**

**Time on bike: ~22 min.** Claude drives the capture and calls every cell in chat
(the model that worked last time — you just ride). Keep this chat open.

---

## Bike setup (important)

- **⚠️ CRANK LENGTH — do this first, it's a direct multiplicative confound.**
  Both meters compute power as F × ω × L, each using its *own* configured crank
  length, and the SB20 cranks are adjustable (165–175 mm). The Stages/Assioma
  ratio ≈ ratio of the two configured lengths, so a mismatch fakes up to several
  percent of "offset". Make all three agree **before** riding:
  1. Note the **physical hole** the pedals are actually in (mm).
  2. **SB20 app** → crank length = that value.
  3. **Favero Assioma app** → crank length = the *same* value.
  (Tell Claude the three values — a current mismatch may explain part of day-1's
  1.085 ratio.)
- **Mode: ERG for the grid, LEVEL/resistance for the sprints.** (Revised from an
  earlier draft.) In erg the bike *pins* the power so you only manage cadence —
  a cleaner constant-power cadence sweep, and less to juggle. Sprints need
  resistance mode because erg caps you at the setpoint.
- Make sure your **power and cadence are visible** (bike app / head unit). For the
  grid you watch **cadence**; the bike holds the power.
- Cranks fresh-ish batteries (the crank read ~2.6 V last time — fine, but a fresh
  CR2032 removes a variable).

## Pre-flight (Claude does this before you clip in — ~60 s)

Per `code/findings/wsl-capture-runbook.md`:
1. `usbipd attach` the stick; confirm `lsusb` sees `0fcf` in Ubuntu-24.04.
2. Confirm the device node is `0666` (chmod once if not — clipboard command).
3. **Find the bike's FE-C id:** `openant scan` (look for a FitnessEquipment /
   `FEC ####` device) — or just use `--fec-id 0` (wildcard) in the capture.
4. 15 s throwaway capture to confirm all three sources broadcast, then stop.

## The capture (Claude launches, detached)

```bash
# device ids: Stages crank 62144, Assioma 17039, bike FE-C wildcard (0)
python code/scripts/07_capture_multi.py \
    --stages-id 62144 --assioma-id 17039 --fec-id 0 \
    --duration 1500 \
    --output code/findings/captures/CAL-multi-$(date +%Y%m%d-%H%M%S).jsonl
```
(launched via the robust procedure in the runbook so the stick can't get stuck)

---

## The ride

### Warm-up — 3 min, easy spin
Settle in. This also lets Claude confirm all three streams (Stages / Assioma /
bike-FEC) are flowing before the real work.

### Grid — ERG mode: 3 power setpoints, sweep cadence within each

For each setpoint: Claude says "set ~X W"; you set the **nearest your bike allows**
and **tell Claude the actual number** (it becomes the cell label — exact value
doesn't matter, just knowing it does). Then the bike holds that power while you
change cadence on Claude's call. You only manage **cadence**; erg holds the power.

Bike erg ≈ **10 W steps**, so these targets land exactly (confirm each as you set it):

| Erg setpoint (report the actual) | Cadence sweep — hold each ~60 s |
|---|---|
| **150 W** | 60 → 80 → 100 rpm |
| **250 W** | 60 → 80 → 100 rpm |
| **330 W** | 60 → 80 → 100 rpm |

9 cells. Constant power × swept cadence is the cleanest torque test — it isolates
the cadence-dependence directly (Stages pinned at the setpoint, Assioma floats to
setpoint ÷ ratio). `08_analyze_grid.py` then says whether a future calibration can
be a short cadence/torque sweep instead of a full grid. Give the bike a couple of
seconds to re-stabilise after each cadence change before the cell "counts".

### Sprints — switch to LEVEL / resistance mode (the 800–1000 W+ corner)
Erg caps you at the setpoint, so flip to resistance mode and set a firm level.
Then **~12 s all-out, ×4**, with ~90 s easy between.
- Probes the extreme top corner the grid can't reach, and stress-tests the decoders
  (12-bit FE-C power field, high crank torque values).
- Don't worry about cadence — just maximum watts. Claude logs the peak each time.

### Cool-down — 2 min easy, then stop. Done.

---

## After the ride (Claude runs)

```bash
python code/scripts/08_analyze_grid.py --input 'code/findings/captures/CAL-multi-*.jsonl'
```
Outputs: the **#7 verdict** (bike-FEC vs crank — pass-through or a factor),
the **ratio surface** (power × cadence), **which dimension drives the offset**
(cadence / power / torque R²), and **grid-design guidance** (how few cells we
actually need). Then commit the capture + write up.

---

## → Then (same session if time/kit allow): BLE evaluation (Session G)

Once the ANT+ work is done, we evaluate the BLE/ESP32 path. Full spec:
`code/findings/session-G-ble-capture-spec.md`. Sequence (mode-exclusive):
1. **While still on ANT+ cranks** — *active BLE recon* (tooling ready):
   ```powershell
   code\.venv-win\Scripts\python.exe code\scripts\06_capture_ble.py --name Stages `
       --duration 120 --control-point request-crank-length,request-sensor-locations,offset-compensation `
       --output code\findings\captures\G-stagesL-ble-recon-$(Get-Date -Format yyyyMMdd-HHmm).jsonl
   ```
   Dumps advert + GATT + reads + CPS notifications, reads the **configured crank length**
   off the BLE side (cross-checks the 165-vs-172.5 fudge!), and does a guarded BLE
   zero-reset (keep cranks STILL for that op).
2. **Flip the bike to "Pair with Bluetooth"** — the **GATE**: does erg still work with
   BLE-paired cranks? Set an erg target, confirm the bike holds power. This is the
   go/no-go for the entire ESP32 direction — no sniffer needed.
3. **The bike's pairing/calibration/bonding** needs either the ESP32 **impersonation
   firmware** (`raedian-probe#1` — nRF dongle is MIA, and ESP32 can't do true passive
   connection sniffing) or a replacement nRF dongle. **Defer to a follow-up** — steps
   1–2 already give the go/no-go plus most of the impersonation surface, and step 3 is
   really the first build step of the actual proxy.

## If something goes wrong
- Stick stuck / "Resource busy" / wrong-state → runbook §3/§2 (kill exact PID via
  `fuser`, relaunch). Claude handles it; you keep spinning.
- FE-C source shows zero records → the wildcard didn't lock; scan for the `FEC ####`
  id and relaunch with `--fec-id <that>`. (Stages + Assioma will still be recording.)
- Anything confusing → just say what you see; Claude's watching the data live.
