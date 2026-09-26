# Bench UI pass — every screen and card, no rider

**Status: IN PROGRESS (started 2026-09-25)** · tracked in [`README.md`](README.md) · drives from
[`../docs/ui-feature-map.md`](../docs/ui-feature-map.md) §4 (the test map) · issue #345.
**Where:** the desk, with the bench camera, the serial console, a phone on the bench WiFi, `fake_meter.py`,
the FTMS trainer sim board and `crank_reader.py`. **Who:** the agent runs every step; the owner is
needed only for the QR-scan and phone-in-hand checks. **Budget:** two to three desk hours per board.

> Purpose: burn down every "never seen on a panel" cell of the map before session 14 spends rider
> time on it, and confirm or refute the defects the map found by reading (#346, #347, the Guition
> layout). A bench row that fails becomes an issue; a bench row that passes moves the map's Verified
> column to today's date.

> **Turnkey (2026-09-24 rewrite):** every command below was read off the script's argparse, the env
> table in `firmware/platformio.ini`, or `lcdSerialConsole` in `firmware/src/main.cpp` — nothing is
> invented. Where no tool exists the step says **GAP** and what stands in. Sources: [`BOARDS.md`](../BOARDS.md)
> (ports, MACs, the camera recipe), [`firmware/BENCH-FLASH.md`](../firmware/BENCH-FLASH.md) (envs, portal),
> [`tools/README.md`](../tools/README.md) (the toolchain paths), [`code/scripts/BLE-LOOP.md`](../code/scripts/BLE-LOOP.md)
> (the fake-meter ↔ crank-reader loop), [`web/HTTP-API.md`](../web/HTTP-API.md) (the JSON routes),
> [`docs/system-reference.md`](../docs/system-reference.md) §2 (the fleet-identity table) and §8e (the bench topology),
> [`code/findings/cyd-board.md`](../code/findings/cyd-board.md) and [`code/findings/guition-board.md`](../code/findings/guition-board.md)
> (board flash/serial gotchas).

## 0. Rig (assert before starting)

All commands are **Windows PowerShell from the repo root**. `pio` below means
`firmware\.venv\Scripts\platformio.exe` (provisioned by `tools\provision-dev-env.ps1`; the LVGL envs
must build from the short junction `C:\sbw` — `cmd /c mklink /J C:\sbw <repo-root>` — or LVGL's include
chains cross `MAX_PATH`, per `BOARDS.md`). Fill these in once per board and paste them ahead of every block:

```powershell
$BOARD = "sb20proxy-guition.local"   # sb20proxy-cyd.local | sb20proxy.local (the C3)
$COM   = "COM14"                      # the board under test's USB serial (see 0a)
$SIM   = "COM10"                      # the trainer-sim C3's USB serial
# Identity: do NOT assign one. Since #354 a board DERIVES its own from its MAC — blank the
# stored name (`curl.exe -s -X POST http://$BOARD/config -d "out_name="`) and it comes up as
# Guition `Stages 99744` / CYD `Stages 92364`, with `identity_default:true` in /status.
# Verified on the Guition 2026-09-25. Leave the C3 ride board's identity to session 14 G0.
$ID    = "Stages 99744"               # the Guition's DERIVED identity (read it, do not set it)
$PY    = "code\.venv\Scripts\python.exe"
$OUT   = "sessions\bench-out\$(Get-Date -Format yyyyMMdd)"   # evidence folder (gitignore it or commit the frames you cite)
New-Item -ItemType Directory -Force $OUT | Out-Null
```

### 0a. Which board is on which port

COM numbers re-enumerate on replug (`BOARDS.md`): identify by **USB VID:PID**, confirm by MAC.

| Board | VID:PID | Console | Last seen | MAC / SSID |
|---|---|---|---|---|
| Guition JC3248W535 (320×480) | `303A:1001` native USB CDC | `SCREEN`/`TAP`/`STATE` console | `COM14` (2026-09-25) | base `28:84:85:49:4D:20` · **`Setup-4D20`** (the SSID comes from the STATION MAC; `4D:21` is the BLE crank address — corrected 2026-09-25) |
| CYD ESP32-2432S028R (240×320) | `1A86:7523` CH340 UART | console + the CYD touch-cal commands | `COM17` (cyd-board.md) / `COM12` (2026-07-11) | STA `…CC:8C` · `Setup-CC8C` |
| C3 + 0.96" OLED (the ride board) | `303A:1001` | **no console** (OLED builds have no `USE_LCD`); its native USB-JTAG does not deliver `Serial` | `COM13` (2026-07-11) | `10:B4:1D:BA:C9:0C` · ⛔ WiFi RF dead — display-only spare |
| C3 + 0.42" OLED (peff74, spare → **the trainer-sim host**) | `303A:1001` | the sim's log | `COM5` (2026-07-11) | `38:44:BE:45:E9:A4` · `Setup-E9A4` |

```powershell
# list every COM port with its VID:PID (the doctor.ps1 idiom):
Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match '\(COM\d+\)' } | Select-Object Name, DeviceID
# confirm a board by MAC / chip family (the port must be free — close any console first):
$PY -m esptool --port $COM read-mac
$PY -m esptool --port $COM chip-id
```

> **The C3 OLED ride board in `BOARDS.md` (`sb20proxy.local`, `192.168.1.165`) is the one that rides;**
> the 0.96" C3 listed above is the WiFi-dead spare. Use whichever C3 currently answers `sb20proxy.local`
> for the OLED rows and confirm its MAC with `read-mac` before flashing anything.

### 0b. Rig checklist — one command per line

- [ ] **Board under test on the bench WiFi, `/status` reachable by hostname; build SHA noted.**
  ```powershell
  curl.exe -s http://$BOARD/status        # keys: fw, version, build_sha, build_time, source (mock|connected|searching),
                                          #       src_name, identity, mode (spoof|corrector), forwarded, src_power_w,
                                          #       src_cadence_rpm, src_balance_pct, power_w, cadence_rpm, balance_pct, rssi, heap, ms
  git rev-parse --short HEAD               # must equal build_sha (stamped by firmware/scripts/build_version.py at compile;
                                          # the build log prints "[build_version] SB20_BUILD_SHA=…")
  curl.exe -s http://$BOARD/log/on ; curl.exe -s http://$BOARD/log   # /log answers 403 until /log/on; /log/off when done
  ```
  Not provisioned yet? The portal flow is `BENCH-FLASH.md` §2: join `Setup-XXXX` (WPA2; the PIN is on the
  LCD/OLED, screenless boards use `sb20setup`), the setup page pops (else `http://192.168.4.1/`), pick the
  bench 2.4 GHz network, the board reboots onto it and `/log` shows `[wifi] connected; status at http://<ip>/`.
