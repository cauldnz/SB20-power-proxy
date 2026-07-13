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
