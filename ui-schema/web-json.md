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

## `GET /calibrate/state` (renderCalStateJson)

The calibration wizard's view as JSON for the SPA's calibrate card (#347): the same CalWizardView GET /calibrate renders as HTML. `state` is CalState (0 idle, 1 collecting, 2 fitted), the nRF Cal characteristic's numbering. `devices` (Idle only) carries ADDRESSES because POST /calibrate/start pins the DUT and the reference by address; its inner keys name/addr/rssi are `nested`.

| field | type | meaning |
|---|---|---|
| `state` | int | 0 idle, 1 collecting, 2 fitted |
| `pairs` | int | paired DUT/reference samples so far |
| `min_pairs` | int | pairs needed before a fit |
| `residual_w` | number | mean residual after the fit (W); 0 until fitted |
| `enough` | bool | enough pairs and coverage to fit |
| `dut_connected` | bool | the meter being corrected is linked (collecting) |
| `ref_connected` | bool | the reference meter is linked (collecting) |
| `coverage` | array | pairs per power band (<100, 100-150, ... 300+) |
| `devices` | array | Idle only: [{name, addr, rssi}], the DUT/reference picker, by address |

## `GET /workout/state` (renderWorkoutStateJson)

The workout cursor (WorkoutEngine.h renderWorkoutJson: loaded, running, paused, seg_index, seg_count, seg_label, seg_target_w, seg_remaining_s, total_elapsed_s, ... outside this contract) with the erg leg appended (#347, F17/F27). Only the appended keys are checked here.

| field | type | meaning |
|---|---|---|
| `erg_connected` | bool | the FTMS trainer link is up |
| `erg_controlled` | bool | the trainer granted control; workout targets drive it |
| `bias_w` | int | the rider's live nudge on the erg target (W, clamped to +/-200) |
