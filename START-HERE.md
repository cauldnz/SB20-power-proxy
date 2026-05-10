# Start Here — for the Project Owner

**Audience**: you, the SB20 owner, on a Windows machine with WSL2 available, doing the initial Phase 0 work yourself before handing engineering work to Claude Code.

**Goal of this document**: get you from "fresh-cloned repo on Windows" to "Phase 0 captures committed and ready to analyse" without you having to read the eight numbered docs first. The numbered docs are the reference; this is the cookbook.

If you're not the owner — if you've stumbled across this repo and are trying to do something similar with your own SB20 — this document is also for you, with the same content. Substitute your own ANT+ IDs.

---

## Contents

1. [What you're about to do](#what-youre-about-to-do)
2. [Hardware checklist](#hardware-checklist)
3. [One-time setup: Windows + WSL2 + USB passthrough](#one-time-setup-windows--wsl2--usb-passthrough)
4. [Project setup inside WSL](#project-setup-inside-wsl)
5. [Pre-capture sanity checks](#pre-capture-sanity-checks)
6. [Run the Phase 0 capture sessions](#run-the-phase-0-capture-sessions)
7. [Quick analysis: summarise and diff](#quick-analysis-summarise-and-diff)
8. [Optional: spin up the InfluxDB + Grafana stack](#optional-spin-up-the-influxdb--grafana-stack)
9. [Hand off to Claude Code](#hand-off-to-claude-code)
10. [Troubleshooting](#troubleshooting)

---

## What you're about to do

Phase 0 is **diagnostic capture, not coding**. The goal is to record what the SB20's native Stages cranks broadcast over ANT+, what your Assioma DUO broadcasts, and crucially what the SB20 sends *to* a crank during the pairing/zero-reset flow. From those captures we derive a precise specification of what the proxy needs to spoof.

Six capture sessions, plus an optional seventh:

| Session | What | Roughly how long | Why |
|---|---|---|---|
| A | Stages L crank, steady state | 15 min | Baseline of "normal" Stages broadcast |
| B | Stages R crank, steady state | 10 min | The R-crank "half power" mode |
| C | Stages L crank, full pairing + zero-reset | 5 min (mostly setup) | **The most important one.** Captures the SB20's calibration handshake. |
| D | Assioma DUO, steady state | 15 min | Baseline for the input side of the future proxy |
| E | Assioma DUO, calibration triggered from a head unit | 5 min | What does Assioma actually reply to a manual-zero request? |
| F | Failure-mode reproduction (enter Assioma ID into Stages app) | 5 min | The original reported failure, captured |
| G | (optional) BLE-paired cranks | 15 min | Decides whether the QZ-integration path is open |

You can do all of these in one ~90-minute evening if you don't pause for analysis. Realistic flow is to do A and D first (easy baselines), look at the diff, then plan C with extra care.

---

## Hardware checklist

You said you have at least two USB ANT+ sticks — perfect. Here's what each is for:

- **Stick #1**: the workhorse for capture. Subscribes to one device at a time.
- **Stick #2**: optional but very useful for Session C (capture handshake on stick 1 listening to L crank, while stick 2 listens to the SB20's FE-C broadcast simultaneously to cross-correlate timestamps).

Other things you'll want at hand:

- **CR2032 batteries**, fresh — for the Stages cranks during testing. Battery voltage drops cause weirdness that looks like protocol bugs. Measure with a multimeter if you can; <2.7V is suspect.
- **A small notebook or text file** — every capture session you start, write down: session letter, date/time, ANT+ device ID being captured, anything weird that happened during the capture (e.g. "stopped pedalling at 8 min for 30 s"). The script logs the technical bits but not your observations.
- **The 5-digit ANT+ IDs** of your Stages L crank, Stages R crank, and Assioma left pedal. Read off the stickers. Tape a labelled list to the bike if you'll be sessioning over multiple days.
- **Physical access to remove/insert CR2032 batteries** in the Stages cranks during Session F testing. The contact tabs are delicate; remove gently.

---

## One-time setup: Windows + WSL2 + USB passthrough

This is the trickiest part of the whole exercise. After this works once, all subsequent sessions are straightforward.

### Step 1 — Install WSL2 and Ubuntu (skip if already running WSL2)

In an **Administrator PowerShell**:

```powershell
wsl --install -d Ubuntu
```

Reboot if prompted. Open the Ubuntu app from the Start menu, set your Linux username and password.

Verify you're on WSL2 (not WSL1):

```powershell
wsl --list --verbose
# NAME      STATE           VERSION
# Ubuntu    Running         2          ← VERSION must be 2
```

If it says `1`, run `wsl --set-version Ubuntu 2`.

### Step 2 — Install `usbipd-win` for USB passthrough

WSL2 doesn't see USB devices natively. `usbipd-win` is the standard Microsoft-supported tool for binding a Windows USB device to WSL.

In an **Administrator PowerShell**:

```powershell
winget install --interactive --exact dorssel.usbipd-win
```

If you don't have `winget`, the alternative is the `.msi` installer from <https://github.com/dorssel/usbipd-win/releases>.

Reboot or restart Windows Terminal so the `usbipd` command is on `PATH`.

### Step 3 — Bind the ANT+ stick to WSL

Plug in **one** ANT+ stick (we'll do the second one similarly). In an **Administrator PowerShell**:

```powershell
usbipd list
```

You'll see something like:

```
BUSID  VID:PID    DEVICE                                  STATE
2-1    0fcf:1009  ANT USBStick3                           Not shared
3-2    8087:0026  Intel(R) Wireless Bluetooth(R)          Not shared
```

Find the row where VID:PID is `0fcf:1008` (older ANTUSB2) or `0fcf:1009` (newer ANTUSB-m). Note the BUSID — let's say `2-1`.

```powershell
# Bind it (one-time per stick; survives reboots)
usbipd bind --busid 2-1

# Attach it to WSL (needs to be redone after every WSL restart / Windows reboot)
usbipd attach --wsl --busid 2-1
```

Now switch to your **Ubuntu terminal** (inside WSL) and verify:

```bash
lsusb | grep -i dynastream
# expected output:
# Bus 001 Device 002: ID 0fcf:1009 Dynastream Innovations, Inc. ANTUSB-m Stick
```

If you see it, ✅ you're done with the hard part.

For the **second ANT+ stick**, repeat `usbipd list` → `usbipd bind --busid <new>` → `usbipd attach --wsl --busid <new>`. Both sticks should then appear in `lsusb`.

> **After every Windows reboot or `wsl --shutdown`**, you'll need to re-run `usbipd attach --wsl --busid <busid>`. The `bind` step persists; only `attach` is per-session. There are scripts and Task Scheduler tricks to auto-attach on boot if this becomes annoying.

### Step 4 — udev rule inside WSL for non-root USB access

Without this, every Python script needs `sudo`.

```bash
# Install the project's openant (we'll do this properly in the next section,
# but the udev rule needs openant available)
sudo apt update && sudo apt install -y python3 python3-pip python3-venv libusb-1.0-0 libusb-1.0-0-dev
pip install --user openant

# Apply the udev rule
sudo $(python3 -m site --user-base)/bin/python3 -m openant.udev_rules

# Detach + reattach the stick from PowerShell so the new rule applies
# (in PowerShell: usbipd detach --busid 2-1; usbipd attach --wsl --busid 2-1)
```

After re-attaching, you should be able to talk to the stick as your normal user without `sudo`.

---

## Project setup inside WSL

```bash
# Clone the repo somewhere sensible inside WSL (NOT /mnt/c/...; performance is poor there)
cd ~
git clone <YOUR-FORK-OR-THIS-REPO-URL> sb20-power-proxy
cd sb20-power-proxy/code

# Create a virtual env
python3 -m venv .venv
source .venv/bin/activate

# Install with the analysis extras (so summarize/diff/ingest scripts have what they need)
pip install -e ".[dev,analysis]"
```

Verify:

```bash
openant scan --device_type=PowerMeter
# Wake your Assioma by spinning it; you should see its ID appear within ~30s
# (rotate the cranks one-quarter turn; the Assioma's blue LED should light up)
```

If `openant scan` finds your Assioma, ✅ you're ready to capture.

---

## Pre-capture sanity checks

Before kicking off Session A, check these:

- [ ] **Wi-Fi**: ideally on 5 GHz only, or wired Ethernet with 2.4 GHz Wi-Fi off. The 2.4 GHz band is shared by ANT+ and BLE; heavy 2.4 GHz Wi-Fi traffic during capture causes dropouts.
- [ ] **Other ANT+ devices nearby**: Garmin watches, head units, other smart trainers. Power them off or move them out of range. They'll usually "just work" alongside captures but their broadcasts add noise to logs.
- [ ] **Stages cranks**: install fresh CR2032s. Note the date if you want to track battery life.
- [ ] **The two Stages crank ANT+ IDs and the Assioma's left-pedal ANT+ ID** are written down. You'll be passing them to the capture script.
- [ ] **Bike paired and working normally** with its native cranks, via the Stages app, *before* you start fiddling. You want a known-good baseline to capture.
- [ ] **Findings directory exists**: `mkdir -p findings/captures` (relative to project root).

---

## Run the Phase 0 capture sessions

### Session A — Stages L crank, steady state

```bash
cd ~/sb20-power-proxy
python code/scripts/01_capture_stages.py \
    --device-id <STAGES_L_ID> \
    --duration 900 \
    --output findings/captures/A-stagesL-steady-$(date +%Y%m%d-%H%M).jsonl
```

Spin up on the bike and pedal at varied power levels for ~15 minutes. The script logs everything; just ride.

### Session B — Stages R crank, steady state

```bash
python code/scripts/01_capture_stages.py \
    --device-id <STAGES_R_ID> \
    --duration 600 \
    --output findings/captures/B-stagesR-steady-$(date +%Y%m%d-%H%M).jsonl
```

Same bike, capturing the R crank's independent broadcast. R crank in this mode reports half-power; that's expected behaviour.

### Session C — Stages L crank, full pairing + zero-reset (CRITICAL)

This one needs care. You're going to deliberately un-pair and re-pair the L crank with the SB20 while the capture runs, so we record the handshake. Annotate timestamps as you go.

1. **Before starting**: in the Stages Cycling app, navigate to your bike → Devices → Power meters. Note your L crank's current ID (you'll need to re-enter it). Then *clear* the L crank ID and back out of the app.

2. **Start the capture** (15 minutes is plenty):
   ```bash
   python code/scripts/01_capture_stages.py \
       --device-id <STAGES_L_ID> \
       --duration 900 \
       --output findings/captures/C-stagesL-pairing-$(date +%Y%m%d-%H%M).jsonl
   ```

3. **Record timestamps in a notebook**:
   - When `--duration` countdown starts: t=0.
   - "Re-entered L crank ID in app": note the time.
   - "Tapped Pair / app shows pairing complete": note the time.
   - "Started zero-reset": note the time.
   - "Zero-reset complete": note the time.
   - "Started pedalling": note the time.

4. **Re-enter the L crank ID in the Stages app**, do the pairing flow per the app's prompts, do the zero-reset (cranks vertical, tap zero), then pedal for the rest of the 15 minutes.

5. After the capture finishes, save your timestamp notes alongside the JSONL — paste them into `findings/captures/C-stagesL-pairing-<timestamp>-notes.md` so they're committed together.

This capture is the highest-information one in Phase 0. If the capture script ran but `openant` didn't see any acknowledged messages from the SB20 to the crank, that's a sign extended messages weren't enabled — a known limitation we'll resolve in Claude Code's first task.

### Session D — Assioma DUO, steady state

You don't need to be on the SB20 for this; any compatible bike (or a stationary platform with the Assioma "woken up") works.

```bash
python code/scripts/01_capture_stages.py \
    --device-id <ASSIOMA_LEFT_ID> \
    --duration 900 \
    --output findings/captures/D-assioma-steady-$(date +%Y%m%d-%H%M).jsonl
```

The script is named `01_capture_stages.py` but it works for any standard ANT+ Bike Power device. (There's also `02_capture_assioma.py` which is a thin wrapper if you prefer the named version.)

### Session E — Assioma DUO, calibration from a head unit

Trigger a manual zero-offset calibration from a Garmin Edge or any ANT+ head unit that supports it, while running the same capture script. The interesting bit will be in the acknowledged-message stream around the time of the calibration request.

If you don't have a Garmin head unit handy, this session is skippable — Claude Code can write a small calibration-trigger script later, or we can infer the Assioma's calibration response from public Assioma documentation.

### Session F — Failure-mode capture

This is the one where you reproduce the original problem.

1. In the Stages app, **enter the Assioma's left-pedal ANT+ ID** as the SB20's L crank ID. (This is the failing case the project exists to solve.)

2. Start the capture script listening on the Assioma's ID:
   ```bash
   python code/scripts/01_capture_stages.py \
       --device-id <ASSIOMA_LEFT_ID> \
       --duration 600 \
       --output findings/captures/F-failure-mode-$(date +%Y%m%d-%H%M).jsonl
   ```

3. Walk through the SB20 pairing flow normally. Record what happens in the app: error message, timeout, "paired" state but no power, etc. Write it down.

4. When done, `git add -f findings/captures/F-failure-mode-*-notes.md` if you wrote notes.

### Session G — (optional) BLE-paired cranks

This one's not in `01_capture_stages.py` — it requires `bleak` / `pycycling`. Skip it for now if you're tight on time; it can come later. Details and rationale in `10-relationship-to-QZ.md`.

---

## Quick analysis: summarise and diff

After captures are saved, you don't need to wait for Claude Code to look at them. Two scripts give you a fast read:

### Summarise one capture

```bash
python code/scripts/04_summarize_capture.py \
    --input findings/captures/A-stagesL-steady-*.jsonl
```

Prints (or `--out summary.md` writes) a markdown summary: session metadata, page mix with rates, common-page values, power/cadence stats, calibration events.

### Diff two captures

```bash
python code/scripts/05_diff_captures.py \
    --left  findings/captures/A-stagesL-steady-*.jsonl \
    --right findings/captures/D-assioma-steady-*.jsonl \
    --left-label  "Stages L" \
    --right-label "Assioma" \
    > findings/captures/diff-stages-vs-assioma.md
```

The diff is the headline Phase 0 deliverable. The most important row to look at: **`page 0x50 → manufacturer_id`**. If Stages says `69` and Assioma says `263`, that's a smoking gun for hypothesis H2 (the SB20 might be validating manufacturer ID).

---

## Optional: spin up the InfluxDB + Grafana stack

For visual exploration of captures over time. Not required for Phase 0 to succeed, but very useful for spotting patterns.

You need Docker. Two paths:

- **Docker Desktop for Windows** (recommended): install from <https://www.docker.com/products/docker-desktop/>. Enable WSL2 integration in Settings → Resources → WSL Integration. `docker` and `docker compose` then work in your WSL terminal.
- **Docker Engine in WSL**: install per the official Docker docs for Ubuntu, inside your WSL Ubuntu. No GUI but no Docker Desktop licensing concerns.

Then:

```bash
cd ~/sb20-power-proxy/code/docker
cp .env.example .env
# Optionally edit .env to change passwords. For local-only dev the defaults are fine.
docker compose up -d
```

Wait ~30 seconds for InfluxDB and Grafana to start. From your **Windows browser**:

- Grafana: <http://localhost:3000> (admin / admin, change on first login)
- InfluxDB: <http://localhost:8086>

Ingest a capture into InfluxDB:

```bash
cd ~/sb20-power-proxy
export INFLUX_TOKEN=dev-token-change-me   # or your custom value from .env
python code/scripts/03_ingest_jsonl_to_influx.py \
    --input findings/captures/A-stagesL-steady-*.jsonl \
    --source-role stagesL
```

In Grafana, navigate to "SB20 Proxy → Phase 0 Capture Overview" — the dashboard with five panels. Use the capture-id selector at the top to switch between captures.

For more detail on the analysis pipeline, see `09-exploring-captures.md`.

---

## Hand off to Claude Code

Once you have at least sessions A, D, and ideally C committed under `findings/captures/`:

1. Generate a diff for me to look at:
   ```bash
   python code/scripts/05_diff_captures.py \
       --left findings/captures/A-stagesL-steady-*.jsonl \
       --right findings/captures/D-assioma-steady-*.jsonl \
       --left-label "Stages L" --right-label "Assioma" \
       > findings/captures/diff-A-vs-D.md
   ```

2. Skim the diff yourself. Note anything surprising.

3. Open Claude Code in the repo:
   ```bash
   cd ~/sb20-power-proxy
   claude    # or however you launch it on your machine
   ```

4. Paste the prompt from `CLAUDE-CODE-PROMPT.md` as the first message. It primes Claude Code to read the project docs in the right order, look at QZ's `CLAUDE.md` for conventions, and propose improvements to the capture script *before* anything else.

5. Once Claude Code has read everything and summarised back, share your captures (or the diff) and ask it to draft `findings/phase-0-report.md` based on what's actually in the captures vs what we hypothesised.

You can also paste a diff into a normal Claude.ai chat (this conversation, or a new one) without using Claude Code, if you want to discuss findings interactively before kicking off engineering. Either is fine.

---

## Troubleshooting

### `usbipd attach` says "device not found" or attach fails after a reboot

The attach step is per-session. After every Windows reboot or `wsl --shutdown`, re-run:

```powershell
usbipd attach --wsl --busid <BUSID>
```

If the BUSID changed (rare but possible if you've been plugging/unplugging), `usbipd list` again to find the new one.

### `lsusb` in WSL doesn't show the stick

Likely causes, in order:

1. You ran `usbipd bind` but not `usbipd attach`. Run attach.
2. WSL was restarted since the last attach. Re-attach.
3. The stick is still claimed by Windows (an antivirus, ANT Agent, or Garmin Express running on Windows can hold the device). Close those apps and re-attach.

### `openant scan` runs but finds nothing

- Wake the device. Spin the cranks (Stages or Assioma). Some meters sleep aggressively.
- Verify with `lsusb` that the stick is still attached.
- Try unplug → plug → re-bind → re-attach. Sometimes the stick gets into a weird state.
- If on an old WSL kernel: `wsl --update` from PowerShell.

### Capture script runs but logs almost no broadcasts

- The device may not be broadcasting on the device ID you specified. Run `openant scan --auto_create` and check what IDs are actually present.
- Wi-Fi interference. Disable 2.4 GHz Wi-Fi temporarily and try again.
- The stick may be in a weird state from a prior crashed session. `usbipd detach`, `usbipd attach` to reset.

### Stages cranks behave strangely during testing

- Battery first. If you've been removing/inserting CR2032s, contact tabs may be slightly mis-aligned. Gently bend tabs back if needed.
- Note the bike's firmware version (visible in the Stages Cycling app). Stages 3.7.0+ behaves slightly differently from older firmwares — record the version with each capture.

### Docker compose up fails

- `docker info` to verify Docker is actually running. On Windows, Docker Desktop must be open.
- Port conflicts: if 3000 (Grafana) or 8086 (Influx) are in use, edit `docker-compose.yml` to map to different ports.

### "I want to start over"

Captures are immutable; you don't lose anything by starting fresh. Just don't delete old captures from `findings/captures/` — they're history.

```bash
# In WSL: detach and re-attach the stick
# In PowerShell:
usbipd detach --busid <BUSID>
usbipd attach --wsl --busid <BUSID>

# In WSL:
deactivate              # exit the venv
rm -rf code/.venv       # blow away the venv
cd code
python3 -m venv .venv && source .venv/bin/activate
pip install -e ".[dev,analysis]"
```

---

## What success looks like

After a couple of evenings of capture, you have:

- 4–6 JSONL files in `findings/captures/`, each ~hundreds of KB
- A markdown diff between Stages and Assioma showing concrete differences
- Notes on what happened during pairing in Session C
- Notes on what the SB20 displayed during the failure-mode reproduction in Session F

That's enough to answer the central hypothesis. Hand it to Claude Code and we move to Phase 1: building a static replay that the SB20 will accept.
