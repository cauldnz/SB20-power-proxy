# Board inventory — MACs, types, and how to identify each board

A quick-reference for the physical boards in this project so we don't re-discover them every session.
**COM ports are NOT stable** (they re-enumerate on replug) — identify a board by its **USB VID:PID /
chip** and confirm with its **MAC**. Get a live MAC any time:

- **ESP32 (any):** `code/.venv/Scripts/python -m esptool --port COMxx read-mac` (the **base** MAC; on
  ESP32 the WiFi-STA MAC == base, the SoftAP MAC == base+1, the BLE MAC == base+2).
- **nRF / BLE side:** scan with `code/scripts/scan_all.py` (bleak) and match the advertised name.
- **Which ESP32 is which C3/S3:** `python -m esptool --port COMxx chip-id` prints the chip family.

The per-device WiFi **setup-AP SSID is `Setup-XXXX`** where `XXXX` = the last 2 bytes of the WiFi-STA MAC
(see `firmware/lib/proxy/SetupPin.h::setupApSsid`) — so a board's SSID suffix *is* a MAC fingerprint.

## Summary

| Board | Chip | Base / key MAC | USB VID:PID (chip) | Display | Role / firmware |
|---|---|---|---|---|---|
| **XIAO nRF52840 Sense** | nRF52840 | BLE `DE:F2:ED:C4:F3:FD` | `2886:8045` (native) | — | The Bridge/spoof board — `firmware-nrf`. Advertises `SB20 Bridge` (corrector) / `Stages 62144` (spoof). IMU onboard. |
| **C3 + 0.42" OLED** (peff74) | ESP32-C3 | base `38:44:BE:45:E9:A4` · BLE `…E9:A6` | `303A:1001` (native USB-JTAG) | SSD1306 **72×40** @0x3C, SDA=5/SCL=6 | The original OBC/portal test board. SSID `Setup-E9A4`. **Superseded by the 0.96" C3 below.** |
| **C3 + 0.96" OLED** (AliExpress) | ESP32-C3 | base/STA `10:B4:1D:BA:C9:0C` | `303A:1001` (native USB-JTAG) | **SH1106 128×64 @0x3C, I2C SDA=5 / SCL=6** — CONFIRMED · ⛔ **WiFi RF DEAD** | ⛔ **DEFECTIVE WiFi (2026-07-13) — do NOT use as a head-unit; display-only/bench spare.** Its softAP reports fully up (`ap=1`, AP-only, max TX, ch 1) but the beacon is **invisible to 3 devices inches away**, and it can't see/join a 2.4 GHz hotspot a Garmin sees. Ruled out: firmware (identical build beacons on the 0.42" board), config, RF-cal (full `erase-flash` no change), power (diff port+cable no change). Verdict: **dead TX / degraded RX — hardware fault.** RMA it. (OLED still works.) New head-unit C3. 4 MB XMC flash. **Build: `esp32c3-oled96sh-live`** (SH1106). **Pins confirmed 2026-07-13** by the rewritten `c3-oled-probe` (scan at **50 kHz** — the earlier 100 kHz scan false-negatived — then it wrote the result to **NVS**, read back with `esptool read-flash 0x9000 0x5000` → `OLEDPROBE SDA=05 SCL=06 ADDR=3C`, fully autonomous, no eyes/serial). **Controller confirmed SH1106** 2026-07-13: the SSD1306 build drew blank on the correct pins; `esp32c3-oled96sh-live` (`-DOLED_SH1106`) renders the UI. Its native-USB-JTAG **doesn't deliver `Serial` to the host** (esptool flashes fine; app output silent) — use the NVS-read-back / OLED channel, not serial. |
| **CYD** (ESP32-2432S028R) | ESP32-D0WD (classic) | STA `…:CC:8C` *(full TBD)* | `1A86:7523` (CH340 UART) | ILI9341/ST7789 **240×320** + XPT2046 touch | LVGL head-unit — `esp32cyd-live`. SSID `Setup-CC8C`. No PSRAM (banded render). |
| **S3-Touch** (Waveshare ESP32-S3-Touch-LCD-1.47) | ESP32-S3R8 | base `A4:CB:8F:DA:E9:CC` | `303A:1001` (native USB) | JD9853 **172×320** + AXS5106 touch | LVGL head-unit — `esp32s3-pio-live-ota`. 8 MB PSRAM, **8 MB flash** (this module — the env's old 16MB `default_16MB.csv` boot-loops it; now `default_8MB.csv`). |
| **Guition JC3248W535** ⚠️ *name unverified* | ESP32-S3-N16R8 | base `28:84:85:49:4D:20` (SSID `Setup-4D21`) | `303A:1001` (native USB) | AXS15231B **320×480** QSPI + AXS15231B I2C touch | LVGL head-unit — `esp32-guition-live-ota`. **Measured 2026-09-23:** ESP32-S3 rev v0.2, 16 MB flash (Boya `0x68`/`0x4018`, eFuse quad), ~8 MB octal PSRAM (7.37 MiB block). QSPI panel driven via the **vendored Espressif `esp_lcd_axs15231b`** driver (Arduino_GFX was abandoned — version pincer); LVGL full-refresh mode. ⚠️ **The model string comes from the owner's AliExpress order listing, not the PCB silkscreen** — no ESP32 board can self-report a vendor model, and the variant suffix (e.g. `…EN`) is unknown. The hardware facts above are measured and the pin map is confirmed on-board, so nothing functional depends on the name; it matters for re-ordering. See [guition-board.md](code/findings/guition-board.md). |
| **nRF52840 USB dongle** | nRF52840 | — | `1915:522A` | — | BLE **sniffer** (Wireshark extcap / `code/scripts/sniff_ble.py`), not a target. |
| **Garmin/Dynastream ANT+ stick** | — | — | `0FCF:1008` | — | ANT+ radio for the Python tooling (`scripts/01_capture_stages.py`, `03_static_replay.py --radio ant`). |

## Notes / gaps
- **COM ports observed this session (2026-07-11, will change):** XIAO nRF `COM9`, C3-0.42 `COM5`,
  C3-0.96 `COM13`, CYD `COM12`, S3-Touch `COM14`/`COM16`, nRF dongle `COM8`, ANT+ stick (WSL usb).
- **CYD full MAC not yet captured** — only the STA suffix `CC:8C` (from its `Setup-CC8C` SSID). Read it
  with esptool next time it's on a CH340 port.
- **Live IPs + mDNS (measured 2026-07-27, Guition 2026-09-23; DHCP so not stable):** the head-unit C3 at
  **`192.168.1.165`**, the CYD at **`192.168.1.234`**, the **Guition at `192.168.1.222`** — note the LAN
  moved from the `192.168.0.x` subnet recorded earlier, so any `192.168.0.*` address in an older doc is
  stale. **The mDNS hostname collision is FIXED:** each board answers its own name — `sb20proxy.local`
  → the C3, `sb20proxy-cyd.local` → the CYD, `sb20proxy-guition.local` → the Guition (all verified
  resolving and serving `/status`). Per-board hostnames are `sb20proxy` / `-cyd` / `-guition` / `-s3`.
  **2026-09-23:** the Guition used to answer to `-s3` — the switch keyed off `CONFIG_IDF_TARGET_ESP32S3`
  and the Guition is *also* an S3, so it collided with the Waveshare board. **Name boards by board, not
  by chip.**
- **ESP32 native-USB flashing** (C3 + S3) needs **esptool ≥ 4.11** (the bundled 4.5.1 wedges the USB-JTAG);
  `code/scripts/flash_c3.py` auto-picks a good one. The S3 must build on the **pioarduino** platform
  (`esp32s3-pio*`) — the stock `esp32s3-touch` env boot-looped and was **removed 2026-07-26**.
- **A USB flash of an S3 board no longer wipes provisioning (fixed 2026-09-23).** `flash_s3.py` used to
  write pioarduino's merged `firmware.factory.bin` at `0x0`, and that image is `0xFF`-padded across the
  NVS window — so *every* flash silently erased WiFi credentials, meter/crank identity and calibration,
  and the board came back up in its setup portal. It now parses the partition table and writes only the
  code regions (`bootloader` / `partitions` / `boot_app0`→otadata / `firmware`→app0), leaving every data
  partition alone. Pass **`--erase-nvs`** when you genuinely want a clean slate. (Push **OTA** always
  preserved NVS and still does.)
- **📷 Bench camera — verify screens without eyes on them (2026-09-18).** A **UC70** USB camera
  (`0AC8:3420`, Camera class) is aimed at the head-units, and **ffmpeg is installed**
  (`C:\ProgramData\chocolatey\bin\ffmpeg.exe`), so a session can *see* what a panel is actually showing
  instead of asking the owner to describe it. Grab a frame:
  `ffmpeg -f dshow -rtbufsize 512M -video_size 3840x2160 -i video="UC70" -t 3 -update 1 -q:v 2 -y shot.jpg`
  (`-t` lets auto-exposure settle; `-update 1` keeps the last frame). Crop/rotate per board with
  `-vf "crop=w:h:x:y,transpose=N"` — the boards sit rotated, and the **CYD and Guition are rotated
  opposite ways** (`transpose=1` vs `transpose=2`). Record across a reboot (`ffmpeg -t 16` while resetting
  with `python -m esptool --port COMxx --after hard_reset chip-id`) to catch boot-time screens. This closed
  the Guition bring-up loop: build → flash → capture → judge, no human in the loop.
- **Building the LVGL envs on Windows** (`esp32cyd*`, `esp32s3-pio*`): LVGL's relative include chains can
  cross the `MAX_PATH` 260-char limit from a deep worktree path and fail to compile. Shorten the root with a
  directory junction — `cmd /c mklink /J C:\sbw <repo-root>` — and build from there. No admin rights, no
  registry change, no toolchain edit. *(2026-07-27; decisions.md.)*

*Maintenance: update a row when a board is added/reflashed/re-MAC'd. This is the local, project-specific
sibling of the general board-ontology idea in [maker-skills#160](https://github.com/cauldnz/maker-skills/issues/160).*
