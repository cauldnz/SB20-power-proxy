# 🚴 Morning Ride Card — Phase 0 capture sessions

One page. The wizard talks you through everything once it's running — this
card is just to get you started and as a backup reference.

**Total time on the bike: ~30 min** (3 min dry run + 15 min ride + optional 10 min).
All power targets are **Stages watts** — the number the bike/app shows you.
(Your FTPs: ~367 W Stages / ~330 W Assioma. The mismatch is the whole point
of this project — ride to the Stages number today.)

---

## Step 1 — Attach the ANT+ stick (Windows, ~30 s)

In an **Administrator PowerShell** (needed again after any reboot):

```powershell
usbipd list                                  # find the 0fcf device's BUSID
usbipd attach --wsl --busid <BUSID>
```

## Step 2 — Start the wizard (WSL / Ubuntu terminal)

```bash
cd ~/local-repos/cauldnz/SB20-power-proxy
source .venv/bin/activate
python code/scripts/ride_wizard.py
```

That's it. The wizard checks the stick, asks two questions (device ID —
default 62144 — and which crank the sticker says it is), offers to launch the
BLE survey window for you, then guides each session with on-screen prompts
and a bell sound. It validates everything automatically between sessions.

> Want to see the flow tonight without the bike?
> `python code/scripts/ride_wizard.py --preview`

## Step 3 — Chat with Claude while you ride

Keep the Claude chat open on your phone or a second screen. Claude can read
your capture files **live off the machine while you ride** — just say
"started C-0" / "started A" / "something looks wrong" and he'll look at the
data as it lands. After the ride, say "sessions done".

---

## What the wizard will have you do

### Session C-0 — zero-reset dry run (3 min, light pedalling)

| When | What |
|------|------|
| 0:00 | Pedal light (~100 W) |
| 1:00 | **Stop pedalling, cranks vertical** |
| 1:15 | **Tap zero-reset in the Stages app** |
| 1:55 | Pedal light again until the end |

The wizard then checks the capture for the calibration response itself and
tells you PASS or INVESTIGATE. Either way, you carry on to Session A.

### Session A — the 15-minute guided ride

| Time | Block | Target (Stages W) | Notes |
|------|-------|-------------------|-------|
| 0–3 min | Warmup | 130–150 | comfy cadence ~85 |
| 3–6 min | Endurance | ~200 | cadence ~90 |
| 6–8 min | Tempo | ~260 | |
| 8–10 min | Threshold | ~330 | hard — ~90% of Stages FTP |
| 10:00–10:30 | Surge | 400+ | 30 s, push! |
| 10:30–11:00 | **Coast** | 0 | stop pedalling completely |
| 11–13 min | Low cadence | ~200 | grind at ~60 rpm |
| 13–15 min | High cadence | ~150 | spin at 95–100 rpm |

(The surge, coast, and cadence extremes aren't training — they exercise the
byte ranges of the protocol: high power, zero power, cadence spread.)

The wizard runs the validator automatically when the ride ends.

### Session B — other crank (optional, 10 min)

Steady ~180–220 W, nothing fancy. The wizard asks if you want it and which
device ID (default 17039) — say no if you're out of time.

---

## If something goes wrong

- **"No ANT+ stick visible"** → Step 1 again (attach doesn't survive reboots).
- **"Almost no data" warnings during a session** → rotate the cranks to wake
  the meter; if it persists, Ctrl-C (the session's data is kept) and message
  Claude.
- **Ctrl-C any time** — never loses data already captured; the wizard carries on.
- **Anything confusing** → just message Claude with what you see on screen.

## Manual BLE survey command (only if auto-launch failed)

In a normal PowerShell window on Windows:

```powershell
cd C:\repos\cauldnz\SB20-power-proxy
code\.venv-win\Scripts\python.exe code\scripts\06_capture_ble.py `
    --adv-only --duration 2700 `
    --output code\findings\captures\ble-adv-survey-$(Get-Date -Format yyyyMMdd-HHmm).jsonl
```

## Bonus (optional, ~1 min of effort): dual-meter data

If your Assiomas are mounted on the SB20 and you have a watch/head unit
handy, record the **Assioma side on the watch during Session A**. Same ride,
both meters → first data for the calibration-model idea (and open question
#7: does the bike scale crank power?).

- Start the watch recording **just before** you press ENTER to start Session A.
- Don't worry about exact sync: the **30 s coast at 10:30** puts a distinctive
  zero-power notch in both recordings — that's our alignment marker.
- Afterwards, export/sync the ride (FIT file) wherever you normally do;
  tell Claude where it lives.

Skip without guilt if it's a faff.
