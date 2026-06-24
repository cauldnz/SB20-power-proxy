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

# the build/flash PRE-FLIGHT — run at the desk, before the rider is at the bike:
.\tools\doctor.ps1                            # PASS/FAIL per piece; exits non-zero on any fail
.\tools\doctor.ps1 -BoardIp 192.168.1.165     # also checks the board + prints OTA host-IP candidates
```

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

## Linux / WSL (the desk Python tooling)

The ANT+ desk tooling runs on Linux/WSL (native Windows is BLE-only). There, set up the full Python env
per the repo `CLAUDE.md` **Setup**: `python3 -m venv .venv && source .venv/bin/activate && pip install -e
"code[dev,analysis,ble]"`. PlatformIO is the same pin — `pip install platformio==6.1.19` (or via pipx).
