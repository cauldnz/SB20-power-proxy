# 07 — Hardware and Environment

> ⛔ **SUPERSEDED — historical.** Part of the **pre-pivot brief**, written before the on-bike captures
> and before the firmware existed. Kept for provenance. For the current state read
> **[`PROJECT-MAP.md`](PROJECT-MAP.md)** (what already exists) and
> **[`code/findings/decisions.md`](code/findings/decisions.md)** (what was decided and measured).
> Where this doc disagrees with those, **they win.**

## Hardware shopping list

### Required for Phase 0–2

| Item | Notes | Approx cost |
|------|-------|-------------|
| ANT+ USB stick × 1 (× 2 strongly preferred) | Garmin/Dynastream ANTUSB-m or ANTUSB2; CYCPLUS U1 is a cheaper alternative | £25–35 each |
| Linux laptop or Raspberry Pi 4 | Any modern Linux machine for development. macOS is workable but harder to debug if libusb misbehaves. | (existing or £40+) |
| Spare CR2032 batteries | For the Stages cranks during testing (and to be able to remove them cleanly without breaking contact tabs) | <£5 |
| Pen, paper, masking tape | For writing ANT+ device IDs on visible labels — you will lose track otherwise | — |

### Required for Phase 3+

| Item | Notes |
|------|-------|
| Raspberry Pi 4 (2GB+) | Deployment device for the proxy |
| MicroSD card 16GB+ | Pi storage |
| USB hub (powered, optional) | If using two ANT+ sticks plus other peripherals; the Pi 4's USB ports are usually fine without a hub |
| Pi case with ventilation | Will run 24/7 |

### Already on hand (assumed)

- Stages SB20 smart bike
- Favero Assioma DUO pedals (or compatible)
- Working internet on the Pi/laptop for installing dependencies

### Why two ANT+ sticks

A single stick can run multiple channels concurrently, so in principle one stick suffices for the production proxy. However, during Phase 0:

- One stick captures Stages crank traffic (slave channels listening to L and R)
- The other captures the SB20's perspective or the Assioma in isolation

Having two also lets you run the master spoof on one stick and continue sniffing on the other to validate that what you're transmitting is what you intended — invaluable when Phase 1 inevitably misbehaves the first few times.

## Dev environment setup

### Linux (Ubuntu/Debian)

```bash
# system dependencies
sudo apt update
sudo apt install -y python3 python3-pip python3-venv libusb-1.0-0 libusb-1.0-0-dev

# project setup
cd /path/to/sb20-power-proxy/code
python3 -m venv .venv
source .venv/bin/activate
pip install -e ".[dev]"

# udev rule for ANT+ stick (lets us run without sudo). Written directly —
# openant's `udev_rules` helper copies from a relative resources/ path that
# pip doesn't ship, so it errors.
sudo tee /etc/udev/rules.d/42-ant-usb-sticks.rules >/dev/null <<'RULE'
SUBSYSTEM=="usb", ATTRS{idVendor}=="0fcf", MODE="0666"
RULE
sudo udevadm control --reload-rules && sudo udevadm trigger
# unplug and replug the ANT+ stick after this
```

For optional BLE work (Phase 2 fallback path or Phase 4 features):

```bash
# bluez headers (Linux)
sudo apt install -y bluez bluez-tools libbluetooth-dev libdbus-1-dev pkg-config
pip install -e ".[ble,dev]"
```

Verify:

```bash
# Stick should be visible
lsusb | grep -i dynastream
# expected: Bus 00x Device 00y: ID 0fcf:1009 Dynastream Innovations, Inc.

# openant can find it (wake the meter by spinning it during the scan)
openant scan --device_type=PowerMeter

# The --auto_create flag is also useful — it instantiates the device profile
# automatically when found, so you see decoded power values, not just IDs:
openant scan --auto_create
```

### Phase 0 capture analysis pipeline

Once captures exist, you'll want to look at them. The package includes a small pipeline of scripts and an optional InfluxDB+Grafana stack — see `09-exploring-captures.md` for the full workflow. The short version:

