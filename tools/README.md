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
```

SSH must be **key-based / non-interactive**. The provisioner + runbook live in
`cauldnz-pos:infra/identities/`; the two-pattern model (per-app read-only / per-machine read+write) is
in [`code/findings/shared-services-adoption.md`](../code/findings/shared-services-adoption.md) §1.

## Linux / WSL (the desk Python tooling)

The ANT+ desk tooling runs on Linux/WSL (native Windows is BLE-only). There, set up the full Python env
per the repo `CLAUDE.md` **Setup**: `python3 -m venv .venv && source .venv/bin/activate && pip install -e
"code[dev,analysis,ble]"`. PlatformIO is the same pin — `pip install platformio==6.1.19` (or via pipx).
