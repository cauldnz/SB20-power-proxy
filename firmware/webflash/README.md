# Browser flashing (ESP Web Tools)

The way riders install the firmware: a static page with an **Install** button per board that flashes
over Web Serial straight from Chrome/Edge. It is also the recovery path when a board won't take an OTA
update. Live at <https://cauldnz.github.io/SB20-power-proxy/> once a release has been published.

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

The merge uses the same chip/offsets/flash settings as `flash_c3.py` (kept in step). It runs whichever
esptool PlatformIO installed under `~/.platformio/packages/tool-esptoolpy`, which is *not* the 4.11+
build `flash_c3.py` insists on — and does not need to be: `merge_bin` opens no serial port, so the C3
USB-JTAG bug behind that rule cannot bite. The real constraint is the CLI dialect (4.x underscores vs
5.x hyphens); see the script's header.

## How it gets published

`.github/workflows/pages.yml` deploys it to GitHub Pages. It assembles `index.html` from `main` with
the manifests and factory images of a **published** release, so a draft can never reach riders.
Publishing a release runs it. Run it by hand after changing the page, or for a release whose tag
predates that workflow:

```bash
gh workflow run pages.yml
```

Before the first run, the repository needs Pages set to deploy from GitHub Actions. The images are
served from the same origin as the page on purpose: release-asset downloads redirect to a host whose
CORS headers we don't control.

## Verify before you publish

- `build_factory_bin.py` prints the merged size (~1.2 MB) — a successful merge ends with
  `Wrote 0x… bytes … ready to flash to offset 0x0`.
- Locally you can smoke-test the page over HTTPS (e.g. a throwaway `gh-pages` deploy) and flash a
  spare board, then run the [acceptance gate](../BENCH-FLASH.md#pre-ship-acceptance-gate--codescriptsqa_boardpy)
  (`qa_board.py`) against it — same gate as a pre-flashed board.

## Notes

- The page is intentionally dependency-light: it pulls ESP Web Tools from a CDN at runtime, so there's
  nothing to build for the page itself — only the firmware image.
- Phones can't flash over USB (no Web Serial); the page says so.
- After the first install, the aim is that boards update over the air, and need this page only to
  recover (`code/findings/ota-update-plan.md`).
