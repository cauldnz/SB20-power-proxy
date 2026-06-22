# Browser flashing (ESP Web Tools)

A no-CLI way to install/recover the proxy firmware: a static page with an **Install** button that
flashes an ESP32-C3 over Web Serial straight from Chrome/Edge. The scalable path beyond the first
pre-flashed boards, and the recovery path when a board won't take an OTA update.

## Files

| File | What |
|---|---|
| `index.html` | the install page (loads ESP Web Tools from a CDN; the button + instructions). |
| `manifest.json` | ESP Web Tools manifest — names the build + points at the factory image. **Generated** by the build script. |
| `firmware-factory.bin` | the single merged image flashed at offset 0. **Generated, not committed** (1.2 MB build artifact — see `.gitignore`). |

## Build the factory image

The browser flashes ONE combined image (bootloader + partitions + boot_app0 + app), not the four
parts `flash_c3.py` writes separately. Build the firmware, then merge:

```bash
cd firmware
pio run -e esp32c3-oled-live-ota              # build the shippable image
cd ..
python code/scripts/build_factory_bin.py --env esp32c3-oled-live-ota --version "$(git rev-parse --short HEAD)"
# -> firmware/webflash/firmware-factory.bin + refreshed manifest.json
```

The merge uses the same chip/offsets/flash settings as `flash_c3.py` (kept in step) and esptool 4.11
(the one that handles the C3 cleanly).

## Host it

ESP Web Tools requires **HTTPS** (Web Serial won't run otherwise) and a Chromium browser on desktop.
Serve the three files (`index.html`, `manifest.json`, `firmware-factory.bin`) together from any
static HTTPS host:

- **GitHub Pages** (simplest): publish this folder. Because the factory bin is gitignored, the
  publishing step must run `build_factory_bin.py` first (a CI job, or a one-off manual copy onto the
  `gh-pages` branch). Don't commit the 1.2 MB binary to `main`.
- Or attach `firmware-factory.bin` to a **GitHub Release** and point `manifest.json`'s `path` at that
  asset URL (absolute), hosting only `index.html` + `manifest.json` on Pages.

## Verify before you publish

- `build_factory_bin.py` prints the merged size (~1.2 MB) — a successful merge ends with
  `Wrote 0x… bytes … ready to flash to offset 0x0`.
- Locally you can smoke-test the page over HTTPS (e.g. a throwaway `gh-pages` deploy) and flash a
  spare board, then run the [acceptance gate](../BENCH-FLASH.md#pre-ship-acceptance-gate--codescriptsqa_boardpy)
  (`qa_board.py`) against it — same gate as a pre-flashed board.

## Notes

- The page is intentionally dependency-light: it pulls ESP Web Tools from a CDN at runtime, so there's
  nothing to build for the page itself — only the firmware image.
- Phones can't flash over USB (no Web Serial); the page says so. Pre-flashed boards remain the default
  for testers — this is the install/recover escape hatch, not the primary path.
