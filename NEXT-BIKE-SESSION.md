# 🚴 Next Bike Session — open-item closure + Phase 1B pairing test

> One trip to the bike clears every remaining on-bike open item.
> **Plan to do 1, 2, 5, 6, 7.** The two gates are **5** (erg-on-BLE go/no-go) and **7** (Phase 1B
> spoof-pairing proof, now in scope — the Phase 1A desk build is done). **6** (BLE recon) is a
> do-regardless capture. **3** is optional/high-value; **4** is a known dead end (skip — see below).
> Full rationale: [`code/findings/forward-plan.md`](code/findings/forward-plan.md) §3.

**~90 min for everything; ~50 min for the core (1, 2, 5, then 7).**
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

## Desk pre-flight (env + sanity — verified hardware-free, ~2 min)

Run once on the bench machine. All of this passes with **no stick / SB20 / ESP32** — do it before
you depend on any tool mid-session:

```bash
cd code && source .venv/bin/activate
pip install -e ".[dev,analysis,ble]"   # [ble] (bleak) is REQUIRED for step 6's BLE capture
pytest -q                              # expect: 75 passed, 1 skipped
# keystone desk tool — software loopback proves the replay path end-to-end (no hardware):
python scripts/03_static_replay.py --radio loopback --duration 3 \
  --input findings/captures/A-stagesL-steady-20260614-165737.jsonl
#   -> "[replay] PASS: twin saw spoofed Stages power"
```

If you'll flash / observe the **ESP32** this session (WiFi setup, OTA, `/log`, or the BLE path):
```bash
cd firmware
pio test -e native      # expect: 29/29
pio run -e esp32c3-ota  # builds with NO wifi_secret.h — creds come from the captive portal
```

**Gotchas confirmed this session:** `06_capture_ble.py` exits immediately without **bleak**
(`pip install -e "code/.[ble]"`); the ANT+ stick (`--radio ant`) needs the udev rule from
`CLAUDE.md`; PlatformIO needs network on first run to fetch the `native`/`espressif32` platforms.

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

## 4 · External / Power-Erg probe · ~5 min · *low priority — already tried, didn't work*

> ⚠️ **Owner has already attempted this** — entering the **Assioma ANT+ id directly into the Stages
> Cycling app** did **not** give external-meter erg. So the simple path is, as far as we know,
> closed and the **crank spoof is confirmed necessary**. Only revisit if a Stages app update has
> added a real "External Power Meter / Power-Erg" pairing option that's distinct from typing in an
> id. If you do retry, look specifically for a *dedicated external-meter pairing UI*, not the normal
> crank-id fields.

1. Stages Cycling app → look for a **dedicated External Power Meter / Power-Erg / "Pair External
   Meter"** option (distinct from the crank-id entry that already failed).
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

## 6 · Session G Part A — BLE recon · ~15 min · *do regardless — capture it for the record*

