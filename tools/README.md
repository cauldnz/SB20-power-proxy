# `tools/` — dev-environment provisioning

Make the build/flash toolchain **reproducible across machines** — work moves between the desk and the
bike laptop (different machines). Session 8 (2026-06-25) lost ~30 min mid-ride because a Python 3.14
upgrade had orphaned PlatformIO (empty `~/.platformio`) and there was no host compiler. These scripts +
pins exist so that never recurs: provision to a known-good state, and **verify at the desk** before
relying on a build/flash at the bike.

## Windows (the bike machine)

```powershell
# one-time, or after a Python upgrade — provision the PINNED toolchain:
.\tools\provision-dev-env.ps1                 # creates code\.venv (bleak) + firmware\.venv (PlatformIO)
.\tools\provision-dev-env.ps1 -WarmToolchain  # also pre-downloads the ESP32 toolchain (slow first time)

# the ride-readiness PRE-FLIGHT — run at the desk, before the rider is at the bike.
# Gates BOTH the build/flash toolchain AND the dual-radio capture rig (Npcap + nRF Sniffer extcap +
# dongle; ANT+ stick shared into WSL + a libusb claim). A green doctor means we can build, flash, AND
# capture — exits non-zero on any fail. (Session 9: doctor used to cover only the build toolchain, so a
# green run gave false confidence and the missing nRF/ANT capture rig surfaced at the bike.)
.\tools\doctor.ps1                            # PASS/WARN/FAIL per piece; capture rig gated by default
.\tools\doctor.ps1 -BoardIp 192.168.1.165     # also checks the board + prints OTA host-IP candidates
.\tools\doctor.ps1 -NoCaptureRig              # build/flash toolchain only (skip the capture-rig gate)
.\tools\doctor.ps1 -NoNrf                     # ANT+-only ride: the nRF sniffer is intentionally absent
```

