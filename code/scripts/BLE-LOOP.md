# BLE local loop — ESP32 ↔ Python harness

The over-the-air analogue of the in-process BLE loopback (`sb20proxy.ble.LoopbackGatt`): the
ESP32 runs the real dual-role proxy, and the Python harness plays the device on the other end of
each link. Proven on hardware 2026-06-17 (see `code/findings/decisions.md`).

```
  Python fake_meter.py ──BLE──▶ ESP32 BleMeterClient (central)
                                      │  [correction]
                                      ▼
                                ESP32 BleCrankPeripheral (peripheral) ──BLE──▶ Python crank_reader.py
```

Run both at once and you've proved the whole chain: **Python meter → ESP32 (receive + relay) →
Python reader**.

## 0. One-time setup (Windows, the BLE host)

```powershell
cd code
python -m venv .venv
.venv\Scripts\Activate.ps1
pip install -e ".[dev,ble]"
```

`bleak` (central) is all that's needed; the peripheral uses the WinRT GATT-server API directly
(`sb20proxy.ble.winrt_peripheral`). **Do not install `bless`** — it's incompatible with Python 3.13
(see decisions.md). Windows 10+ only for `fake_meter` (WinRT peripheral).

## 1. Flash the live firmware (the REAL dual-role proxy)

The live envs build with `USE_MOCK_METER=0` so the central actually scans for + reads a meter:

```powershell
# first time, over USB (plain board or OLED board):
python -m platformio run -e esp32c3-oled-live -d firmware -t upload      # OLED board
python -m platformio run -e esp32c3-wifi-live -d firmware -t upload       # plain board

# thereafter, over the air (after the board has joined WiFi):
python -m platformio run -e esp32c3-oled-live -d firmware                 # build only
python "$env:USERPROFILE\.platformio\packages\framework-arduinoespressif32\tools\espota.py" `
    -i <board-ip> -p 3232 -f firmware\.pio\build\esp32c3-oled-live\firmware.bin -r
```

The board serves status at `http://<ip>/` (and `http://sb20proxy.local/`) and a dashboard at
`/ui`. The OLED shows the IP + live power/cadence.

## 2. Goal #1 — ESP32 receives power-meter data from Python

```powershell
python scripts\fake_meter.py --watts 177 --cadence 90 --steady
```

Then watch the board pick it up:

```powershell
(Invoke-WebRequest http://sb20proxy.local/ -UseBasicParsing).Content
# -> "source":"connected","src_power_w":177,"src_cadence_rpm":90, ...
```

`src_power_w` / `src_cadence_rpm` are what the ESP32 **received**; `power_w` / `cadence_rpm` are what
it **broadcasts** to the spoofed crank. The `/ui` page shows this as `METER IN → CRANK OUT`.

## 3. Goal #2 — Python receives the ESP32's broadcast

With (or without) `fake_meter` still feeding, read the spoofed crank back:

```powershell
python scripts\crank_reader.py --address 38:44:BE:45:E9:A6 --seconds 12
# -> [n]  177 W   90 rpm   raw=2000b1...
```

Match by **address** when more than one "Stages 62144" is on air (e.g. a second board); otherwise
`--name Stages` is enough. `--zero` also fires a Start Offset Compensation and prints the reply.

## Notes / gotchas

- **Two boards, same name.** Both the live board and any board running the old mock firmware
  advertise `Stages 62144`. The live central *skips* `Stages 62144` advertisers on purpose (never
  read FROM the crank we impersonate), so it will ignore a sibling board and lock onto `fake_meter`
  / a real Assioma. Target `crank_reader` by `--address` to pick a specific board.
- **Clean stop matters.** `fake_meter --duration N` stops advertising cleanly so the board sees the
  meter go away and returns to `searching`. *Force-killing* the process leaves a WinRT zombie
  advertisement; the board's staleness watchdog still recovers, but a clean stop is tidier.
- A single BT adapter can't reliably connect to its own advertisement, so `fake_meter` and
  `crank_reader` on the same PC talk *through the ESP32*, not to each other.
