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
| **C3 + 0.96" OLED** (AliExpress) | ESP32-C3 | base/STA `10:B4:1D:BA:C9:0C` | `303A:1001` (native USB-JTAG) | SSD1306 **128×64** — **I2C pins UNKNOWN** ⚠️ | New head-unit C3. 4 MB XMC flash. `esp32c3-oled96-live` build is done (compiles/flashes/boots/USB-stable) but the panel is **blank on 5/6** and the `c3-oled-probe` found **no device at 0x3C/0x3D on 17 candidate pairs** — so the OLED is on pins outside the list (maybe the USB pins 18/19 — the board's USB also flakes on the vendor demo). NEXT: wider pin scan or the board schematic. Its native-USB-JTAG also **doesn't deliver `Serial` to the host** (esptool flashes fine; app output is silent) — verify via the OLED, not serial. |
| **CYD** (ESP32-2432S028R) | ESP32-D0WD (classic) | STA `…:CC:8C` *(full TBD)* | `1A86:7523` (CH340 UART) | ILI9341/ST7789 **240×320** + XPT2046 touch | LVGL head-unit — `esp32cyd-live`. SSID `Setup-CC8C`. No PSRAM (banded render). |
| **S3-Touch** (Waveshare ESP32-S3-Touch-LCD-1.47) | ESP32-S3R8 | base `A4:CB:8F:DA:E9:CC` | `303A:1001` (native USB) | JD9853 **172×320** + AXS5106 touch | LVGL head-unit — `esp32s3-pio-live-ota`. 8 MB PSRAM, **8 MB flash** (this module — the env's old 16MB `default_16MB.csv` boot-loops it; now `default_8MB.csv`). |
| **nRF52840 USB dongle** | nRF52840 | — | `1915:522A` | — | BLE **sniffer** (Wireshark extcap / `code/scripts/sniff_ble.py`), not a target. |
| **Garmin/Dynastream ANT+ stick** | — | — | `0FCF:1008` | — | ANT+ radio for the Python tooling (`scripts/01_capture_stages.py`, `03_static_replay.py --radio ant`). |

## Notes / gaps
- **COM ports observed this session (2026-07-11, will change):** XIAO nRF `COM9`, C3-0.42 `COM5`,
  C3-0.96 `COM13`, CYD `COM12`, S3-Touch `COM14`/`COM16`, nRF dongle `COM8`, ANT+ stick (WSL usb).
- **CYD full MAC not yet captured** — only the STA suffix `CC:8C` (from its `Setup-CC8C` SSID). Read it
  with esptool next time it's on a CH340 port.
- **DHCP IPs seen (not stable):** an ESP32 at `192.168.0.92` (SPA-over-HTTP verify, spoof/ASSIOMA), the
  CYD at `192.168.0.247`. mDNS: both answer `sb20proxy.local` (hostname collision — a per-device hostname
  is the clean follow-up, same idea as the SSID suffix).
- **ESP32 native-USB flashing** (C3 + S3) needs **esptool ≥ 4.11** (the bundled 4.5.1 wedges the USB-JTAG);
  `code/scripts/flash_c3.py` auto-picks a good one. The S3 must build on the **pioarduino** platform
  (`esp32s3-pio*`) — the stock `esp32s3-touch` env boot-loops (⛔ superseded).

*Maintenance: update a row when a board is added/reflashed/re-MAC'd. This is the local, project-specific
sibling of the general board-ontology idea in [maker-skills#160](https://github.com/cauldnz/maker-skills/issues/160).*
