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
{ "scale":1.0, "offset":0.0, "single_sided":false, "src_filter":"ASSIOMA", "out_name":"Stages 62144",
  "mode":"spoof", "has_curve":false }
```
`mode` is `"spoof"` (impersonate the Stages crank — drive an SB20) or `"corrector"` (our own identity).
The ESP32's correction is a fitted **curve**, so `scale`/`offset` report the `1.0`/`0` baseline and
`has_curve` flags whether a curve is active (the nRF's Config is scalar scale/offset instead).

### `POST /config` → persist + **reboot** to apply (mode/identity are built at boot; mirrors `/setup/save`)
Body is **urlencoded form fields** (the ESP32 has no JSON parser): `single` (`1`/`0`), `src_filter`,
`out_name`, `mode` (`spoof`/`corrector`). It **merges** onto the stored config — fields not sent are
preserved (the fitted curve, reference meter, trainer, spoof serial), so a partial Apply never wipes a
calibration. Returns `{"ok":true,"reboot":true}` (or `{"error":"…"}` on a validation failure), then
restarts. The SPA posts only the ESP32-meaningful fields (scale/offset are the nRF's scalar model).

### `GET /curve` → the correction curve as portable breakpoints
```json
{ "has_curve":true, "curve":[ [100.0,1.0500], [200.0,0.9800], [300.0,1.0200] ] }
```
### `POST /curve`  → load a curve LIVE (no reboot). Body is the compact `"power:factor,..."` form
(`"100.0:1.0500,200.0:0.9800"`; empty body clears it). The SPA does the portable-profile-JSON ↔
compact-string conversion, so the ESP32 needs no JSON parser (it reuses `curveFromString`). This is
the **cross-device profile import**: a curve fitted on the nRF (read off its Curve GATT characteristic)
or the desk tooling loads here, and vice versa.

**Portable profile format** (what Export writes / Import reads — the desk tooling's `CalibrationProfile`):
```json
{ "kind":"grid", "target":"", "ref":"", "breakpoints":[[100.0,1.05],[200.0,0.98]],
  "scale":1.0, "offset":0.0, "meta":{ "source":"bike-bridge-web", "device":"ble|http", "exported":"…" } }
```
`breakpoints` are `[power_w, factor]` (1 dp power, 4 dp factor) — the same across the nRF Curve
characteristic, the ESP32 `/curve`, and `code/scripts/09_fit_calibration.py`.

### `GET /obc/buttons.json` → the SB20-button binding + sink enable
```json
{ "enabled":false, "actions":[1,2,5,1,2,6] }
```
`actions` are action-option **indices** (0 = none) into the shared `firmware/lib/proxy/Sb20ButtonMap.h`
option order — byte-identical to the nRF Bridge GATT Buttons char (0009). The 6 slots are LEFT
up/down/3rd then RIGHT up/down/3rd.

### `POST /obc/buttons.json`  (body: the same JSON) → persist + apply LIVE (no reboot); return the new value.
Sinks the SB20's own shifter buttons and re-broadcasts each press as the bound action (an OBC id, or a
local erg nudge). Enabling starts the SB20 central in place. The ESP32 parses this one fixed shape (no
general JSON parser on-device — `buttonsFromJson`, host-tested), mirroring the nRF's index wire form.

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
