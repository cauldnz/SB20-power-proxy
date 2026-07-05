# ESP32 HTTP/JSON API — the `HttpTransport` contract

`web/index.html`'s `HttpTransport` talks to the ESP32 over this JSON API, the HTTP mirror of the
nRF's GATT contract (`firmware-nrf/GATT.md`). Some endpoints **already exist** on the ESP32 (`/status`,
`/workout/state`); the rest are **added in U4** (the deferred ESP32-side task). Field names are the
ESP32's existing snake_case; `HttpTransport` maps them to the shared normalized objects the view
renders (see the top of the `<script>`).

All JSON, `Content-Type: application/json`. Reads are `GET`; commands are `POST`. Live data is polled
at 1 Hz (`/status`, `/workout/state`, `/scan`, `/calibrate/state`) — the same model the ESP32 dashboard
already uses.

## Exists today

### `GET /status` → normalized **Status**
```json
{ "source":"connected|searching|mock", "power_w":248, "src_power_w":250,
  "src_cadence_rpm":95, "src_balance_pct":50, "ms":12345, "fw":"sb20proxy-esp32" }
```
Map: `srcConnected = source==="connected"`, `outW = power_w`, `srcW = src_power_w`, `cad`, `bal`,
`uptime = ms/1000`. `scale`/`offset` come from `/config` (cached). `recN = 0` (no IMU on the ESP32).

### `GET /workout/state` → normalized **Wk**
```json
{ "loaded":true, "running":true, "paused":false, "seg_index":2, "seg_count":8,
  "seg_target_w":280, "seg_remaining_s":180, "total_elapsed_s":900 }
```
`HttpTransport` also reads `erg_connected`, `erg_controlled`, `bias_w` if present (add them in U4 so the
Garmin/web erg line + shifter bias render; default 0/false until then).

## Added in U4 (ESP32-side, additive to the existing form-POST routes)

### `GET /config` → normalized **Config**
```json
{ "scale":1.05, "offset":-2.0, "single_sided":false, "src_filter":"ASSIOMA", "out_name":"Stages 62144" }
```
### `POST /config`  (body: the same JSON) → persist + apply; return the new `/config`.

### `GET /scan` → **Scan** list
```json
{ "devices":[ {"name":"SB20-FTMS-Server","rssi":-55,"cps":false,"ftms":true,"crank":false} ] }
```
`POST /setup/scan` kicks a rescan (already exists as a redirect; fine).

### `GET /calibrate/state` → normalized **Cal**
```json
{ "state":1, "pairs":40, "min_pairs":30, "residual_w":-0.3, "coverage":[1,2,5,5,3,0], "enough":true }
```
`POST /calibrate/start` (`ref=<name>`), `/calibrate/cancel`, `/calibrate/save` — the existing routes,
which may reply HTML today; `HttpTransport` ignores the body and re-polls `/calibrate/state`.

### Workout commands (mirror the GATT `WkCmd`s)
`POST /workout/trainer` (`name=<n>`), `/workout/preset?key=4x8|ss3x12|vo25x3|endur45`,
`/workout/start|pause|resume|stop`, and `/workout/bias?d=<±W>` (the shifter — add if the ESP32 erg
gains a target bias).

## Not applicable to the ESP32
IMU recording (`recSetRate/recStart/recStop/recErase/recDownload`) — the ESP32 has no IMU, so
`HttpTransport.caps.recording = false` and the view hides the Track-recording card.