- [ ] **The bench build is on the board** (the WinRT fake meter advertises under the PC's *computer name*, so
  the ride build's `ASSIOMA` name filter rejects it — `fake_meter.py` docstring, decisions.md 2026-06-22).
  ```powershell
  # Guition — USB flash (flash_s3.py writes only the code regions; NVS survives; --erase-nvs for a clean slate):
  pio run -e esp32-guition-live-bench -d firmware
  $PY code\scripts\flash_s3.py --env esp32-guition-live-bench --port $COM [--verify-ble "$ID"]
  # CYD — classic ESP32 (CH340, no USB-JTAG stub bug; no OTA env exists for it):
  pio run -e esp32cyd-live-bench -d firmware -t upload --upload-port $COM
  # C3 OLED rows — there is NO esp32c3-oled-live-bench env (GAP). Keep the ride build and point its name
  # filter at the PC instead (MeterMatch.h: a named advertiser matches by substring; system-reference §8e):
  firmware\flash.ps1 -Env esp32c3-oled-live-ota            # OTA the ride build (refuses bench envs without -Force)
  curl.exe -s -X POST http://$BOARD/config -d "src_filter=$env:COMPUTERNAME"   # merges + reboots
  # ... and restore it before the board goes near a bike:
  curl.exe -s -X POST http://$BOARD/config -d "src_filter=ASSIOMA"
  ```
  **`-bench` = `METER_MATCH_ANY_CPS=1` = DESK ONLY** (it would latch onto a stranger's meter). `firmware\flash.ps1`
  refuses bench/mock envs without `-Force`; `flash_s3.py` and `flash_c3.py` have **no guard** — reflash the
  ride build (`esp32-guition-live`, `esp32cyd-live`) before the board leaves the bench.
- [ ] **Unique spoof identity per the fleet table (`docs/system-reference.md` §2); no other board powered.**
  ```powershell
  curl.exe -s -X POST http://$BOARD/config -d "out_name=$ID"     # persist + reboot (POST /config merges; curl sends no Origin, so CSRF passes)
  curl.exe -s http://$BOARD/status | Select-String identity
  curl.exe -s http://$BOARD/scan       # every advertiser this board sees (name, rssi, cps, ftms, crank) — proves what else is on air
  $PY code\scripts\qa_board.py --no-flash --connect --spoof-name "$ID" --ip <board-ip>   # the acceptance card: advert + /status + CPS frames
  ```
  Fleet table today: C3 `Stages 62145` (confirm), Guition *assign, not 62144/62145*, CYD defaults to `Stages 62144`
  (collides with a real crank — change it). Second witness for what's on air, if the nRF dongle is plugged in:
  `$PY code\scripts\sniff_ble.py --scan-only --duration 10`.
- [ ] **Bench camera framed on the panel (`BOARDS.md` recipe).** UC70 (`0AC8:3420`), ffmpeg at `C:\ProgramData\chocolatey\bin\ffmpeg.exe`.
  ```powershell
  ffmpeg -f dshow -rtbufsize 512M -video_size 3840x2160 -i video="UC70" -t 3 -update 1 -q:v 2 -y "$OUT\frame.jpg"
  # crop + rotate per board (the CYD and Guition sit rotated opposite ways: transpose=1 vs transpose=2 — pick the one that reads upright):
  ffmpeg -f dshow -rtbufsize 512M -video_size 3840x2160 -i video="UC70" -t 3 -update 1 -q:v 2 -vf "crop=w:h:x:y,transpose=2" -y "$OUT\frame.jpg"
  ```
  `-t 3` lets auto-exposure settle; `-update 1` keeps the last frame. **GAP:** the crop rectangle per board is
  recorded nowhere — find it once with an uncropped frame and write it into §2 so the next pass copies it.
- [ ] **The serial console answers.** LCD builds only (`lcdSerialConsole` is under `#if USE_LCD`); 115200 8N1,
  newline-terminated, ≤ 64 chars a line. One process per COM port — close the console before esptool/flashing.
  ```powershell
  pio device monitor -p $COM -b 115200        # interactive; type STATE ⏎ — the scripted path is bench_s3.py's S3Bench class (below)
  ```
  `STATE` → `{"screen":N,"details":0|1,"power":W,"cad":rpm,"wk_loaded":0|1,"wk_running":0|1,"wk_target":W,"touch":0|1}`;
  `screen` is the `LcdScreen` enum **0 Ride · 1 Setup · 2 More · 3 Workout · 4 Calibrate · 5 Compare**; `touch=1` = the
  controller ACKs. The LVGL task also prints `[lvgl] alive scr=N heap=… stack=…` every 5 s.
  Full command set (main.cpp): `SCREEN` · `TAP x y` · `STATE` · `TRAINER <name>` (sets `trainerNameFilter` + **reboots**;
  bare `TRAINER` clears it) · `CMP` (jump to Compare) · `CMPRATIO <n>` (simulated meter B = A × n/1000; default 1000) ·
  `WATTY` · `WBAUTO` · `WBPWR <w>` · `WBSTATE` · `WBSHOT` · CYD-only: `CALTOUCH` · `RAWTAP rx ry` · `CALINFO` · `RAWZ` ·
  `CALCLEAR` · `INV 0|1` · `MAD <hex>`.
- [ ] **A `SCREEN` dump arrives.** On every LCD board (all LVGL) `SCREEN` streams one `<AREA x1 y1 x2 y2 <base64 RGB565-LE>>`
  block per flush area and ends with `<DUMPDONE>` (decisions.md 2026-07-03; guition-board.md). **GAP: no committed
  decoder** — the reassembler was a scratchpad script, and `bench_s3.py`'s `screenshot()` still waits for the
  pre-LVGL `<BMP…BMP>` wrapper, so it reports "no BMP frame" on these boards. Until one lands, save the raw stream
  as evidence and let the camera be the picture:
  ```powershell
  # raw capture of one SCREEN dump (pyserial is in code\.venv — doctor.ps1 gates it). CYD: clear DTR/RTS before open or the CH340 resets the board.
  $PY -c "import serial,sys; s=serial.Serial(); s.port='$COM'; s.baudrate=115200; s.timeout=5; s.dtr=s.rts=False; s.open(); s.write(b'SCREEN\n'); b=b''
  while b'<DUMPDONE>' not in b: c=s.read(65536); b+=c;  (not c) and sys.exit('timeout')
  open(r'$OUT\screen.txt','wb').write(b); print(len(b),'bytes', b.count(b'<AREA'),'areas')"
  ```
  A dump is "good" when it ends in `<DUMPDONE>` and has ≥ 1 `<AREA` block whose rectangle is the whole panel
  (Guition full-refresh: `<AREA 0 0 319 479 …>`).
- [ ] **`fake_meter.py` streaming; the board attaches.**
  ```powershell
  $PY code\scripts\fake_meter.py --watts 150 --cadence 85                       # triangle ramp around 150 W @ 1 Hz
  $PY code\scripts\fake_meter.py --watts 200 --steady --balance 48 --hz 2       # constant 200 W, 48%L, 2 Hz
  $PY code\scripts\fake_meter.py --watts 100 --duration 600                     # clean stop after 600 s (a force-kill leaves a WinRT zombie advert)
  ```
  Its own console prints `[subscribers=1] connected` and `tx 150 W … subs=1` once the board subscribes; the board's
  `/status` flips `source` to `connected` (`src_power_w` follows) and `/log` shows `[meter] found '<PC-name>' <addr>`.
  Windows 10+ only (WinRT peripheral). One BT adapter can't talk to its own advert, so `fake_meter` and
  `crank_reader` on the same PC only ever meet *through* the board (BLE-LOOP.md).
- [ ] **The FTMS trainer sim on its own board, name known.** Env `esp32c3-ftms-server` (its own `main`, no WiFi/OLED),
  advertises **`SB20-FTMS-Server`** (FTMS `0x1826`), streams a 100–300 W Indoor-Bike-Data ramp at 2 Hz, and logs
  `[ftms-server] controlled=… started=… hasTarget=… target=…W power=…W` once a second on its serial (115200).
  ```powershell
  pio run -e esp32c3-ftms-server -d firmware
  $PY code\scripts\flash_c3.py --env esp32c3-ftms-server --port $SIM [--verify-ble SB20-FTMS-Server]
  pio device monitor -p $SIM -b 115200                    # the sim log is step 4's oracle — leave it open in its own terminal
  # prove it answers from the host before involving the board (host = FTMS controller: Request Control → Start → Set Target):
  $PY code\scripts\ftms_hw_loop.py --mode server --set 225 --output "$OUT\ftms-hwloop.jsonl"   # NOT under code\findings\captures (the index guard would fail on an unlisted file)
  ```
  Host the sim on the 0.42" spare C3, not the 0.96" board whose RF is dead. Tell the board under test its name
  either on the Setup screen (step 3) or over serial: `TRAINER SB20-FTMS-Server` (reboots).
- [ ] **A phone joined to the bench WiFi with `/app` open; browser console visible** (USB debugging, or a desktop
  browser standing in). **GAP:** `WifiLink` logs no client IPs and `/status` has no client list, so the proof is
  the phone itself: a screenshot of `http://$BOARD/app` whose hero watts equal `curl /status`'s `power_w` at that
  second (the SPA polls `/status` at 1 Hz).
- [ ] **`route_baseline.py capture` taken as the "before" oracle.**
  ```powershell
  $PY code\scripts\route_baseline.py capture $BOARD -o "$OUT\routes-before.json"   # 57 vectors; exits 2 if the harness mutated the board
  ```
  It runs `/log/on … /log/off` and `/stats/reset` itself, never POSTs `/forget` or `/wifi/off`, and refuses to POST an
  empty curve. `GET /log`, `/setup`, `/stats`, `/diag` are compared by structure; a diff confined to those between a
  cold and a warm boot is *unproven*, not a regression (its docstring).
- [ ] The map's Decision column (filled 2026-09-24) is the oracle: a row placed *off* a surface is checked
      there as "not offered" (a removal check), a row placed *on* a surface it lacks is recorded as a gap, not a fail.

### 0c. TAP coordinates per board (derived from `firmware/src/ui/LvglUi.cpp`; verify each with `STATE`)

`W`×`H` = panel: Guition 320×480, CYD 240×320, S3 172×320. Only the S3 numbers have been proven on a panel
(`bench_s3.py`); the others are the same layout code evaluated at that width — **GAP: no board-agnostic walker
exists**, so after every `TAP` read `STATE` and treat a wrong `screen` as a finding, not a typo.

| Target | Formula (centre) | Guition | CYD | S3 (`bench_s3.py`) |
|---|---|---|---|---|
| Nav **Ride** / **Setup** / **More** (30-px bar at the bottom of every screen) | `(W/6, H−15)` / `(W/2, H−15)` / `(5W/6, H−15)` | `53,465` / `160,465` / `267,465` | `40,305` / `120,305` / `200,305` | `20,312` / `90,312` / `150,312` |
| Ride title → details pop-down (`rideTitleCb`) | top-left label | `40,10` | `40,10` | `40,10` |
| Setup device row *i* (0–5) | `(W/2, 71 + 38·i)` | `160,71` … `160,261` | `120,71` … `120,261` | `86,71` … |
| Setup **Rescan** / **Save** | `(10 + (W−30)/4, H−54)` / `(W−10−(W−30)/4, H−54)` | `82,426` / `238,426` | `62,266` / `178,266` | `45,266` / `127,266` |
| More row *k*: 0 Workout · 1 Calibrate · 2 Compare · 3 Mode · 4 Identity · 5 Source · 6 Trainer · 7 **Bright** · 8 Touch cal (CYD only) | `(W/2, 49 + 29·k)` | `160,49` … Bright `160,252` | `120,49` … Bright `120,252`, Touch cal `120,281` | `86,49` … |
| Workout preset row *i* (0–3: `4x8`, `ss3x12`, `vo25x3`, `endur45`) | `(W/2, 72 + 52·i)` | `160,72` … `160,228` | `120,72` … `120,228` | `86,70` … |
| Workout **Start** (loaded) · **Change** | y ≈ 255 (button spans 232–274) | `118,255` · `251,255` | `91,255` · `184,255` | `86,262` |
| Workout **Pause** / **Skip** / **Stop** (running; `bw=(W−30)/3`) | `(20+bw/2, 255)` / `(25+3bw/2, 255)` / `(30+5bw/2, 255)` | `68,255` / `169,255` / `270,255` | `55,255` / `130,255` / `205,255` | `28,262` / — / `150,262` |
| Calibrate **Connect + start** | `(W/2, H−60)` | `160,420` | `120,260` | `86,260` |

Note the LVGL More table (`kMoreRows`) has **no Firmware row** — the canvas twin in `LcdUi.h` does; that is the
#346 claim step 5 confirms. Compare is reachable by the More row *or* serial `CMP`.

## 1. Order of work (per board: Guition first, then CYD, then the C3 for the OLED rows)

| Step | Map rows | What | Evidence to record |
|---|---|---|---|
| 1 | geometry | Walk Ride → Setup → More → Workout → Calibrate → Compare → (portal) with `TAP` and the camera | one frame per screen; note clipping, dead bands, unreadable text |
| 2 | F13–F15, F19 | Ride screen live with the fake meter ramping | frame at 100 W and 300 W; `/app` hero side by side |
| 3 | F04, F05, F10 | Setup: rescan, pick the fake meter, pick the trainer sim, Save → reboot | `/status` before/after; `STATE` |
| 4 | F20–F23, F16, F17 | Load a preset, start, pause, resume, skip, stop, change; trainer sim log | sim log excerpt; frames of the console states; `/app` Workout card |
| 5 | F41, F38, F42 | More: brightness cycle (four taps), Firmware row, touch-cal (CYD) | frames; confirm or refute #346 |
| 6 | F29, F33 | Calibrate button; Compare screen | confirm the no-op (the button is decided out: map §2i DEVROWS); Compare renders the stub verdict |
| 7 | F01–F03 | Portal: forget, QR scan with the owner's phone, rejoin; WiFi-off | phone screenshot of the join; `/status` after |
| 8 | F18 | `/app` on a phone for 15 minutes with the board rebooted once | does the screen sleep; does the page recover |
| 9 | F06–F09, F30, F34 | `/app` Settings: filter, ×2, mode, identity, curve round-trip, buttons card | `/status`, `crank_reader.py --name`, `obc_reader.py` |
| 10 | F37 | `route_baseline.py diff` against the "before" capture | 0/57 or the exact differences |
| 11 | map §2i | Removal checks: every row placed off a surface is not offered there (today only F29's device button); every row placed on a surface it lacks is listed as a gap with its §2i item | one line per row |

### Commands per step

The scripted console driver is `bench_s3.py`'s `S3Bench` class (`tap`, `state`, `_send`; `--port`, `--out`) — its
`run()` walk is hard-wired to the S3's coordinates, so drive the other boards with the table in §0c. Per screen,
"a frame + a dump" means: the ffmpeg line from §0b into `"$OUT\<board>-<step>-<screen>.jpg"` and the raw `SCREEN`
capture into `"$OUT\<board>-<step>-<screen>.txt"`.

**Step 1 — geometry.** From Ride (`STATE` → `screen:0`): `TAP` Setup → expect `screen:1`; `TAP` More → `2`;
`TAP` More row 0 → Workout `3`; `TAP` More → `TAP` row 1 → Calibrate `4`; `CMP` (or More row 2) → Compare `5`;
frame + dump at each. The portal screen is step 7's (it only builds while the setup AP is up). Board-specific
expectations: Guition 320-wide (map §3 layout defect), CYD 240, and on the S3 the Setup row text is dotted past
`W−128` px (`LV_LABEL_LONG_DOT`).

**Step 2 — F13–F15, F19.**
```powershell
$PY code\scripts\fake_meter.py --watts 100 --steady --duration 120 ; # frame + dump + /status at 100 W, then:
$PY code\scripts\fake_meter.py --watts 300 --steady --duration 120 ; # …and at 300 W
curl.exe -s http://$BOARD/status      # src_power_w = the meter, power_w = what the crank broadcasts, identity/mode/src_name = F19
$PY code\scripts\crank_reader.py --name "$ID" --seconds 12          # the broadcast side: "[n] 300 W  85 rpm  raw=2f00…"
```
F14 (the chart advances over 60 s): run the default ramp `fake_meter.py --watts 150` for 60 s and frame the Ride
screen twice; `TAP 40 10` opens the details pop-down (`STATE` → `details:1`) for F15's names/link dots/RSSI.
`/app` hero beside it on the phone (F13/F19 on the web).

**Step 3 — F04, F05, F10.** Have three advertisers up: `fake_meter.py` (CPS, named after the PC), the trainer
sim (FTMS, not CPS) and a **second CPS advertiser** — a spare C3 on `esp32c3-supermini` (the mock crank,
advertises `Stages 62144` — it is *listed* because only the board's own `$ID` is skipped; `flash.ps1 -Force` or
`flash_c3.py`). Then on the device: `TAP` Setup → `TAP` Rescan (clears the candidates and forces a 15 s scan
window) → read the rows on the camera (`meter`/`trn` badges) → `TAP` the fake-meter row → `TAP` the sim row →
`TAP` Save: the board prints `[lcd] setup saved; rebooting to apply` and reboots with `meterAddress` pinned and
`trainerNameFilter=SB20-FTMS-Server`.
```powershell
curl.exe -s http://$BOARD/scan                           # the same list the picker shows (name, rssi, cps, ftms, crank)
curl.exe -s http://$BOARD/status                         # before/after: src_name = the fake meter; source=connected
curl.exe -s http://$BOARD/setup/scan                     # the web rescan is a GET (303 → /setup; HTTP-API.md's "POST" is loose); F05 on /app: no 404 in the phone's console
```
**GAPs:** `/status` and `/config` carry **no trainer key** — the picked trainer is proven by the More → Trainer row
(frame) and by the sim log flipping to `controlled=1 started=1`; the web picker cannot pin by address and its rescan
is a phantom (#347, map §2i PICKER) — record, don't fail.

**Step 4 — F20–F23, F16, F17.** Keep the sim log open on `$SIM`. Device side: More → row 0 (Workout) → `TAP`
preset row 0 (`4x8`; `STATE` → `wk_loaded:1`) → `TAP` Start (`wk_running:1`, `wk_target` > 0) → Pause → Resume
(Pause again toggles) → Skip → Stop (`wk_running:0`) → Change (back to the picker). Frame + dump each state; the
erg line on the Workout screen walks *no trainer / connecting… / linked, no ctrl / ON xxx W* (F17). Web side, the
same from curl (or the `/app` Workout card):
```powershell
curl.exe -s -X POST "http://$BOARD/workout/preset?key=4x8"     # keys: 4x8 | ss3x12 | vo25x3 | endur45 ; "loaded" / 400 "unknown preset"
curl.exe -s -X POST http://$BOARD/workout/start ; curl.exe -s http://$BOARD/workout/state
curl.exe -s -X POST http://$BOARD/workout/pause ; curl.exe -s -X POST http://$BOARD/workout/resume
curl.exe -s -X POST http://$BOARD/workout/skip  ; curl.exe -s -X POST http://$BOARD/workout/stop
$PY code\scripts\import_workout.py <file.zwo|.fit> --ftp 250 --post http://$BOARD    # F24: a ZWO/FIT load over /workout/load
```
`/workout/state` gives `loaded, running, paused, seg_index, seg_count, seg_target_w, seg_remaining_s,
total_elapsed_s`; the `erg_*` fields the `/app` erg line needs are **not there yet (#347)** — F17 on the web is a
recorded gap. The sim's oracle lines (decisions.md 2026-07-04 twin proof): boot `controlled=1 started=1
target=0W`; Start → the segment target; Skip → the next; Stop → `target=0W`. F16 wants target/segment/time-left
**on the Ride view** of both surfaces — placed there by the owner, not built yet (map §2i WEBRIDE #351): gap.

**Step 5 — F41, F38, F42.** More → `TAP` the Bright row four times: 25 → 50 → 75 → 100 (`moreRowCb` cycles; the
value cell is the accent-coloured one) — frame after each, the panel must visibly change. F38: the LVGL More table
has no Firmware row (see §0c) → frame the More screen and record #346 confirmed/refuted; the SHA the row should
show is `/status`'s `build_sha`. F42 (CYD only): serial `CALCLEAR` (wipes the stored fit and restarts the ritual)
or `CALTOUCH`; the ritual replaces the UI — camera frames per crosshair; `CALINFO` →
`{"cal_valid":…,"sx":…,"ox":…,"sy":…,"oy":…,"ritual":…,"point":…}`; `RAWZ` → one `{down,rx,ry,z,z1,z2}` sample (an
idle `down:1` is the phantom press cyd-board.md §5 describes); `RAWTAP rx ry` injects a raw press for a headless
run. The "BOOT ≥ 1 s restarts it" clause needs a finger on the board's BOOT button — owner step.

**Step 6 — F29, F33.** More → row 1 (Calibrate): `TAP` **Connect + start** (`(W/2, H−60)`) — `lcdExecute` has no
case for `CalStart` (the comment says the actions are wired in the erg PR), so `STATE` must not change and `/log`
must stay silent: that is the no-op the map decided out (DEVROWS). Compare: serial `CMP` → `screen:5`; the verdict
label reads `waiting for both meters...` until meter B exists; the stub is `CMPRATIO 1110` (B = A × 1.11) with the
fake meter running — frame the verdict + the bias-by-torque chart; `CMPRATIO 1000` restores. F33's real two-meter
verdict is a bike row (session 14 S2).

**Step 7 — F01–F03.** Do this **last** on each board: `/wifi/off` leaves HTTP gone until a power-cycle.
```powershell
curl.exe -s -X POST http://$BOARD/forget     # wipes creds → reboots into the setup portal; the LCD shows the QR + SSID + PIN (frame it)
# owner: scan the QR with the phone → joins Setup-XXXX → the setup page pops (else http://192.168.4.1/) → pick the bench network → reboot
curl.exe -s http://$BOARD/status             # back on the LAN with the same identity/meter/trainer (NVS config survives /forget: only creds go)
curl.exe -s http://$BOARD/wifi/off           # the confirm PAGE (safe) — then:
curl.exe -s -X POST http://$BOARD/wifi/off   # ride mode: "[wifi] ride mode: WiFi off (BLE-only) until power-cycle"
$PY code\scripts\crank_reader.py --name "$ID" --seconds 12    # F03: CPS still on air…
curl.exe -s -m 5 http://$BOARD/status        # …and HTTP times out; recover with a power-cycle (or: close the console, then
$PY -m esptool --port $COM --after hard_reset chip-id          #  the BOARDS.md reset idiom)
```
The phone's join screenshot and the SSID (`Setup-4D21` Guition · `Setup-CC8C` CYD · `Setup-E9A4` the spare C3) are
the F01 evidence; "no NVS residue" for F02 = `/status` identity/mode unchanged after re-provisioning.

**Step 8 — F18.** Phone on `http://$BOARD/app` for 15 min (Wake Lock is a WEBRIDE item — record whether the screen
sleeps). At ~7 min reboot the board **without touching WiFi creds** — there is **no `/reboot` route** (F40 is a
DEVROWS/WEBPARITY item; GAP): close the console, then `$PY -m esptool --port $COM --after hard_reset chip-id`
(the `BOARDS.md` idiom), or over serial `TRAINER SB20-FTMS-Server` (same name → no config change, reboots). Note
the "reconnecting" state and whether the page recovers unaided; `/status`'s `ms` restarting from 0 dates the
reboot. Optional side record for the same 15 min: `$PY code\scripts\perf_soak.py --host $BOARD --duration 900
--interval 5 --output "$OUT\soak.jsonl" --label bench-f18` (heap flat, no stalls).

**Step 9 — F06–F09, F30, F34.** Every `/app` Settings control is one of these routes (HTTP-API.md); use the card on
the phone and confirm with curl:
```powershell
curl.exe -s http://$BOARD/config                                       # scale, offset, single_sided, src_filter, out_name, mode, has_curve
curl.exe -s -X POST http://$BOARD/config -d "single=1"                 # F07 ×2: after the reboot power_w == 2 × src_power_w in /status and in crank_reader
curl.exe -s -X POST http://$BOARD/config -d "src_filter=$env:COMPUTERNAME"   # F06 filter (the bench build ignores it; the ride build needs it)
curl.exe -s -X POST http://$BOARD/config -d "mode=corrector"           # F08: our own identity, no Stages service…
$PY code\scripts\crank_reader.py --any-cps --name "<out_name>" --seconds 12
curl.exe -s -X POST http://$BOARD/config -d "mode=spoof"               # …and back
$PY code\scripts\qa_board.py --no-flash --connect --spoof-name "$ID" --ip <board-ip>   # F09: the fleet identity is what's advertised
# F30 curve round-trip (POST is text/plain; an EMPTY body CLEARS the curve — never post one you did not just read):
curl.exe -s http://$BOARD/curve > "$OUT\curve-before.json"             # {"has_curve":…,"curve":[[100.0,1.05],…]}
curl.exe -s -X POST http://$BOARD/curve -H "Content-Type: text/plain" -d "100.0:1.0500,200.0:0.9800"
curl.exe -s http://$BOARD/curve                                        # byte-identical breakpoints (1 dp power, 4 dp factor)
# F34 buttons card (application/json, the same shape back) + the live observer:
curl.exe -s http://$BOARD/obc/buttons.json                             # {"enabled":false,"actions":[1,2,5,1,2,6]}
curl.exe -s -X POST http://$BOARD/obc/buttons.json -H "Content-Type: application/json" -d "{""enabled"":true,""actions"":[2,1,5,1,2,6]}"
curl.exe -s -X POST http://$BOARD/obc/devmode/on                       # advertises as OBC-SB20 (not the crank) so the observer can find it
$PY code\scripts\obc_reader.py --name OBC-SB20 --timeout 20            # prints "<t> BUTTON <name> (0x30)  PRESSED" per event
curl.exe -s "http://$BOARD/obc/press?id=0x30" ; curl.exe -s "http://$BOARD/obc/press?id=0x30&state=0"   # F35 virtual press + release
curl.exe -s -X POST http://$BOARD/obc/devmode/off
```
F09's "two boards from one image differ" (#330) needs a second board on the same build powered *for that check
only* — `crank_reader.py --address <mac>` picks each by address (two boards on one name make `--address`
mandatory, system-reference §8e).

**Step 10 — F37.**
```powershell
curl.exe -s http://$BOARD/log/on ; curl.exe -s http://$BOARD/log ; curl.exe -s http://$BOARD/stats ; curl.exe -s http://$BOARD/status
$PY code\scripts\route_baseline.py capture $BOARD -o "$OUT\routes-after.json"
$PY code\scripts\route_baseline.py diff "$OUT\routes-before.json" "$OUT\routes-after.json"     # exit 0 = "0 differing of 57 vectors -- BEHAVIOUR PRESERVED"
```
Run it *before* step 7 (it needs HTTP) and with the board in the same state as the "before" capture (warm, same
config) — the steps above change config on purpose, so either restore it first or list each expected diff.

**Step 11 — map §2i.** One line per row placed off/on a surface it lacks; the instruments are the frames and
dumps already taken. Today's off-surface row is F29's device button (step 6); the on-surface gaps are the
DEVROWS list in the map (F02/F03 on More, F40 Reboot, F38 Version, F27 bias ±, F25 FTP).

## 2. Actual — fill in as it runs

Run 2026-09-25 on `4f7b96b5b+dirty` (main 4f7b96b + this branch's tooling), `esp32-guition-live-bench`,
lvgl 9.5.0. Raw captures in `sessions/bench-out/20260925/` (gitignored); cited frames committed under
`sessions/bench-evidence/2026-09-25/`.

| Board | Step | Result | Evidence | Issue filed |
|---|---|---|---|---|
| Guition | 1 geometry | ✅ all six screens reachable, every tap landed | `bench_ui.py --walk`: 8/8 taps correct; dumps `guition-0[1-6]-*.txt` | — |
| Guition | 1 geometry | ❌ **bottom ~40 % of the panel is empty** on Ride and More (content ends ~275 px, nav at 465) | framebuffer + camera agree | **#358** (confirms map §3) |
| Guition | F09 identity | ✅ blanked `out_name` → **`Stages 99744`**, `identity_default=true`; visible on the panel | `/status`, camera | — |
| Guition | 2 F13/F15/F19 | ✅ live watts on panel = `/status`; details pop-down shows names, RSSI −56, erg target | `guition-f15-details.png` | — |
| Guition | 2 F15 | ❌ **OUT card's erg line wraps and collides** with the row beneath it; both unreadable | `f15-out-card-zoom.png` | **#359** (new) |
| Guition | OUT side | ✅ crank broadcasts byte-faithful `2f00…`, 175–200 W, 85 rpm, L50/R50 | `crank_reader.py --address 28:84:85:49:4D:21` | — |
| Guition | 4 F20–F23 | ✅ **load, start, pause, skip, stop all work from the device** (skip moved target 138 → 248) | one-session `--seq`; `/workout/state` `paused=true` with `seg_elapsed` frozen | — |
| Guition | 5 F41 bright | ❌ **#346 CONFIRMED**: label reads `100 %` at all 5 steps; backlight drops on tap 1 then never moves (normalised 0.318 → 0.203, 0.204, 0.204, 0.202) | `bright-rows-stacked.png`, camera normalised against the CYD | #346 |
| Guition | 5 F38 firmware row | ❌ **#346 CONFIRMED**: More has 8 rows, no Firmware/Version row | `guition-03-more.png` | #346 |
| Guition | 5 More → Trainer | ✅ **#346 item FIXED** by #354 — shows `Stages Bike 0105`, not `not set` | `guition-03-more.png` | — |
| Guition | 6 F29 calibrate | ✅ expected no-op: screen unchanged, `/log` silent | `/log` after the tap | — (decided out, map §2i DEVROWS) |
| Guition | 6 F33 compare | ✅ renders the documented stub (`SIMULATED B x1.101`, n=93 pairs) | `guition-f33-compare.png` | — (real verdict = session 14 S2) |
| Guition | 9 F30 curve | ⚠ round-trips byte-identically, but **the board has no fitted curve**, so the interesting path is untested | `/curve` before/after | — |
| Guition | 10 F37 routes | ✅ 57 vectors captured | `routes-guition.json` | — |
| Guition | 3 F04/F05/F10 · 4 F16/F17 · F24–F27 | ⛔ **BLOCKED** — the trainer simulator needs a C3 over USB and no C3 enumerates (charge-only cable) | USB sweep: 2 boards, not 3 | — |
| Guition | 7 F01–F03 · 8 F18 | ⛔ **NOT RUN** — portal QR + 15-min `/app` need a phone in hand | — | — |
| CYD | all | ⛔ **NOT RUN** this pass | — | — |
| C3-OLED | all | ⛔ **NOT RUN** — no USB; OTA-only | — | — |

Second sitting, same day, once a spare C3 (`38:44:BE:45:53:34`, brand new from the box) became the
FTMS simulator. Raw captures in `sessions/bench-out/20260925-cyd/`.

| Board | Step | Result | Evidence | Issue filed |
|---|---|---|---|---|
| Guition | 3 **F10** | ✅ trainer pick persists and is **provable from `/status`** (`trainer:"SB20-FTMS-Server"`) | `/status` | — (run-sheet's "no trainer key" GAP is stale since #354) |
| Guition | 4 **F17** (trainer side) | ✅ erg link reaches **linked + controlling**: sim reports `controlled=1 started=1 hasTarget=1` | simulator serial log | — |
| Guition | 4 **F17** (device screen) | ⛔ **NOT CAPTURED** — the on-screen erg line needs `TAP` to reach the Workout screen, and the Guition has no serial (its cable is the simulator's) | — | — |
| Guition | 4 **F20–F23** | ✅ **confirmed at the trainer**, matching the decisions.md oracle: start → `target=138W`, pause → `0W`, resume → `138W`, skip → `248W`, stop → `0W` | simulator serial log + `/workout/state` | — |
| Guition | — | ℹ **pause RELEASES the erg target to 0 W** rather than holding resistance — correct and safe, now evidenced | simulator log | — |
| CYD | **F09** | ✅ blanked `out_name` → **`Stages 92364`**, `identity_default=true` — the predicted value, and it **ends the `Stages 62144` collision** with the ride C3 | `/status` | — |
| CYD | 1 geometry | ⚠ **6–7 of 8 taps land; the failures move between runs.** Hand-driven taps work every time (`TAP 200 305` → `[lcd] tap injected` → screen 1→2), so this is **instrument flakiness, not a UI defect** — see the note below | `cyd-0*-*.txt` dumps (now complete at ~207 KB) | — |
| CYD | rest | ⛔ **NOT RUN** — workout console, brightness, F42 touch-cal | — | — |

> **Why the CYD walk is flaky, and why it is the instrument.** The CYD is a real CH340 UART at
> 115200 (~11.5 KB/s); its 240×320 frame is ~205 KB of base64, so a `SCREEN` dump needs **~18 s**.
> The harness allowed 8 s, truncated mid-dump (98304 B one run, 81920 the next), and the remaining
> ~10 s of pixel data then flooded the console and swallowed every command after it — which is
> exactly what a dead UI looks like. The Guition never showed this because it is native USB CDC:
> effectively megabits, so its *larger* 410 KB dump finishes in under a second.
>
> Fixed by deriving the read budget from panel size × link speed, warning loudly on truncation,
> taking the LAST JSON line rather than the first (a *searching* board floods `[meter] found ...`
> every second), settling after each dump, and spending the first tap after a port open on a no-op.
> That took the walk from 1/8 to 7/8. **The remaining flakiness means framebuffer dumps are the
> wrong instrument on this board — use the bench camera for CYD screens.**

### CYD block — complete (2026-09-25)

Run with the Guition **parked via `POST /ble/off`** so the CYD could own the simulator: the first
real use of that route, and the reason it exists (two head units, one trainer, nobody at the bench).

| Step | Row | Result | Evidence |
|---|---|---|---|
| — | **F09** | ✅ derived **`Stages 92364`**, `identity_default=true` — predicted value; ends the `Stages 62144` collision with the ride C3 | `/status` |
| 3–4 | **F10, F17 (trainer), F20–F23** | ✅ **identical to the Guition**: start → `138W`, pause → `0W`, resume → `138W`, skip → `248W`, stop → `0W`, `controlled=1 started=1` | simulator serial log |
| 5 | **F41** | ❌ **#346 CONFIRMED** — label reads `100 %` after four taps and the backlight never moves (normalised spread **0.022**, i.e. noise). Note the difference from the Guition, where tap 1 *did* drop it and then froze; here nothing moves at all | `cyd-bright-row-stuck-at-100.png`, camera normalised against the parked Guition |
| 5 | **F38** | ❌ **#346 CONFIRMED** — no Firmware row on this board either (9 rows: the 8 shared + Touch cal) | `cyd-more-*.png` |
| 5 | **F42** | ✅ the touch-cal ritual works **headlessly**: `CALCLEAR` wipes + restarts, `RAWTAP` walks 1/4→4/4, point 4 computes and `[tcal] SAVED` the fit | serial |
| 6 | **F29** | ✅ designed no-op — Calibrate's button leaves the screen unchanged and `/log` stays silent | `/log` |
| 6 | **F33** | ✅ renders the stub: Meter A 215 / B 238, `SIMULATED B x1.104`, n=174 pairs | `cyd-f33-compare.png` |
| 1 | geometry | ❌ **NEW defect #364** — the IP footer is drawn **through** the `Touch cal` row | `cyd-more-ip-collides-with-touchcal.png` |

> **#364 and #358 are one bug with two faces.** Rows sit at `y = 49 + 29*k` regardless of panel
> height or row count. On the Guition (480 tall, 8 rows) that leaves ~40 % of the panel empty; on the
> CYD (320 tall, **9** rows — Touch cal exists only here) the ninth row lands on the fixed IP footer.
> A layout derived from row count and available height fixes both; nudging constants fixes neither,
> and the 4.3-inch Guition on the roadmap would be a third geometry.

> **`RAWTAP` needs raw values in the panel's REAL range.** The first F42 attempt fed 300–3800 and the
> fit was rejected as "taps too clustered" — this digitiser reads ~160–1890 in x and ~93–1915 in y, so
> those coordinates were off the film. Deriving them from the stored fit
> (`raw = (target - offset) / scale`) reproduced the original calibration to the fifth decimal.
> **Clearing a board's touch calibration is destructive** — have the restore values before you start.

## 3. Close-out

Update the map's Verified column for every row that passed; file an issue per failure (link it in the
map's ⚠ cell); move confirmed rows of #346/#347 to "confirmed on hardware"; record the durable
findings in `decisions.md`; then set this doc's status to DONE with the date and hand the bike rows
to session 14.
