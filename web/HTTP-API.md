# ESP32 HTTP/JSON API — the `HttpTransport` contract

`web/index.html`'s `HttpTransport` talks to the ESP32 over this JSON API, the HTTP mirror of the
nRF's GATT contract (`firmware-nrf/GATT.md`). **Every route below exists on the board**: the route
table is `firmware/lib/proxy/WebRoutes.h` `stationRoutes()` (+ the shared workout verbs), and the
native test `test_every_route_the_spa_calls_exists` (`firmware/test/test_webroutes/`) fails if a route
the SPA calls goes missing. Until #347 (2026-10) four of them were phantoms the SPA called and the
board 404'd; this file used to describe them as existing. Field names are the ESP32's snake_case;
`HttpTransport` maps them to the shared normalized objects the view renders (see the top of the
`<script>`). The field names of `/scan`, `/config`, `/curve`, `/compare`, `/calibrate/state` and the
erg keys of `/workout/state` are single-sourced in [`ui-schema/web-json.json`](../ui-schema/web-json.json)
(CI: `gen_webjson.py --check`).

All JSON, `Content-Type: application/json`, unless noted. Reads are `GET`; commands are `POST`, and
every `POST` is CSRF-guarded (same-origin only — the SPA served at `/app` is). The SPA posts its form
bodies as `text/plain` (fetch's default for a string body); the board hands that raw body to the route,
which parses it as `application/x-www-form-urlencoded`.

## Live data and the link (F18)

`HttpTransport` polls once a second: `/status` first, then `/workout/state`, `/scan`,
`/calibrate/state`. **`/status` is the heartbeat** (`web/ride-model.js` `LinkMonitor`): two failed polls
in a row (2.5 s timeout each) revoke the green dot, show *reconnecting*, and stop the other three polls
until it answers again; `ms` going **backwards** means the board restarted (also when the restart fit
between two polls). On recovery the SPA re-reads `/config`, `/curve` and `/obc/buttons.json`. No manual
reload: many commands below reboot the board, and the page rides through it.

### `GET /status` → normalized **Status**
```json
{ "fw":"sb20proxy-esp32", "version":"…", "build_sha":"…", "build_time":"…",
  "source":"connected|searching|mock", "src_name":"ASSIOMA17039L",
  "identity":"Stages 92729", "mode":"spoof", "identity_default":true,
  "source_pin":"e6:20:90:8c:f3:fe", "source_filter":"ASSIOMA", "trainer":"Stages Bike 0105", "ble_off":false,
  "forwarded":736, "src_power_w":169, "src_cadence_rpm":55, "src_balance_pct":45,
  "power_w":338, "cadence_rpm":55, "balance_pct":45, "rssi":-55, "heap":125536, "ms":743252 }
```
(`Status.h` `renderStatusJson`; the numbers are from a real capture,
`code/findings/captures/G-zero-reset-onair-pass-20260626-0651.txt`.)
Map: `srcConnected = source==="connected"`, `outW = power_w`, `srcW = src_power_w`, `cad`, `bal`,
`uptime = ms/1000`. `scale`/`offset` come from `/config` (cached). `recN = 0` (no IMU on the ESP32).
`trainer` (`""` = none set) also tells the SPA's erg row to say *no trainer* instead of *searching…*.

**Fleet identity (#330):** `identity` is the name the board advertises. `identity_default` is `true`
when nothing is stored and the name was derived from the board's base MAC at boot (`Stages 9NNNN`,
`firmware/lib/proxy/FleetIdentity.h`), `false` for a stored identity (a non-blank name saved from
`/setup` or `POST /config`; a blank keeps the derived default). `source_pin` / `source_filter` are the
pinned source address and the name filter, `trainer` the FTMS trainer the workout engine erg-drives
(`""` = erg off) — the three bindings a two-bike room must get right, in one read, so a wrong one is
visible (`docs/system-reference.md` §7).

### `GET /workout/state` → normalized **Wk**
```json
{ "name":"4x8 Threshold", "ftp_w":250, "loaded":true, "running":true, "paused":false, "finished":false,
  "seg_index":1, "seg_count":9, "seg_label":"Interval 1", "seg_target_w":248, "seg_elapsed_s":100,
  "seg_remaining_s":380, "next_label":"Recovery", "next_target_w":125, "total_elapsed_s":700,
  "total_remaining_s":2480, "segments":[{"t":600,"w":138,"label":"Warm-up"}, …],
  "erg_connected":true, "erg_controlled":true, "bias_w":10 }
```
`WorkoutEngine.h` `renderWorkoutJson` (unchanged), then **the erg leg appended** (#347, F17/F27;
`WebJson.h` `renderWorkoutStateJson`): `erg_connected` (the FTMS trainer link is up), `erg_controlled`
(it granted control — workout targets now drive it), `bias_w` (the rider's live nudge on the target,
clamped ±200 W; the trainer gets `seg_target_w + bias_w`). The SPA's ride hero (F16) shows
`seg_target_w`, `seg_label` and `seg_remaining_s`.

### `GET /scan` → **Scan** list
```json
{ "devices":[ {"name":"SB20-FTMS-Server","rssi":-55,"cps":false,"ftms":true,"crank":false} ] }
```
### `POST /setup/scan` → `{"ok":true,"scanning":true}`
Kicks a rescan (#347); the list refills through `GET /scan`. The legacy page's `GET /setup/scan` (a 303
back to `/setup`) is unchanged. (The SPA has the call but no Rescan button yet — F05, PICKER.)

## Config, curve, buttons

### `GET /config` → normalized **Config**
```json
{ "scale":1.0, "offset":0.0, "single_sided":false, "src_filter":"ASSIOMA", "out_name":"",
  "mode":"spoof", "has_curve":false }
```
`mode` is `"spoof"` (impersonate the Stages crank — drive an SB20) or `"corrector"` (our own identity).
`out_name` is the **stored** identity: `""` means none is stored and the board advertises its MAC-derived
default (`Stages 9NNNN`; the live name is `/status` `identity`). `POST /config` with a blank `out_name`
keeps it derived; a non-blank one stores it. The SPA no longer forces `Stages 62144` in spoof mode (#330).
The ESP32's correction is a fitted **curve**, so `scale`/`offset` report the `1.0`/`0` baseline and
`has_curve` flags whether a curve is active (the nRF's Config is scalar scale/offset instead).

### `POST /config` → persist + **reboot** to apply (mode/identity are built at boot; mirrors `/setup/save`)
Body is form fields (the ESP32 has no JSON parser): `single` (`1`/`0`), `src_filter`, `out_name`,
`mode` (`spoof`/`corrector`). It **merges** onto the stored config — fields not sent are preserved (the
fitted curve, reference meter, trainer, spoof serial), so a partial Apply never wipes a calibration.
Returns `{"ok":true,"reboot":true}` (or `{"error":"…"}` on a validation failure), then restarts. The SPA
posts only the ESP32-meaningful fields (scale/offset are the nRF's scalar model).

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

### `POST /obc/buttons.json`  (body: the same JSON, `application/json`) → persist + apply LIVE (no reboot); return the new value.
Sinks the SB20's own shifter buttons and re-broadcasts each press as the bound action (an OBC id, or a
local erg nudge). Enabling starts the SB20 central in place. The ESP32 parses this one fixed shape (no
general JSON parser on-device — `buttonsFromJson`, host-tested), mirroring the nRF's index wire form.

## Calibration (F29, #347)

### `GET /calibrate/state` → normalized **Cal**
```json
{ "state":1, "pairs":40, "min_pairs":30, "residual_w":0.0, "enough":true,
  "dut_connected":true, "ref_connected":true, "coverage":[1,2,5,5,3,0], "devices":[] }
```
The same wizard view `GET /calibrate` renders as HTML (`CalibrationPage.h` `CalWizardView`), as JSON.
`state`: 0 idle, 1 collecting, 2 fitted (the nRF Cal characteristic's numbering). `coverage` is pairs per
band (`<100, 100-150, 150-200, 200-250, 250-300, 300+` W). In **idle**, `devices` is the wizard's picker
list **with addresses** — `[{"name":"ASSIOMA17039L","addr":"e6:20:90:8c:f3:fe","rssi":-61}, …]` —
because start pins both meters by address.

### Commands (each the legacy wizard's own route; replies are the wizard's HTML, which the SPA ignores)
- `POST /calibrate/start` — form `dut=<addr>&ref=<addr>` (two different addresses from `devices`).
  Persists a calibration boot and **reboots** into it (two meter centrals). The SPA picks both from
  dropdowns (`caps.calByAddress`); the nRF's Cal characteristic matches the reference by name instead.
- `POST /calibrate/finish` — fit the collected pairs in place (no reboot; a 303 to `/calibrate` the SPA
  does not follow). The SPA's **Save fit** posts finish, re-reads `/calibrate/state`, and only if it is
  `2` (fitted) posts save — so it never claims a save the board refused.
- `POST /calibrate/save` — form `name=<corrector name>` (optional) → persist the curve, switch to
  corrector mode, **reboot**. Refuses (no reboot) if nothing is fitted.
- `POST /calibrate/cancel` — clear the calibration boot and **reboot**.

## Workout and erg (mirror the GATT `WkCmd`s)

- `POST /workout/trainer` — form `name=<trainer>` (blank = erg off) → `{"ok":true,"reboot":true|false}`.
  Persists `trainerNameFilter` on top of the stored config; the erg client is started at boot, so a
  **changed** name reboots (like the `/setup` and LCD pickers), an unchanged one does not.
- `POST /workout/preset?key=4x8|ss3x12|vo25x3|endur45` → `loaded` / 400 `unknown preset` (text/plain).
- `POST /workout/start|pause|resume|skip|stop` → `ok` (text/plain).
- `POST /workout/bias?d=<±W>` → `{"bias_w":<new bias>}` — nudge the erg target live (no reboot), the web
  twin of the SB20 shifter's erg actions. One nudge is `-50..50` (else 400 `{"error":…}`); the running
  total is clamped to ±200 W. The SPA's ±10 W buttons send it.

## Not applicable to the ESP32
IMU recording (`recSetRate/recStart/recStop/recErase/recDownload`) — the ESP32 has no IMU, so
`HttpTransport.caps.recording = false` and the view hides the Track-recording card.

## Desk testing without a board
`python -m sb20proxy.qa.mock_board --port 8320` (from `code/`, `PYTHONPATH=src` in a worktree) serves
the exact `/app` bytes from `WebSpa.h` plus these routes with the firmware's field names
(`code/tests/test_mock_board.py` diffs them against the C++ serializers); `POST /_mock/reboot?down_s=6`
simulates a restart. See [`README.md`](README.md).
