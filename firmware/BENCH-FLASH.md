# Bench flash card — SB20 proxy ESP32-C3

Copy-paste quick start for getting firmware onto the board. Run on the machine the board is
plugged into. Branch: `claude/esp32-bike-powermeter-urnc0c`.

- BLE proxy + captive-portal design notes: `code/findings/decisions.md` (#4 WiFi portal, #5 /log).
- On-air bench checklist (portal / OTA / log): `NEXT-BIKE-SESSION.md` §8.
- Live BLE dev loop (Python ↔ ESP32, no SB20): `code/scripts/BLE-LOOP.md`.
- Ride-day dashboard: `code/scripts/RIDE-WEB.md`.

## 0. Get the code + tools
```bash
git clone https://github.com/cauldnz/SB20-power-proxy.git   # or cd into your clone
cd SB20-power-proxy
git checkout claude/esp32-bike-powermeter-urnc0c
git pull origin claude/esp32-bike-powermeter-urnc0c
pip install platformio
cd firmware
pio test -e native        # host tests, no board — expect 56/56
```

## ⚡ Reliable flash helper (recommended) — `firmware/flash.ps1`

The C3 has two flashing failure modes we hit repeatedly: **weak-signal OTA drops** and the **USB-JTAG
bootloader wedge**. `flash.ps1` encodes the recipes (RSSI pre-flight + OTA auto-retry + reboot verify;
USB auto-detect + bootloader guidance). From `firmware/` (Windows PowerShell):
```powershell
.\flash.ps1                            # build + OTA esp32c3-oled-live -> sb20proxy.local (retries)
.\flash.ps1 -NoBuild                   # just (re)flash the existing build
.\flash.ps1 -Env esp32c3-wifi-live -Target 192.168.1.165
.\flash.ps1 -Mode usb                  # USB flash (auto-detects the COM port)
```
- **OTA drops?** It's the signal. OTA gets unreliable below ~ **−72 dBm** — move the board nearer the
  AP (watch `WiFi -XX` on the OLED, or `/ui`), then re-run. The helper retries automatically.
- **USB "No serial data received" / "Unable to verify flash chip connection"?** The C3 USB-JTAG didn't
  enter the bootloader. Recover: **HOLD BOOT, TAP RESET, RELEASE BOOT**, re-run `-Mode usb`, then
  power-cycle the board. (This is the only reliable path once OTA is unavailable.)

### Pre-ship acceptance gate — `code/scripts/qa_board.py`

Before a beta board ships, run the acceptance test: it flashes (delegating to the hang-resistant
`flash_c3.py`), then verifies off the air that the board advertises as the spoof crank, answers
`/status` healthily, and emits decodable CPS — printing a PASS/FAIL **acceptance card**.
```bash
# full gate: flash + accept (one board on air at a time)
code/.venv/Scripts/python code/scripts/qa_board.py --port COM10 --env esp32c3-oled-live --connect
# validate a board already running, zero flash risk:
code/.venv/Scripts/python code/scripts/qa_board.py --no-flash --connect [--ip <board-ip>]
```
A `-live` board with no meter near it is correctly **silent** on CPS (it only notifies on a real
reading), so "no CPS frames" is non-blocking; only a garbled frame or a missing advert fails the gate.

## 1. Build / flash envs (platformio.ini)

| env | what | upload |
|-----|------|--------|
| `esp32c3-supermini` | BLE-only **mock** (ramps 100–300 W as "Stages 62144") — sanity check | USB |
| `esp32c3-wifi` | WiFi **captive-portal** build (mock meter) — **first flash is this** | USB @115200 |
| `esp32c3-ota` | same binary as `-wifi`, flashed **over the air** | `--upload-port <ip>` |
| `esp32c3-oled` | `-wifi` + 0.42" **OLED** (peff74 board, SSD1306 72×40) | USB |
| `esp32c3-oled-ota` | OLED build, over the air | `--upload-port <ip>` |
| `esp32c3-wifi-live` | **real** dual-role proxy (`USE_MOCK_METER=0`) — reads a live meter | USB |
| `esp32c3-oled-live` | real dual-role + OLED | USB |
| `*-live-ota` | the live builds, over the air | `--upload-port <ip>` |

> First flash of any WiFi build must be over **USB** (`esp32c3-wifi` / `esp32c3-oled`); the `-ota`
> variants are the *same binary* and only change the transport. A fresh board needs the USB flash
> once to lay down our partition table, then you can iterate over the air.

## 2. Typical sequence
```bash
# a) sanity: board advertises "Stages 62144" with a power ramp — pair a phone CPS app
pio run -e esp32c3-supermini -t upload
pio device monitor                                  # Ctrl-C to exit

# b) WiFi captive portal (no wifi_secret.h needed), first flash over USB
pio run -e esp32c3-wifi -t upload                   # use esp32c3-oled if your board has the OLED
pio device monitor                                  # prints: join 'SB20-Setup' -> http://192.168.4.1/
```
Phone: join open **`SB20-Setup`** → setup page auto-pops (else `http://192.168.4.1/`) → pick your
2.4 GHz network → saves to NVS, reboots onto WiFi. Then:
```bash
curl http://<device-ip>/        # status JSON (METER IN -> CRANK OUT)
curl http://<device-ip>/ui      # streaming web dashboard
curl http://<device-ip>/log     # serial-over-HTTP log (serial-flaky workaround)
curl http://<device-ip>/forget  # wipe creds -> reboots into setup
```
```bash
# c) over-the-air thereafter (same binary), and the OTA-survival check:
pio run -e esp32c3-ota -t upload --upload-port <device-ip>
#    -> device should REJOIN with NO re-provisioning (NVS creds survive OTA)

# d) the REAL dual-role proxy (reads a live meter instead of the mock ramp):
pio run -e esp32c3-wifi-live -t upload              # or esp32c3-oled-live
#    validate off-bike against the Python harness — see code/scripts/BLE-LOOP.md
```

## Notes
- **Linux:** C3 native USB = `/dev/ttyACM0`; permission error on upload → `sudo usermod -aG dialout
  $USER` (re-login) or `sudo`. Upload speed is pinned to 115200 (the C3 USB-JTAG drops the stub at
  the 460800 default).
- **First `pio` run** downloads the toolchain/platform/libs — needs internet.
- Driving the **real SB20** over BLE still needs the Session G Part B/C captures from a ride.
