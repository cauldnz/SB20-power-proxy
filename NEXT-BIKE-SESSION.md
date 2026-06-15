# 🚴 Next Bike Session — open-item closure + Phase 1B pairing test

> One trip to the bike clears every remaining on-bike open item.
> **Do steps 1–6 regardless.** Step 7 (the Phase 1B pairing proof) is **only if the desk build
> (Phase 1A) is already done** — if not, skip it and it rides along on a later visit.
> Full rationale: [`code/findings/forward-plan.md`](code/findings/forward-plan.md) §3.

**~90 min for everything (incl. Phase 1B); ~50 min for the must-dos (1, 2, 5).**
Bring: a **fresh CR2032**, a coin/screwdriver for the battery door, your phone with the
**Stages Cycling** app *and* the **StagesPower** meter app, and the Claude chat open.

**Live support:** I read your capture files off the machine while you ride. Just narrate in chat
("battery done", "started length test", "flipped BLE on", "pairing now") and I'll watch the data land.

---

## Pre-flight (Windows, ~30 s) — only needed for the capture steps

Administrator PowerShell (re-attach after any reboot):
```powershell
usbipd list                          # find the 0fcf device's BUSID
usbipd attach --wsl --busid <BUSID>
```
WSL terminal:
```bash
cd ~/local-repos/cauldnz/SB20-power-proxy && source .venv/bin/activate
```

---

## 1 · Fresh CR2032 in the LEFT crank ⭐ must-do · ~8 min