Captures the crank's BLE surface (the impersonator template) — **worth documenting whatever Part C
decides**, and it's **independent of Part C**: a 2026-06-15 finding confirmed the **Stages crank is
reachable over BLE while in ANT+ mode** (target it by name `Stages 62144`, not the generic "Stages"
filter, which hits the bike's FTMS device). So no BLE-crank mode needed for this. Runs on **native
Windows** (WSL has no Bluetooth) in a normal **PowerShell** window:
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

> ⚠️ **Only if the desk build (Phase 1A) is done.** `03_static_replay.py` exists and its loopback
> passes, so this is **in scope** for the session.

**The keystone proof: does the SB20 accept our spoofed crank?**

> 🔁 **WRITE DOWN THE CURRENT PAIRING BEFORE YOU CHANGE ANYTHING — restore it afterward:**
> **Stages `62144` (L) : `4963` (R)** · crank length **165 mm** · zero-offset **L 903 / R 951**.
> The app pairs the crankset as a **linked L/R pair**, so it asks for *both* IDs; only the **L** id
> changes for this test, **R stays `4963`** (the real right crank stays in and keeps broadcasting).

1. On the proxy machine (WSL, stick attached), run the static replay on a **distinct test id** for
   the **left** crank — this avoids any on-air collision with the live L crank, so you keep the
   fresh battery in:
   ```bash
   python code/scripts/03_static_replay.py \
     --input code/findings/captures/A-stagesL-steady-20260614-165737.jsonl \
     --spoof-id 62145
   ```
2. In the Stages app, set the crankset IDs to **L `62145` (our spoof) : R `4963` (unchanged)** and
   pair. The bike now listens for our spoofed L master; the real L (`62144`) keeps broadcasting but
   the bike ignores it.
   - **If the app rejects an unmatched L id** (insists on a registered linked pair): fall back to
     spoofing the **real** id — `--spoof-id 62144`, pull the **L-crank battery** for the test so the
     real and spoof don't collide (R `4963` stays in), then reinsert + re-pair the real pair after.
3. Trigger a **zero-reset** in the app — confirm it's accepted.
4. Set an **erg target** and watch: does the SB20 show the replayed watts, and does **erg react**?
5. **Restore** the pairing to **`62144` : `4963`** (length 165, offsets 903/951) and confirm normal
   operation before you leave.

**✅ Pass:** SB20 displays replayed power **and** erg responds → impersonation works → greenlight
Phase 2. Capture a short screen video for `findings/phase-1-demo/`.
**If it sticks:** narrate exactly what you see — most first attempts need one calibration/encoding
iteration (see `forward-plan.md` §3 failure modes).

---

## 8 · WiFi captive-portal bench test · ~10 min · *optional, desk-testable*

> No SB20 or bike needed — just the ESP32-C3 + a phone. Verifies WiFi provisioning end-to-end
> (the host-side logic is already CI-green; this is the on-air half).

1. **Flash WiFi build with NVS empty.** `cd firmware && pio run -e esp32c3-ota -t upload` over USB.
   (First time, or after a `GET /forget` / NVS erase.) No `wifi_secret.h` required.
2. **Portal comes up.** Serial prints `SETUP: join WiFi network 'SB20-Setup' ...`. On a phone, join
   the open **`SB20-Setup`** AP — the setup page should **auto-pop** (captive-portal detection). If
   not, browse to `http://192.168.4.1/`.
   - **Diagnostic log (the serial-flaky workaround):** the setup page links to `/log`; open
     `http://192.168.4.1/log` to read the device's recent log lines over HTTP instead of serial.
     Toggle with `/log/off` · `/log/on` (persisted). This is the reliable window into setup.
3. **Provision.** Pick your 2.4 GHz network from the list, enter the password, **Save & Connect**.
   Device replies "Saved … restart" and reboots.
4. **Joins as station.** Serial prints `connected; status at http://<ip>/`. Confirm
   `curl http://<ip>/` returns the status JSON, and `curl http://<ip>/log` shows this boot's join
   log (`joining '<ssid>'` → `connected ...`). The ring buffer is RAM (fresh each boot); what
   *persists* across the reboot is the on/off **toggle** (NVS, default on) — so `/log` is reachable
   here without re-enabling it.
5. **Survives OTA** (the important one). With the device joined from step 4, flash a *new* build
   over the air — `pio run -e esp32c3-ota -t upload --upload-port <ip>` — and confirm it **reboots
   and rejoins the same network with no re-provisioning** (serial shows `joining '<your-ssid>'`,
   not the `SB20-Setup` portal). This proves the `nvs` creds survive an OTA app-slot swap.
   *(Bonus: reflash the BLE-only `esp32c3-supermini` over USB and back — creds should still be
   there, since both partition tables keep `nvs` at 0x9000.)*
6. **Re-provision path.** `curl http://<ip>/forget` → device wipes creds and reboots back into the
   portal. (Same fallback fires automatically if the saved network is unreachable.)

**✅ Pass:** portal auto-pops, creds persist across reboot **and OTA**, `/` serves status, `/forget`
returns to setup. **If the page doesn't auto-pop** on Android, opening any `http://` URL should 302
to setup.

---

## If anything goes sideways

- **No ANT+ stick visible** → re-run pre-flight `usbipd attach` (doesn't survive reboot).
- **Capture looks empty** → rotate the cranks to wake the meter; if it persists, Ctrl-C (data is
  kept) and message me.
- **Confused by app UI** → screenshot it; I'll read it.
- **Ctrl-C any time** — never loses already-captured data.