```bash
# (One-off) bring up the InfluxDB + Grafana stack
cd code/docker
cp .env.example .env
docker compose up -d
# Grafana: http://localhost:3000 (admin/admin), InfluxDB: http://localhost:8086

# Install analysis extras
cd .. && pip install -e ".[analysis]"

# After running a capture (say A-stagesL-steady-NNNN.jsonl):
export INFLUXDB_TOKEN=dev-token-change-me  # match docker/.env
python scripts/03_ingest_jsonl_to_influx.py --input findings/captures/A-stagesL-steady-NNNN.jsonl --source-role stagesL
python scripts/04_summarize_capture.py --input findings/captures/A-stagesL-steady-NNNN.jsonl > findings/captures/A-stagesL-summary.md
```

The summary script produces markdown that's easy to share with Claude for collaborative analysis. Grafana is for visual pattern-matching the owner does on their own. The diff script (`05_diff_captures.py`) compares two captures side-by-side and is the core Phase 0 analytical artefact.

JSONL is the canonical record. InfluxDB is derived; you can blow it away and re-ingest from JSONL at any time.

### macOS

```bash
brew install libusb python@3.11
cd /path/to/sb20-power-proxy/code
python3 -m venv .venv
source .venv/bin/activate
pip install -e ".[dev]"
```

No udev rules needed on macOS, but you may need to run with `sudo` for USB access depending on your setup. If `openant scan` hangs without finding the stick, try `sudo` and revisit permissions afterward.

### Windows + WSL2 (recommended for Windows users)

WSL2 with USB passthrough is the cleanest path. The capture scripts are POSIX-y enough that running them natively on Windows is fiddly; running them inside WSL2 Ubuntu is just running them on Linux.