The L crank (#62144) is at **14%** — replace it before anything else; a dropout mid-capture
wastes the session.

1. Remove the crank / open the battery door on the crank head.
2. Swap in the fresh CR2032 (match polarity, **+** out).
3. Close the door, reinstall the crank (~12–15 Nm).

**✅ Pass:** crank wakes; app shows L-crank battery ≥95%.

---

## 2 · Firmware + right-crank battery check ⭐ must-do · ~5 min

1. Stages Cycling app → Settings/About → note the **SB20 firmware** (expect ~1.12.4+3792).
2. Power Meters tab → right crank (#4963) → note its **battery %** (flag if <20%).

**✅ Pass:** firmware + R-crank level recorded (tell me, I'll log to `decisions.md`).

---

## 3 · Crank-length scaling experiment · ~12 min · *optional, high-value*

Does the **in-meter (StagesPower app)** crank-length setting scale the watts the crank broadcasts?
*(The bike is pass-through, so whatever scales the broadcast also scales what the bike consumes.
Moot for the proxy — the crank leaves the loop — but it explains the 1.085→1.13 ratio history.)*

Start the capture **in the WSL/Ubuntu terminal** (bash):
```bash
python code/scripts/01_capture_stages.py --device-id 62144 --duration 150 \
  --log-channel-events \
  --output "code/findings/captures/LENGTH-test-$(date +%Y%m%d-%H%M%S).jsonl"
```
1. Pedal **steady ~200 W @ ~85 rpm for 60 s** (baseline).
2. Stop. In the **StagesPower meter app** (the meter's own app, *not* the Stages Cycling bike app),
   change crank length **172.5 → 165 mm**; wait ~5 s.
3. Pedal the **same ~200 W @ ~85 rpm for another 60 s**.
4. Let the capture finish.

**✅ Just record what happened — I'll interpret it.** Expected: the crank's broadcast watts drop
~4.3% (165 ÷ 172.5) after the change → confirms the in-meter length scales the broadcast, so the
day-1 (165) → day-2 (172.5) change is what moved the Stages/Assioma ratio 1.085 → 1.13. If the watts
*don't* move, the in-meter length isn't the cause and we look elsewhere. Tell me the file.
**⚠ Set the meter length back to 172.5 afterward.**

---

## 4 · External / Power-Erg probe · ~12 min · *optional*

Could make Phase 1 far simpler if it works.

1. Stages Cycling app → look for **External Power Meter** / **Power-Erg** / "Pair External Meter".
2. If present, pair the bike to the **Assioma** and set an erg target (~250 W); pedal.
3. Does the bike control resistance off the *Assioma* (no crank spoof)?

If erg responds, optionally capture what the bike broadcasts vs consumes (**WSL terminal**):
```bash
python code/scripts/07_capture_multi.py --stages-id 62144 --assioma-id 17039 --fec-id 0 \
  --duration 300 \
  --output "code/findings/captures/EXT-power-erg-$(date +%Y%m%d-%H%M%S).jsonl"
```

**✅ Pass:** external erg works → simpler path exists. **Missing/no response:** crank spoof
confirmed necessary (expected). Either way, tell me what the UI showed.

---

## 5 · Session G Part C — erg-on-BLE GATE ⭐ must-do · ~15 min

**This is the go/no-go for the entire ESP32/BLE direction.** No special kit. *(Independent of the
Phase 1B ANT+ pairing test — whichever way this lands, Phase 1B is unaffected.)*

1. Confirm baseline: in ANT+ mode, set erg ~200 W, bike holds it. ✔
2. Stages Cycling app → Power Meters → flip **"Pair with Bluetooth" ON**; wait ~15 s for "Paired".
3. Set erg targets **200 → 300 → 250 W**, ~20–30 s each; pedal smoothly.
4. Watch: does resistance track each target? Any disconnects?
5. Flip BLE **OFF** again; confirm ANT+ erg still works.

**✅ PASS:** erg responds on BLE cranks, holds within ~10 W, no drops → **Track C (ESP32) viable.**
**❌ FAIL:** erg dead / BLE drops → **ESP32 path closed; ANT+/Pi is the only route.** (Either
result is valuable — tell me which.)

*(Optional parallel BLE log on Windows: `code\.venv-win\Scripts\python.exe code\scripts\06_capture_ble.py --name Stages --duration 300 --output code\findings\captures\G-partC-ble-erg-<ts>.jsonl`)*

---

## 6 · Session G Part A — BLE recon · ~15 min · *only if Part C looked promising*

Captures the crank's BLE surface (the impersonator template). Runs on **native Windows**
(WSL has no Bluetooth) in a normal **PowerShell** window:
```powershell
code\.venv-win\Scripts\python.exe code\scripts\06_capture_ble.py `
  --name 'Stages 62144' --duration 180 `
  --control-point request-crank-length,offset-compensation `
  --output code\findings\captures\G-crankL-ble-recon-$(Get-Date -Format yyyyMMdd-HHmm).jsonl
```
The script runs the control-point ops automatically a few seconds after it connects — there's no
interactive prompt, so **just keep the crank stationary for the first ~20 s** after you start it
(that covers the offset-compensation / zero-reset). It then logs CPS samples for the rest of the run.

**✅ Pass:** GATT dump + crank-length read + offset-compensation response + CPS samples land in the
JSONL. *(We already have the ANT+ offset from C-0, so no ANT+ zero-reset needed here.)*

---

## 7 · Phase 1B — pairing test

> ⚠️ **Only if the desk build (Phase 1A) is done.** `03_static_replay.py` is built at the keyboard
> first; if it doesn't exist yet, **skip this step** — it rides along on a later visit.

**The keystone proof: does the SB20 accept our spoofed crank?**

1. On the proxy machine (WSL, stick attached), run the static replay on a **distinct test id** —
   this avoids any on-air collision with the live crank, so you keep the fresh battery in:
   ```bash
   python code/scripts/03_static_replay.py \
     --input code/findings/captures/A-stagesL-steady-20260614-165737.jsonl \
     --spoof-id 62145
   ```
   *(Fallback if you'd rather test the real id 62144: pull the L-crank battery for the test, then
   reinsert after.)*
2. In the Stages app, pair the SB20 to the new power meter (**62145**).
3. Trigger a **zero-reset** in the app — confirm it's accepted.
4. Set an **erg target** and watch: does the SB20 show the replayed watts, and does **erg react**?

**✅ Pass:** SB20 displays replayed power **and** erg responds → impersonation works → greenlight
Phase 2. Capture a short screen video for `findings/phase-1-demo/`.
**If it sticks:** narrate exactly what you see — most first attempts need one calibration/encoding
iteration (see `forward-plan.md` §3 failure modes).

---

## If anything goes sideways

- **No ANT+ stick visible** → re-run pre-flight `usbipd attach` (doesn't survive reboot).
- **Capture looks empty** → rotate the cranks to wake the meter; if it persists, Ctrl-C (data is
  kept) and message me.
- **Confused by app UI** → screenshot it; I'll read it.
- **Ctrl-C any time** — never loses already-captured data.
