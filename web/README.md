# `web/` — the shared Bike Bridge web UI (one SPA, two transports)

This is the **canonical source** for the browser UI, served to **two hosts from one file**:

- **GitHub Pages** (`cauldnz/bike-bridge-web`, public) — the nRF52840 build has no WiFi, so its UI is
  a static site the browser loads over HTTPS and talks to the board over **Web Bluetooth**.
- **The ESP32, served from the device** (✅ shipped: PR #234, hardware-verified at `GET /app`) — the ESP32 embeds this same `index.html` and serves
  it over its WiFi HTTP server; the page talks to the board over **HTTP/JSON** instead.

`index.html` is **self-contained** (all CSS + JS inline, no external fetches) apart from its two
same-origin sibling ES modules (`bridge-codec.js`, `ride-model.js`), because both hosts require it:
GitHub Pages + Web Bluetooth needs an HTTPS origin with no cross-origin loads, and the ESP32 serves the
page with no asset routes — so `gen_spa_header.py` inlines the modules into the board's single file.
One source → identical behaviour on both hosts.

## How one file serves two transports

All device I/O goes through a `Transport` interface — `connect`, `onStatus`, `getConfig`/`setConfig`,
`scan`/`onScan`, the calibration + workout + recording ops — with two implementations, both
translating to/from the **same normalized objects** the view renders:

- **`BleTransport`** — Web Bluetooth GATT (mirrors `firmware-nrf/GATT.md`, PROTO_VER 1).
- **`HttpTransport`** — `fetch()` to the ESP32's JSON API (contract: [`HTTP-API.md`](HTTP-API.md)).

`pickTransport()` auto-selects: served from a device origin (its `/status` probe answers) → HTTP;
GitHub Pages / `file:` → BLE. The view never calls GATT or `fetch` directly, so a UI change lands on
both hosts at once. Per-transport `caps` hide features a host lacks (e.g. the ESP32 has no IMU, so the
Track-recording card is hidden there).

**Status:** both transports are live. `BleTransport` is what the nRF build ships; `HttpTransport`
is served by every ESP32 board at `GET /app` (hardware-verified 2026-07-05/11). As of #347 (2026-10)
every route `HttpTransport` calls exists on the board — `/calibrate/state`, `POST /workout/trainer`,
`POST /workout/bias` and `POST /setup/scan` were phantoms that 404'd, and `/workout/state` lacked the
`erg_*` fields; a native test now pins the list. The phone-on-the-bench pass of the new ride view
(`docs/ui-feature-map.md` §4, bench step 8) has not run yet.

## The ride view (#351)

`/app` is meant to be a usable ride display on a phone on the bars, not only a settings page
(`docs/ui-feature-map.md` §1 principle 3):

- **Hero (F16):** while a workout runs, the current erg target (plus any ± bias), the segment's label
  and its time left, and the erg link state, under the big watts.
- **Power chart (F14):** the last 90 s of output power at ≤1 Hz, the workout target dashed; an outage
  draws as a gap.
- **Ride mode (F45):** ⛶ **Ride mode** opens a full-screen overlay of large numbers only — watts,
  cadence, target, time left, erg state — and one tap anywhere leaves. It asks for browser fullscreen
  where allowed (not iPhone Safari); the overlay works without it.
- **Keep-awake and reconnect (F18):** a Screen Wake Lock is held in ride mode or while a workout runs,
  and re-acquired when the page becomes visible again. **The Wake Lock API exists only in secure
  contexts**: the Pages copy (https) has it, the board's own `http://<board>/app` does **not** — there the
  page falls back to playing a tiny muted inline video (a canvas stream), which browsers treat as media
  playback. That fallback is unverified on a phone; the ride view shows which mechanism holds the screen
  (`screen kept on` / `(video)` / `screen may sleep`). Over HTTP, `/status` is a heartbeat: two missed
  polls revoke the green dot, show *reconnecting* and dim the numbers; `ms` going backwards marks a
  board restart; the page recovers on its own, no reload (`HTTP-API.md` §Live data).

The pure half of all this — the link monitor, the hero/ride-mode workout line, the chart history — is
[`ride-model.js`](ride-model.js), a sibling ES module like the codec, tested under Node by
[`test/ride-model.test.mjs`](test/ride-model.test.mjs) (CI: the `bridge-parity` job) and inlined into
`WebSpa.h` by `gen_spa_header.py`.

## Trying it at the desk (no board)

```bash
cd code && PYTHONPATH=src python -m sb20proxy.qa.mock_board --port 8320
# open http://127.0.0.1:8320/app (a phone-width window), then e.g.:
curl -X POST "http://127.0.0.1:8320/_mock/reboot?down_s=6"   # watch "reconnecting" and the recovery
```
The mock serves the exact bytes the board streams (the raw string in `WebSpa.h`, so regenerate it
first) and the JSON routes with the firmware's field names — `code/tests/test_mock_board.py` diffs them
against the C++ serializers. The power is a synthetic ramp; the presets are the board's own
(`WorkoutPresets.h`). For the BLE copy, serve `web/` statically (`python -m http.server -d web`): the
page picks `BleTransport` when `/status` does not answer.

## Design tokens

The colour palette is **not** hand-written here — the `:root` block between the `TOKENS-GEN` markers is
generated from [`../design/tokens.json`](../design/tokens.json) (the single source shared with the ESP32
web CSS, the LVGL RGB565 palette, and the mockups). Edit `tokens.json`, run the generator, and every
frontend re-themes. CI fails if the generated blocks drift.

## Deploy

`./deploy.sh` pushes `index.html` to the public Pages repo. It clones `cauldnz/bike-bridge-web` to a
temp dir, copies this file in, commits, and pushes — so `web/index.html` here stays the source of truth
and the Pages repo is a deploy target. It ships every sibling module `index.html` imports
(`bridge-codec.js`, `ride-model.js`). The ESP32 embeds the same file, with those modules inlined, via
`gen_spa_header.py` → `firmware/lib/proxy/WebSpa.h`.