A `[FAIL]` carries its own one-line fix. Build/flash fails -> re-`provision-dev-env.ps1`; capture-rig fails
-> stand the rig up per [Capture rig setup](#capture-rig-setup) below. Don't send the
rider to the bike on a red doctor.

Pinned versions live in [`dev-env.lock`](dev-env.lock) (the session-8 known-good set: **PlatformIO
6.1.19**, **bleak 3.0.2**). Both venvs are repo-local and gitignored (`code/.venv`, `firmware/.venv`).

## Build / flash quick-reference (Windows)

```powershell
# build:
firmware\.venv\Scripts\platformio.exe run -e esp32c3-oled-live -d firmware

# OTA flash — on a multi-NIC laptop espota auto-picks the WRONG host IP (0.0.0.0 -> "No response from
# device"); pass the explicit host LAN IP (one on the board's subnet — doctor.ps1 prints candidates):
firmware\.venv\Scripts\python.exe "$env:USERPROFILE\.platformio\packages\framework-arduinoespressif32\tools\espota.py" `
  -i <board-ip> -I <host-lan-ip> -p 3232 -f firmware\.pio\build\esp32c3-oled-live\firmware.bin -r

# USB flash (fallback): python code\scripts\flash_c3.py --env esp32c3-oled-live --port COM9
```

## Capture rig setup

The standing pre-flight rule (`sessions/PLAYBOOK.md` §pre-flight) is an **always-on dual-radio capture**
for the whole ride: an **nRF BLE sniffer → pcap** and an **ANT+ stick → JSONL**. `doctor.ps1` gates both
(see above); this section is how you **stand the nRF path up at the desk** so the gate goes green. The
ANT+/WSL path is in [`code/findings/wsl-capture-runbook.md`](../code/findings/wsl-capture-runbook.md) — the
notes below only cover what `doctor.ps1` asserts.

> Why this exists: session 9 reached the bike with **Npcap not installed** and the **nRF Sniffer extcap not
> registered** — neither was caught because doctor only checked the build toolchain. Stand the nRF rig up
> **once, at the desk**, and `doctor.ps1` keeps it honest thereafter.

### nRF BLE sniffer (Windows) — Npcap + the nRF Sniffer extcap + dongle firmware

Hardware here: a **Nordic nRF52840 dongle** (`VID_1915`) running the **nRF Sniffer for Bluetooth LE**
firmware, which enumerates as a CDC-ACM **COM port** (e.g. `COM8`). Three pieces, each a doctor check:

1. **Install Npcap** (doctor: `Npcap (wpcap.dll loads)`). tshark cannot capture without it.
   - Easiest: re-run the **Wireshark installer** (4.6.x is already installed here) and tick **Install
     Npcap**, or grab the standalone installer from <https://npcap.com/#download>. **Needs admin + a
     reboot** — this is not scriptable unattended, so do it at the desk.
   - Verify: `& 'C:\Program Files\Wireshark\tshark.exe' -D` lists interfaces with **no** `Unable to load
     Npcap (wpcap.dll)` line.

2. **Install + register the nRF Sniffer for Bluetooth LE extcap** (doctor: `nRF Sniffer extcap`).
   - Download **nRF Sniffer for Bluetooth LE** from Nordic:
     <https://www.nordicsemi.com/Products/Development-tools/nRF-Sniffer-for-Bluetooth-LE/Download>
     (a zip, e.g. `nrf_sniffer_for_bluetooth_le_4.x.x.zip`). It contains an `extcap/` folder
     (`nrf_sniffer_ble.bat`, `nrf_sniffer_ble.py`, `SnifferAPI/`) and the dongle firmware under `hex/`.
   - Find Wireshark's extcap folder: `& 'C:\Program Files\Wireshark\tshark.exe' -G folders` → the
     **Personal Extcap path** (usually `%APPDATA%\Wireshark\extcap`, i.e.
     `C:\Users\<you>\AppData\Roaming\Wireshark\extcap`). Copy the contents of the zip's `extcap/` there.
   - The extcap calls `python`, which needs **pyserial**: ensure a `python` is on `PATH`, then
     `python -m pip install pyserial` (or `pip install -r requirements.txt` from the extcap folder).
   - Register: reopen Wireshark (or **Capture → Refresh Interfaces**). Verify from the CLI:
     `& 'C:\Program Files\Wireshark\tshark.exe' -D` now lists an **`nRF Sniffer for Bluetooth LE COMx`**
     interface. (This is exactly what doctor greps for.)

3. **Confirm the dongle's Sniffer firmware** (doctor: `nRF dongle (COM port)`).
   - The dongle must run the **Sniffer** firmware (a stock/DFU dongle won't sniff). Flash it with **nRF
     Connect for Desktop → Programmer**: put the dongle in bootloader (press its **RESET**, the LED pulses
     red), select `sniffer_nrf52840dongle_nrf52840_4.x.x.hex` from the zip's `hex/`, and **Write**.
   - Confirm: it appears as a **Nordic `VID_1915`** serial port (doctor reports the PID + COMx). A green
     `nRF Sniffer extcap` line above means the extcap is talking to that COM.

**Few-second live test capture** (the playbook also requires this on ride day — doctor proves the rig is
*installed*; a test capture proves it *records*):

```powershell
$ts = 'C:\Program Files\Wireshark\tshark.exe'
& $ts -D                                          # note the number/name of the nRF Sniffer interface
& $ts -i <that-number-or-name> -a duration:3 -w "$env:TEMP\nrf-test.pcapng"
& $ts -r "$env:TEMP\nrf-test.pcapng" | Select-Object -First 5   # non-empty => the dongle is sniffing
```

> nRF gotcha (PLAYBOOK §Passive BLE sniffing): the sniffer can only **follow** a connection whose
> `CONNECT_IND` it caught — start the sniff **before** the target connects, or you get adverts only.

### ANT+ stick (WSL) — usbipd share + a libusb claim

doctor's `ANT+ stick` / `ANT+ WSL libusb claim` checks assert the `0fcf:1008` stick is shared into WSL
**and** that openant can actually claim it. The full bring-up (usbipd, the udev/`systemd` permission
gotcha, the post-attach `chmod 666`, zombie-holder recovery) is in
[`wsl-capture-runbook.md`](../code/findings/wsl-capture-runbook.md). The one that bites every attach:

```powershell
# after `usbipd attach --wsl --busid <id>` the node is root-only (crw-rw-r--); openant gets [Errno 13]:
wsl -d Ubuntu-24.04 -- sudo chmod 666 /dev/bus/usb/001/002    # bus/dev from `lsusb` (runbook §1)
```

doctor attempts the attach itself and, if the node is root-only, prints the exact `chmod` to run (it can't
`sudo` unattended — passwordless sudo isn't configured here). Do the `chmod` at the desk and re-run doctor
until `ANT+ WSL libusb claim` is green.

## Secrets — Infisical → Windows Credential Manager

The build authenticates to the POS Infisical vault with a least-privilege **Universal-Auth** machine
identity (clientId/clientSecret; [cauldnz-pos#1](https://github.com/cauldnz/cauldnz-pos/issues/1)). Keep
that credential in **Windows Credential Manager** (target `SB20/infisical/<identity>`), never in Git.

```powershell
# pull the read-only build identity from the NAS -> Credential Manager:
.\tools\secrets-pull.ps1                      # identity = sb20-power-proxy (default)
.\tools\secrets-pull.ps1 -SelfTest            # parser check only — no SSH, no write

# store a freshly-provisioned dev identity (the provisioner prints the secret ONCE):
ssh unraid "sh /tmp/new-machine-identity.sh chris-p1-sb20 --project sb20-power-proxy --write" |
  .\tools\secrets-pull.ps1 -Identity chris-p1-sb20 -FromStdin

# read it back (the build uses this to `infisical login`):
.\tools\secrets-get.ps1                       # masked summary object
.\tools\secrets-get.ps1 -Field clientSecret   # raw secret (for scripting / the build)

# regenerate the gitignored firmware\ota_secret.h FROM the vault (the firmware compile + flash.ps1 read it):
.\tools\secrets-sync-ota.ps1                  # pull OTA_PASSWORD from Infisical -> firmware\ota_secret.h
.\tools\secrets-sync-ota.ps1 -Check           # drift check (vault vs local header); exit 1 on drift
```

The vault is the source of truth for `OTA_PASSWORD`; `firmware/ota_secret.h` is a **generated, gitignored
artifact**. Rotate = change it in Infisical, re-run `secrets-sync-ota.ps1`, reflash.

SSH must be **key-based / non-interactive**. The provisioner + runbook live in
`cauldnz-pos:infra/identities/`; the two-pattern model (per-app read-only / per-machine read+write) is
in [`code/findings/shared-services-adoption.md`](../code/findings/shared-services-adoption.md) §1.

## Linux / WSL (the desk Python tooling)

The ANT+ desk tooling runs on Linux/WSL (native Windows is BLE-only). There, set up the full Python env
per the repo `CLAUDE.md` **Setup**: `python3 -m venv .venv && source .venv/bin/activate && pip install -e
"code[dev,analysis,ble]"`. PlatformIO is the same pin — `pip install platformio==6.1.19` (or via pipx).
