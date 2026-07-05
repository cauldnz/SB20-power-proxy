# `web/` — the shared Bike Bridge web UI (one SPA, two transports)

This is the **canonical source** for the browser UI, served to **two hosts from one file**:

- **GitHub Pages** (`cauldnz/bike-bridge-web`, public) — the nRF52840 build has no WiFi, so its UI is
  a static site the browser loads over HTTPS and talks to the board over **Web Bluetooth**.
- **The ESP32, served from the device** (planned) — the ESP32 embeds this same `index.html` and serves
  it over its WiFi HTTP server; the page talks to the board over **HTTP/JSON** instead.

`index.html` is a **single self-contained file** (all CSS + JS inline, no external fetches) because both
hosts require it: GitHub Pages + Web Bluetooth needs an HTTPS origin with no cross-origin loads, and the
ESP32 serves the page inline with no asset routes. One file → identical bytes on both hosts.

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

**Status:** the `Transport` seam + `BleTransport` are live (what the nRF build ships). `HttpTransport`
is written to the `HTTP-API.md` contract and auto-selects when served from a device, but is unverified
until the ESP32 side (its JSON endpoints + embedding this file) lands — the deferred U4 task.

## Design tokens

The colour palette is **not** hand-written here — the `:root` block between the `TOKENS-GEN` markers is
generated from [`../design/tokens.json`](../design/tokens.json) (the single source shared with the ESP32
web CSS, the LVGL RGB565 palette, and the mockups). Edit `tokens.json`, run the generator, and every
frontend re-themes. CI fails if the generated blocks drift.

## Deploy

`./deploy.sh` pushes `index.html` to the public Pages repo. It clones `cauldnz/bike-bridge-web` to a
temp dir, copies this file in, commits, and pushes — so `web/index.html` here stays the source of truth
and the Pages repo is a deploy target. (The ESP32 will embed the same file via a generated header once
that side lands.)
