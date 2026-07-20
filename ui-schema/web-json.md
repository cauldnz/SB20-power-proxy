<!-- GENERATED from ui-schema/web-json.json by code/scripts/gen_webjson.py — do not edit by hand -->
# Web JSON contract (ESP32 `/scan` `/config` `/curve` <-> the web SPA)

The single source is [`web-json.json`](web-json.json); `firmware/lib/proxy/WebJson.h` emits
these and `web/index.html` reads them. CI (`gen_webjson.py --check`) fails if either side drifts.

## `GET /scan` (renderScanJson) — items under `devices[]`

Nearby meters/trainers for the source picker.

| field | type | meaning |
|---|---|---|
| `name` | string | advertised device name |
| `rssi` | int | signal strength (dBm) |
| `cps` | bool | advertises Cycling Power Service (a meter) |
| `ftms` | bool | advertises FTMS (a trainer) |
| `crank` | bool | is a Stages crank |

## `GET /config` (renderConfigJson)

The device's identity/correction config for the SPA to display.

| field | type | meaning |
|---|---|---|
| `scale` | number | linear scale baseline (1.0; the ESP uses a curve) |
| `offset` | number | linear offset baseline (0.0) |
| `single_sided` | bool | double a single-sided meter |
| `src_filter` | string | meter name filter |
| `out_name` | string | advertised spoof identity |
| `mode` | string | "spoof" | "corrector" |
| `has_curve` | bool | a fitted correction curve is active |

## `GET /curve` (renderCurveJson)

The correction curve as portable [power_w, factor] breakpoints.

| field | type | meaning |
|---|---|---|
| `has_curve` | bool | a fitted curve is active |
| `curve` | array | [[power_w, factor], ...] breakpoints |

## `GET /compare` (renderCompareJson)

The #10 A/B meter-compare deep-dive the web Compare card renders: summary + per-torque-band bias + a power×cadence bias grid + downsampled pairs for Bland-Altman. `nested` are the grid's inner keys (checked but not top-level fields); `deltaW` is emitted for head-unit parity and not read by the SPA (js=false).

| field | type | meaning |
|---|---|---|
| `valid` | bool | at least one usable pair yet |
| `simulated` | bool | meter B is fabricated from A (bench adapter) — the numbers restate the ratio, not a measurement |
| `aName` | string | meter A (reference) label |
| `bName` | string | meter B label |
| `aW` | int | latest paired A watts |
| `bW` | int | latest paired B watts |
| `deltaW` | int | b-a for the latest pair (head-unit view-model; the SPA doesn't render it) |
| `ratio` | number | rolling mean B/A |
| `biasPct` | number | rolling mean (b-a)/a % |
| `nPairs` | int | pairs in the rolling window |
| `tqBandNm` | int | torque-band width (N·m) |
| `tqBias` | array | per-torque-band bias %, null = empty band |
| `grid` | object | power×cadence bias heatmap; inner keys pW/cLo/cW (axes), P/C (bin counts), bias[][] (null = empty cell) |
| `pairs` | array | downsampled [a,b] pairs for the Bland-Altman scatter |
