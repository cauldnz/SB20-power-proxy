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
# Gates BOTH the build/flash toolchain AND the dual-radio capture rig (nRF: sniffer-fw dongle + SnifferAPI
# extcap + pyserial, for sniff_ble.py; ANT+: stick shared into WSL + libusb claim). A green doctor means we can build, flash, AND
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

### Flash guards — read this before you flash anything

Both flash scripts refuse before they do damage. Neither refusal is advisory; both cost a real board.

```powershell
firmware\flash.ps1     -Env esp32c3-oled-live-ota      # ESP32: refuses bench / mock / probe / host envs
firmware-nrf\flash.ps1 -Env xiao-sense-s340            # nRF:   refuses a SoftDevice/linker mismatch
```

- **ESP32 — the ride-safety gate.** `...-live` and `...-live-bench` differ by one hyphen, and the flag that
  matters (`METER_MATCH_ANY_CPS`, "pairs with *any* CPS advertiser — DESK ONLY") is up to three `extends`
  hops away, so the two are indistinguishable at the call site. `flash.ps1` resolves the env through
  PlatformIO itself and names the reason. Override with `-Force` for a desk session; never leave such a
  build on a board you ride.
- **nRF — the SoftDevice gate.** An nRF52 app is linked to start immediately above the SoftDevice, so its
  base address is a property of *the board*, not the source. `xiao-sense` assumes **S140** (app @0x26000);
  a board carrying **S340** needs `xiao-sense-s340` (app @0x31000). Flashing the wrong one lands the app
  inside the SoftDevice — it does not fail loudly, it just stops working. `flash.ps1` reads the
  bootloader's `INFO_UF2.TXT` and refuses on mismatch (#298). It stops rather than guessing if it cannot
  identify either side.

Both scripts also reject an unknown env name (listing near-matches), so a stale runbook command fails
loudly instead of silently building something else.

## Capture rig setup

The standing pre-flight rule (`sessions/PLAYBOOK.md` §pre-flight) is an **always-on dual-radio capture**
for the whole ride: an **nRF BLE sniffer → pcap** and an **ANT+ stick → JSONL**. `doctor.ps1` gates both
(see above); this section is how you **stand the nRF path up at the desk** so the gate goes green. The
ANT+/WSL path is in [`code/findings/wsl-capture-runbook.md`](../code/findings/wsl-capture-runbook.md) — the
notes below only cover what `doctor.ps1` asserts.

> Why this exists: session 9 reached the bike with **Npcap not installed** and the **nRF Sniffer extcap not
> registered** — neither was caught because doctor only checked the build toolchain. Stand the nRF rig up
> **once, at the desk**, and `doctor.ps1` keeps it honest thereafter.

### nRF BLE sniffer — the headless `sniff_ble.py` path (primary)

**Canonical doc: [`code/findings/nrf-sniffer.md`](../code/findings/nrf-sniffer.md)** (hardware, the `nrfutil`
DFU firmware-flash recipe, the GUI path) — read it; this is the doctor-gated quick view. We capture BLE
**headless** with [`code/scripts/sniff_ble.py`](../code/scripts/sniff_ble.py): it drives Nordic's `SnifferAPI`
straight over the dongle's serial port (scan → follow a MAC → `.pcap`). **No Npcap, no tshark** — those are
only for the interactive Wireshark-GUI alternative (below). Three pieces, each a `doctor.ps1` check:

1. **Dongle on the sniffer firmware** (doctor: `nRF dongle (sniffer fw 522A)`). A Nordic nRF52840 dongle
   (`VID_1915`) running the **nRF Sniffer for Bluetooth LE** app enumerates as a CDC-ACM COM port with PID
   **`522A`**. PID `C00A` = the nRF-Connect "connectivity" firmware, which **can't** sniff — re-flash via
   `nrfutil` DFU per `nrf-sniffer.md` §"How it was flashed". *(Currently the firmware IS flashed; it's the
   host side below that's missing.)*
2. **Stage the `SnifferAPI` extcap** (doctor: `nRF SnifferAPI extcap staged`). `sniff_ble.py` borrows Nordic's
   `SnifferAPI` from the Wireshark extcap dir. Get the **matched v4.1.1** extcap from
   `github.com/makerdiary/nrf52840-mdk-usb-dongle` (the extcap version must match the firmware) and copy its
   `extcap/` contents (`SnifferAPI/`, `nrf_sniffer_ble.py`, `.bat`) into **`%APPDATA%\Wireshark\extcap`**.
   Verify: `Test-Path "$env:APPDATA\Wireshark\extcap\SnifferAPI"`.
3. **pyserial in the BLE venv** (doctor: `pyserial (for sniff_ble.py)`):
   `code\.venv\Scripts\python.exe -m pip install pyserial`.

Then confirm + capture (the playbook's few-second live test — doctor proves it's *installed*, a scan proves it
*records*):

```powershell
code\.venv\Scripts\python.exe code\scripts\sniff_ble.py --scan-only --duration 10   # lists the dongle port + advertisers
# follow the SB20 for a session — START BEFORE the app connects (else adverts only):
code\.venv\Scripts\python.exe code\scripts\sniff_ble.py --device E4:AA:5A:D6:0E:D4 --duration 420 `
  --output code\findings\captures\SNIFF-sb20-app-<stamp>.pcap
```

> nRF gotcha (PLAYBOOK §Passive BLE sniffing): the sniffer can only **follow** a connection whose
> `CONNECT_IND` it caught — start the sniff **before** the target connects, or you get adverts only.

**Interactive alternative (Wireshark GUI):** to watch live in Wireshark instead, that path *does* need
**Npcap** installed + the extcap **registered** (`tshark -D` lists `nRF Sniffer for Bluetooth LE COMx`). See
`nrf-sniffer.md` §"Using it … Wireshark GUI". `doctor.ps1` does **not** gate this path.

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