**One-time setup** is covered in detail in [`START-HERE.md`](START-HERE.md#one-time-setup-windows--wsl2--usb-passthrough). Quick summary:

1. Install WSL2 + Ubuntu: `wsl --install -d Ubuntu` from Administrator PowerShell, then `wsl --update` (USB passthrough needs a recent WSL kernel — skip this and `lsusb` comes up empty after attach)
2. Install [`usbipd-win`](https://github.com/dorssel/usbipd-win) for USB passthrough: `winget install --interactive --exact dorssel.usbipd-win`
3. From Administrator PowerShell, with the ANT+ stick plugged in:
   ```powershell
   usbipd list                              # find the busid (look for VID 0fcf)
   usbipd bind --busid <BUSID>              # one-time per stick (persists)
   usbipd attach --wsl --busid <BUSID>      # per-WSL-session (redo after reboot/shutdown)
   ```
4. Inside WSL Ubuntu, verify with `lsusb | grep -i dynastream` — the stick should appear. Install system libs: `sudo apt update && sudo apt install -y python3 python3-venv libusb-1.0-0 libusb-1.0-0-dev usbutils`
5. Write the udev rule so non-root scripts can use the stick, then detach + re-attach (PowerShell) so it applies:
   ```bash
   sudo tee /etc/udev/rules.d/42-ant-usb-sticks.rules >/dev/null <<'RULE'
   SUBSYSTEM=="usb", ATTRS{idVendor}=="0fcf", MODE="0666"
   RULE
   sudo udevadm control --reload-rules
   sudo udevadm trigger --subsystem-match=usb --attr-match=idVendor=0fcf --action=add
   ```
   Do **not** use openant's `python -m openant.udev_rules` helper: it copies a rules file from a relative `resources/` path that pip doesn't ship, so it errors. (In WSL, udev rules only auto-apply if systemd/udev is running; if not, run captures with `sudo $(which python) scripts/01_capture_stages.py ...` instead.)

After that, all the Linux instructions in this doc work as-written.

**Per-session quirk**: `usbipd attach` does not survive Windows reboots or `wsl --shutdown`. Re-attach after each. The `bind` step persists.

**Two ANT+ sticks**: bind and attach each independently. Both will appear in `lsusb`. openant currently picks the first one it finds — selecting between them programmatically is a Phase 0 / Phase 1 task.

**Filesystem performance**: clone the repo into WSL's native filesystem (`~/sb20-power-proxy`), not under `/mnt/c/...`. The Windows-mounted path is much slower for many-file operations.

**Wi-Fi interference still applies**: it's the host machine's 2.4 GHz Wi-Fi, not WSL's. Disable it on the host, or force 5 GHz, during capture sessions if you see drop-outs.

### Windows native (without WSL)

Possible but harder. Follow libusb's Windows driver installation: <https://github.com/libusb/libusb/wiki/Windows#Driver_Installation>. Use Zadig to install the WinUSB driver for the ANT+ stick. Watch out for Garmin Express / ANT Agent which can grab the stick on boot.

We'd recommend WSL2 over native Windows unless you have a specific reason to avoid WSL.

## Raspberry Pi deployment (Phase 3+)

### OS

- Raspberry Pi OS Lite (64-bit), bookworm or later. Headless setup via Pi Imager.
- Configure SSH and Wi-Fi during imaging.

### Initial setup

```bash
sudo apt update && sudo apt full-upgrade -y
sudo apt install -y python3 python3-pip python3-venv libusb-1.0-0 git
git clone <your fork URL> ~/sb20-power-proxy
cd ~/sb20-power-proxy/code
python3 -m venv .venv
source .venv/bin/activate
pip install -e .
# udev rule for ANT+ sticks (write directly; openant's helper isn't pip-shipped)
sudo tee /etc/udev/rules.d/42-ant-usb-sticks.rules >/dev/null <<'RULE'
SUBSYSTEM=="usb", ATTRS{idVendor}=="0fcf", MODE="0666"
RULE
sudo udevadm control --reload-rules && sudo udevadm trigger
```

### systemd unit (template — to be finalised in Phase 3)

```ini
# /etc/systemd/system/sb20proxy.service
[Unit]
Description=SB20 Power Proxy
After=multi-user.target

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi/sb20-power-proxy/code
ExecStart=/home/pi/sb20-power-proxy/code/.venv/bin/python -m sb20proxy.cli run --config /home/pi/sb20-power-proxy/config.toml
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

Enable: `sudo systemctl enable --now sb20proxy.service`. Tail logs: `journalctl -u sb20proxy -f`.

## Test/lab setup tips

### "Bench mode" — the SB20 with cranks isolated

For Phase 1+ testing, you want the SB20 to be in a clean state with no real Stages cranks competing for the channel:

1. Open the Stages app and unpair the L crank (Devices → bike → Power meters → enter empty/different ID).
2. Remove the CR2032 from the L crank — completely. Do not just slide it; pull it out and check the contacts.
3. Remove the CR2032 from the R crank as well — even though we're spoofing L, the R will keep broadcasting on its own ANT+ ID and is one more source of confusion.
4. In the Stages app, set the bike to single-sided mode if Phase 0 confirms this works (see `02-technical-context.md`).
5. Leave the bike powered on. The SB20 will be in "no cranks paired" state and ready to pair to whatever spoofed device we present next.

### Restoring real cranks

For days when you want to actually ride normally:

1. Re-insert CR2032 batteries in both Stages cranks.
2. Open the Stages app, re-enter the original ANT+ IDs.
3. Pair, zero-reset, ride.

Track which mode you're in. Tape a note to the bike if needed.

### Capture identification

Use a clear naming convention: `findings/captures/<session>-<device>-<scenario>-<YYYYMMDD-HHMM>.jsonl`. Examples:
- `findings/captures/A-stagesL-steady-20260510-1830.jsonl`
- `findings/captures/C-stagesL-pairing-20260510-1900.jsonl`
- `findings/captures/D-assioma-steady-20260511-0830.jsonl`

Future-you will thank present-you.

## Notes on running on the SB20 itself

The SB20's internal computer runs its own (unexposed) software. We do not modify it. The proxy hardware (Pi or laptop) sits separately and communicates with the SB20 only via ANT+ broadcasts, exactly as a real crank would. The SB20 doesn't know — and doesn't need to know — that the proxy exists.

This is deliberately the simplest possible deployment: nothing on the bike, everything on a separate device.
