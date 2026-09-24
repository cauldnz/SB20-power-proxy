# UI feature map — web and device

**Status: LIVING (first cut 2026-09-24; every placement decided by the owner 2026-09-24; issue #345).** Every user-facing feature on every surface: what
it does, what it touches, whether it has ever been seen working on hardware, **where it should live**
(the owner's decision, row by row), and the test that proves it. It exists so a testing session runs
from a map instead of a memory, and so "does this belong on the screen or in the web app?" is decided
once and written down. The runtime rules the features rely on are in
[`system-reference.md`](system-reference.md); the design decisions they descend from are in
[`../design/ui-architecture-review.md`](../design/ui-architecture-review.md) and
[`../code/findings/ui-unification.md`](../code/findings/ui-unification.md).

## 0. The surfaces

| Surface | Runs on | Renderer / transport | Reach | Code |
|---|---|---|---|---|
| **The shared SPA over HTTP** | any phone or PC on the board's WiFi, at `http://<board>/app` | one DOM app, `HttpTransport` → the ESP32 JSON routes | ESP32 boards only | `web/index.html` → embedded as `firmware/lib/proxy/WebSpa.h` |
| **The shared SPA over BLE** | a phone or PC with Web Bluetooth, from GitHub Pages | the same file, `BleTransport` → the nRF Bridge GATT | nRF only | `web/index.html`, `web/bridge-codec.js`, `firmware-nrf/GATT.md` |
| **The older per-route ESP32 pages** | `http://<board>/`, `/ui`, `/setup`, `/calibrate`, `/more`, `/workout`, `/report`, the portal | server-rendered HTML per route | ESP32 boards | `firmware/lib/proxy/WebApp.h`, `ConfigPage.h`, `CalibrationPage.h`, `Provisioning.h`, `WebRoutes.h` |
| **The LVGL head unit** | CYD (240×320, resistive touch), Waveshare S3-Touch (172×320, capacitive), Guition (320×480, capacitive) | LVGL v9 screens fed by the shared view model | the LCD boards | `firmware/src/ui/LvglUi.cpp`, `firmware/lib/proxy/UiModel.h`, `LcdUi.h` (actions) |
| **The OLED** | the C3 ride board (0.42" 72×40; 0.96" 128×64 variant) | four text rows from the same view model | the C3 | `firmware/lib/proxy/OledScreen.h`, `firmware/src/disp/OledDisplay.h` |
| **The serial bench console** | LCD builds over USB serial | `SCREEN`, `TAP x y`, `STATE`, `TRAINER`, `WATTY`, the CYD touch-cal commands, `CMP` | the bench, not the rider | `firmware/src/main.cpp` `lcdSerialConsole` |
| **nRF status LED + Garmin Connect IQ** | the XIAO bridge; an Edge/epix watch | LED colours; one CIQ screen | the track bike, not the SB20 stack | `firmware-nrf/src/main.cpp`, `firmware-nrf/ciq/` |

Facts that frame every row below:
- Every LCD env sets `USE_LVGL=1`; the canvas renderer (`LcdUi.h`/`LcdCanvas.h`) ships on no board and is a
  host-test twin that has drifted from LVGL in three visible ways (§3).
- The shared SPA is served from the ESP32 at `/app` (hardware-verified 2026-07-05/11) and is
  **mentioned in no tester-facing document**; the beta docs send testers to `/`, `/diag`, `/report`.
- The SPA over HTTP has **never been used on a ride**; sessions 10 and 12 planned it and never ran.

## 1. Principles (recorded decisions, plus the owner's of 2026-09-24)

1. **LVGL is the single on-device renderer; the SPA stays its own DOM app**; they share the contract
   (schema-generated codecs, design tokens) and nothing else (design review §7, locked 2026-07-11).
2. **`/app` is the one web UI.** The older per-route pages were kept only until the SPA-over-HTTP path was
   hardware-verified; it was (2026-07-11). They retire once §2 confirms `/app` has parity for every
   feature the owner places on the web (owner, 2026-09-24).
3. **The web app must be as good for a live ride as for setup** (owner, 2026-09-24): a phone on the bars
   showing `/app` must be a usable ride display, not just a settings page.
4. **The north star** (`ROADMAP.md`) is a ride with "no Stages app, no phone, no agent in the loop", so
   the device screen must carry everything a ride needs; the web carries what needs a keyboard, a
   network, an account or a large display.
5. **Placement is decided per feature by the owner** (2026-09-24), recorded in §2's last column. A
   feature placed on a surface that lacks it becomes a Next item; a feature placed off a surface that
   has it is removed or hidden there so the surface stays intentional.
6. **Both pickers list every BLE device and filter to power meters and trainers** (owner, 2026-09-24);
   heart-rate straps join the filter later (F46). Today the device list is pre-filtered to CPS/FTMS and
   the web cannot pin by address (#347).
7. **Peloton needs no class picking** (owner, 2026-09-24): the head unit detects the class the rider has
   started, the way qz does (poll the account's latest workout until it is in progress), fetches its
   timeline once and runs it; the web holds only the sign-in. Recipe: `code/findings/peloton-integration.md`.

## 2. Feature inventory and placement

Columns: **Device** = the LVGL screens (and the OLED where noted) today · **Web** = the shared SPA at
`/app` today (HTTP unless noted BLE) · **Legacy** = the older ESP32 pages today · **Verified** =
strongest hardware evidence (date, source) or *desk*/*never* · **Proposed** = the recommended home
(**D** device screen, **W** web app, **D+W** both, **T** tooling/curl only, **—** nowhere) · **Decision** =
the owner's call, taken group by group on 2026-09-24 (every row is decided; §2i lists what the decisions require). Defects found while mapping are marked ⚠ and listed in §3.

### 2a. Onboarding and WiFi

| id | Feature | Device today | Web today | Legacy today | Verified | Proposed | Decision |
|---|---|---|---|---|---|---|---|
| F01 | Join the board to WiFi (captive portal on `Setup-XXXX`, `172.29.4.1`: network list, password, save → reboot) | LCD: QR join screen + SSID/PIN; OLED: SSID/PIN rows | — | portal page | HW: C3 07-11, CYD 07-25, Guition 09-23; a phone scanning the QR: never recorded | D (QR/PIN) + portal page | D + portal |
| F02 | Forget WiFi / re-provision | — (no BOOT-button path exists) | — | `POST /forget` (legacy only) | never | D (More → Forget WiFi, confirm) + W | D + W |
| F03 | Ride-mode WiFi-off until power-cycle | — | — | `GET/POST /wifi/off` (legacy) | desk (GET only) | D (More toggle) + W | D + W |
### 2b. Source and identity

| id | Feature | Device today | Web today | Legacy today | Verified | Proposed | Decision |
|---|---|---|---|---|---|---|---|
| F04 | Pick the source meter from a scan list, pinned by address | Setup screen: filtered list (CPS/FTMS only), tap + Save → reboot | list shown; ⚠ cannot pin by address (`mergeSpaConfigForm` never sets `meterAddress`) | `/setup` list + hidden `addr`, Save → reboot | HW: every session (legacy, LCD) | D+W | D + W; the picker lists every BLE device and filters to PM / trainer (HR later, F46) |
| F05 | Rescan for devices | Setup → Rescan (scan-window boost) | ⚠ `POST /setup/scan` is a phantom (route is GET-only; the call is dead code) | `GET /setup/scan` | HW: CYD 07-03 | D+W | D + W |
| F06 | Source **name filter** (when not pinned) | — | `src_filter` (inert once an address is pinned) | `name` field | HW 07-11 (HTTP), 07-11 (BLE) | W | W |
| F07 | Single-sided ×2 (a surviving R crank) | — | checkbox | checkbox | HW BLE 07-11; HTTP sent 07-11 | W | W |
| F08 | Mode: spoof ↔ corrector (reboots) | More: read-only row | selector + reboot hint | none to set; `/more` read-only | HW 07-11 | W (device shows it) | W; device shows |
| F09 | Spoof identity: name + serial | More: read-only row; OLED row 4 (the 0.96" panels) | name, editable in both modes (blank = the board's MAC-derived `Stages 9NNNN`, #330; the `Stages 62144` lock string is gone); no serial | name + serial (blank = the per-board default) | HW BLE 07-11; HTTP preserved 07-11; per-board default host-tested only (#330) | W (device shows it); default is per-board (#330) | W; device shows |
| F10 | Trainer (erg target) selection | Setup: FTMS rows; More → Trainer shows the configured name (the literal `not set` of #346 fixed in #330) | ⚠ "Set trainer" → `POST /workout/trainer` 404 (#347) | `/setup` FTMS tap-list | HW legacy C3 07-05; LCD pick twin-proven 07-05 | D+W | D + W; same picker rule as F04 |
| F11 | Reset source + identity to defaults | — | — | `POST /setup/reset` + confirm | CSRF probe only | W | W |
| F12 | Spoof radio BLE / ANT+ (nRF) | — | BLE: selector, ANT disabled | — | never (S340-gated) | W (BLE) | W (BLE) |
| F46 | Heart-rate strap as a source (Heart Rate Service `0x180D`): listed under an HR filter in both pickers; bpm on the ride view | — | — | — | not built | D+W (later) | D + W, later (owner: "at some point") |
### 2c. Ride display

| id | Feature | Device today | Web today | Legacy today | Verified | Proposed | Decision |
|---|---|---|---|---|---|---|---|
| F13 | Live power hero, cadence, L/R balance | Ride: hero + cadence/balance cards; OLED rows | hero (3.4 rem) + Live card, 1 Hz; BLE 2 Hz | `/`: hero 4.6 rem + cards, 1 Hz | HW: LCD every board; SPA/HTTP mock values 07-05; legacy every session | D+W | D + W |
| F14 | Power history chart | Ride: 48-point line chart | ⚠ none | `/`: 90-sample canvas | HW LCD | D+W (add to `/app` for ride use) | D + W |
| F15 | Source / output names, link state, uptime, RSSI, erg trainer | Ride title bar + details pop-down (IN RSSI, OUT uptime + `erg … <trainer>`, #330) | source-link + uptime rows | `/` title + detail cards; `/status` `identity_default` / `source_pin` / `trainer` | HW: CYD 07-03, S3 07-04, Guition 09-23; the erg line host-compiled only | D+W | D + W |
| F16 | Current erg target, segment, time left **on the ride screen** | ⚠ only in the canvas twin (`RideView` strip never drawn by LVGL); LVGL shows it on the Workout screen only | ⚠ in the Workout card (7th of 10 cards), not the hero | none | desk | D+W (both need it on the ride view) | D + W |
| F17 | Erg link status (connected / controlled) | Workout screen erg line (4 states) | ⚠ "searching…" forever on HTTP (missing `erg_*` fields, #347); real on BLE | none | LCD: twin 07-05 | D+W | D + W |
| F18 | Keep the display awake through a ride; survive a board reboot | n/a (device) | ⚠ no Wake Lock; HTTP has no reconnect (errors swallowed, green dot never revoked); BLE reconnects with backoff | n/a | S13 R13 (BLE) | W (required for principle 3) | W |
| F19 | Identity + mode shown as text | More rows | `devName` = hostname only; mode in Settings | `/` title, `/more` rows | HW 07-02 (legacy) | D+W | D + W |
| F45 | Full-screen ride mode on the web: large numbers only (watts, cadence, target, time left, erg state), no cards, one tap to leave | n/a (the device is that already) | ⚠ none | `/` comes closest (hero 4.6 rem + cards) | never | W | W (owner: a must-have before riding with a phone on the bars) |
### 2d. Erg and workouts

| id | Feature | Device today | Web today | Legacy today | Verified | Proposed | Decision |
|---|---|---|---|---|---|---|---|
| F20 | Load a preset (4×8, SS 3×12, VO2 5×3, Endurance 45) | Workout picker (4 buttons) | Workout card preset | `/workout` picker | HW: CYD 07-03, HTTP 07-02, twin 07-05 | D+W | D + W |
| F21 | Start / Pause / Resume / Stop | Workout console buttons | verbs | `/workout` | HW: baseline 07-27; twin 07-05; **never on a real SB20** | D+W | D + W |
| F22 | Skip segment | Skip button | ⚠ none (BLE has no skip either) | `POST /workout/skip` | LCD twin | D+W | D + W |
| F23 | Unload / change workout | Change button (when loaded, not running) | ⚠ none | none | LCD 07-03 | D+W | D + W |
| F24 | Import a custom workout (ZWO / FIT / JSON) | — | ⚠ none | `/workout/load` textarea; `import_workout.py --post` | legacy + tooling | W | W |
| F25 | Per-rider FTP (scales `%FTP` and zone segments; presets assume 250 W) | — | — | — | never (ROADMAP Next 3) | W (set) + D (shown) | W set + D shown |
| F26 | Peloton class as the workout source (#342) | — | — | — | not built | W (login, class pick) → D (runs it) | W sign-in only; D detects the class the rider started (qz-style) and runs it |
| F27 | Shifter bias ±10 W | via the SB20 buttons (OBC actions 8/9) | ⚠ `POST /workout/bias` 404 (#347); works on BLE | none | BLE bench 07-05 | D (buttons) + W | D (buttons + screen) + W |
| F28 | Workout progress: profile bar chart, next block, total clock | Workout console (bar chart, `N of M`, clock) | segment + time left only | `/workout`: profile, next, total | HW LCD 07-03/04 | D+W | D + W |
### 2e. Calibration and corrector mode

| id | Feature | Device today | Web today | Legacy today | Verified | Proposed | Decision |
|---|---|---|---|---|---|---|---|
| F29 | Meter-to-meter calibration wizard (pick DUT + ref, collect, fit, save → corrector) | ⚠ Calibrate screen's only button (`Connect + start`) does nothing (#346) | ⚠ card dead on HTTP (`/calibrate/state` phantom; Start posts a name where addresses are required, #347); works on BLE | `/calibrate` 3-state wizard (addresses, coverage bars, fit table) | legacy: desk only; the corrector ride never run | W (device shows state only) | W; device shows state, no button |
| F30 | Portable calibration profile export / import (`/curve`) | — | profile card | none | HW 07-05 (HTTP), 07-11 (BLE) | W | W |
| F31 | Scalar scale / offset (nRF only) | — | BLE inputs; hidden on the ESP32 | — | HW 07-11 (hidden correctly) | W (BLE) | W (BLE) |
| F32 | Fit review (points, residual) | — | "N points" | full table | desk | W | W |
### 2f. Compare (A/B meters)

| id | Feature | Device today | Web today | Legacy today | Verified | Proposed | Decision |
|---|---|---|---|---|---|---|---|
| F33 | A/B compare: verdict, bias-by-torque chart, Bland–Altman | More → Compare screen (12 bands); ⚠ meter B is simulated (A×1.11) | Compare card, 3 s poll; ⚠ hook only on LCD builds, so the C3 says "waiting…" forever | none | desk (host tests; no on-panel record) | W (deep dive) + D (verdict) — revisit after real two-meter data | W deep dive + D verdict only; revisit after real two-meter data |
### 2g. OBC buttons

| id | Feature | Device today | Web today | Legacy today | Verified | Proposed | Decision |
|---|---|---|---|---|---|---|---|
| F34 | SB20 button → action binding; enable the shifter sink | — | buttons card (`/obc/buttons.json`, live) | `GET /obc` help text | HW: JSON via curl (sessions 11/13); the card itself never | W | W |
| F35 | Devmode / shifter-sink toggles; virtual press | — | — | `POST /obc/{devmode,shifter}/…`, `GET /obc/press` (curl) | HW curl sessions 11/13 | T (curl) or W Settings | W (Settings) + T |
### 2h. Diagnostics, report, updates, admin

| id | Feature | Device today | Web today | Legacy today | Verified | Proposed | Decision |
|---|---|---|---|---|---|---|---|
| F36 | Tester report: review on device → download / copy / email (`/report`, `/diag`) | — (report-v2 mockup never built) | — | `/report`, `/diag` | desk; beta docs point here | W | W |
| F37 | `/log`, `/stats`, `/status` (instruments) | — | client-side log card only | footer links; curl | HW every session | T | T |
| F38 | Firmware version + build SHA visible | ⚠ Version row exists but is unreachable (#346); OLED none | ⚠ none | `/` footer (`fw`); `/status` | HW `/status` | D+W (session 14 G0 needs it per board) | D + W |
| F39 | Push OTA / signed pull / nRF DFU | — | — | none (push OTA is `flash.ps1`) | HW OTA 09-23 | T | T |
| F40 | Reboot the board | — (every Save reboots) | — (every Apply reboots) | none explicit | — | D (More → Reboot) + W | D + W |
| F41 | Backlight brightness | More → Bright ⚠ stuck at 25 % (#346) | n/a | n/a | never (defect) | D | D |
| F42 | Touch calibration (CYD only) | More → Touch cal; BOOT ≥ 1 s; first boot | n/a | n/a | HW 07-03; refactor 07-26 not re-run | D | D |
| F43 | Watty Birds | serial `WATTY` only (no menu entry) | n/a | n/a | boot-safety 07-25; gameplay never recorded | D (hidden entry) | D (hidden entry) |
| F44 | IMU track recording (nRF) | LED colour | BLE recording card | — | live sampling 07-04 | W (BLE) | W (BLE) |
### 2i. What the decisions require

Rows whose decided home lacks the feature today, grouped into the work items `ROADMAP.md` carries:

- **WEBRIDE** (Now; issue #351): the web ride view — F14 chart, F16 target/segment/time-left on the hero,
  F18 Wake Lock + HTTP reconnect, F45 full-screen ride mode — plus the #347 fields and routes the ride
  view needs (F17 `erg_*`, F27 bias).
- **PICKER** (Next): both pickers list every BLE device and filter to power meters and trainers (HR
  later): the device list is pre-filtered today (F04, F10); the web cannot pin by address and its rescan
  is a phantom (F04, F05, #347).
- **WEBPARITY** (Next; then the legacy pages retire): rows placed on the web that only the legacy pages,
  or nothing, serve today: F02 forget WiFi, F03 WiFi-off, F11 reset to defaults, F22 skip, F23 change
  workout, F24 import, F29 the calibration wizard over HTTP (#347), F35 devmode + virtual press in
  Settings, F36 the tester report, F38 version, F40 reboot. F25 FTP and F26 Peloton land with their own
  items.
- **DEVROWS** (Next; after the bench pass): rows placed on the device that the screens lack: F02 Forget
  WiFi and F03 WiFi-off on More, F40 Reboot, F38 Version (#346), F27 bias ± on the Workout console, F25
  FTP shown; and the removals: F29 loses its dead button and keeps a state row, F33 shrinks to the
  verdict once real two-meter data exists.
- **HR** (Later): F46.

Rows already where they were placed need only the bench pass (§4) to move their Verified column.

## 3. What the map found

**Web (issue #347):** the SPA over HTTP calls `GET /calibrate/state`, `POST /workout/trainer`,
`POST /workout/bias` and `POST /setup/scan`, none of which the ESP32 serves; `/workout/state` lacks the
`erg_*` fields the trainer-link row needs; Calibrate Start posts a name where addresses are required;
no Wake Lock and no HTTP reconnect. `web/HTTP-API.md` documents the missing routes as existing;
`web/README.md` still calls the HTTP path "unverified until U4".

**Device (issue #346):** the brightness cycle shows 100 % and emits 25 % forever; More → Trainer was
the literal `not set` (fixed in #330); the Calibrate button falls through to `default`; the Version row
has a renderer and no table entry. To confirm with the bench camera: the Ride layout's absolute offsets (hero 60,
chart 140, cards 208) on the 480-tall Guition, which likely leaves the bottom third empty above the nav.

**Drift between the canvas twin and LVGL:** the twin has a live workout strip on Ride, a Firmware row
and a working three-state Calibrate wizard; LVGL has none of them. Tests on the twin prove nothing
about the panel (`ui-unification.md` U1).

**Coverage:** the host LVGL harness compiles only the 240×320 geometry; 172×320 and 320×480 are
untested on the host, and the Guition has only ever shown the Ride screen and the nav on camera.

**Docs:** `/app` is absent from every tester-facing document; the `system-reference.md` note that
the portal offers `/wifi/off` is wrong (the portal route table has neither `/setup` nor `/wifi/off`).

## 4. The test map

One row per feature, keyed to §2. **Where** = bench (no rider; fake meter, the FTMS trainer sim, the
bench camera, the serial console, a phone on the bench WiFi) or bike (session 14). **Instrument** =
what produces the evidence. A row passes when the criterion holds on every surface the owner placed
it on; a row for a surface the owner placed it *off* is a removal check ("not offered").

| id | Where | Instrument | Pass criterion |
|---|---|---|---|
| F01 | bench | phone camera on the LCD QR; `curl` the portal; serial | phone joins `Setup-XXXX` from the QR; portal lists networks; save → reboot → `/status` reachable on the LAN |
| F02 | bench | phone / `curl` | forget → portal returns; provisioning again works; no NVS residue in `/status` |
| F03 | bench | `/status` then `curl` timeout | after WiFi-off the board keeps advertising CPS (`crank_reader.py`) and HTTP is gone until power-cycle |
| F04 | bench | `fake_meter.py` + a second CPS advertiser + a non-CPS advertiser; camera + `TAP`; `/app` | both pickers list every BLE device and the PM/trainer filter narrows it; a tap + Save pins the *address* (`/status` shows it) on the device and on `/app` |
| F05 | bench | `TAP` on Rescan; `/app` Rescan | the list refills within the boost window; no 404 in the browser console |
| F06–F07 | bench | `/app` Settings, `/status`, `crank_reader.py` | filter and ×2 persist across a reboot and change the relayed watts as expected |
| F08 | bench | `/app` mode selector, `crank_reader.py` | corrector mode advertises our own name with no Stages service; spoof mode the reverse |
| F09 | bench | `/app`, `/status`, `crank_reader.py --scan` | the identity from the fleet table is advertised; two boards from one image differ (#330) |
| F10 | bench | FTMS trainer sim (`esp32c3-ftms-server`); camera; `/app` | picking the trainer on the device and on `/app` both persist to `trainerNameFilter`; More → Trainer shows it |
| F11 | bench | `/app` (once moved) or `/setup/reset` | defaults restored; `/status` shows the derived identity |
| F13 | bench → bike | `fake_meter.py` ramp; camera; a phone on `/app` | device and `/app` show the same watts within one refresh; cadence and balance present; then real pedals on the bike |
| F14 | bench | camera; `/app` | the chart advances over 60 s on both |
| F15 | bench | camera; `/app` | names, link dots and RSSI/uptime shown; title tap toggles details |
| F16 | bench | trainer sim + preset running; camera; `/app` | current target, segment and time-left visible **on the ride view** of both surfaces |
| F17 | bench | trainer sim; camera; `/app` | erg line walks all four states on the device; `/app` shows connected/controlled (needs #347) |
| F18 | bench | a phone on `/app` for 15 min; reboot the board mid-way | screen stays on; the page shows "reconnecting" and recovers without a manual reload |
| F19 | bench | camera; `/app` | identity, mode, source agree with `/status` on both |
| F20–F23 | bench | trainer sim; `TAP`; `/app` | load, start, pause, resume, skip, stop, change from each surface; the sim logs the target changes |
| F24 | bench | `/app` (once built) or `import_workout.py --post` | a ZWO and a FIT import load and run |
| F25 | bench | `/app` (once built) | FTP per rider changes the `%FTP` targets the sim receives |
| F26 | bike | Peloton Phase 0 → 1 (#342) | the head unit detects the Power Zone class the rider started and runs its timeline; nothing is picked by hand |
| F27 | bench | SB20 buttons or `obc_reader.py`; `TAP`; `/app` | ±10 W reaches the sim from the buttons, from the device console and from `/app` (needs #347) |
| F28 | bench | camera; `/app` | profile chart, next block and total clock present where placed |
| F29 | bench | `fake_meter.py` ×2 (or one meter + ref); `/app` on the nRF; legacy `/calibrate` on the ESP32 | a full collect → fit → save → corrector reboot on the web; the device shows the state and offers no dead button |
| F30 | bench | `/app` export → import | the curve round-trips byte-identically (`/curve`) |
| F31–F32 | bench | `/app` on the nRF | scalar inputs hidden on the ESP32, live on the nRF; the fit table renders |
| F33 | bench → bike | two real Assioma sets on one bike (session 14 S2) | the verdict is computed from real pairs, not the ×1.11 stub |
| F34–F35 | bench | `/app` buttons card + Settings; `obc_reader.py` | a re-bound button emits the new action live; the devmode toggle and the virtual press work from Settings without curl |
| F36 | bench | `/app` (once moved) or `/report` | download, copy and mailto each produce the same `/diag` text |
| F37 | bench | curl | `/log`, `/stats`, `/status` answer; `route_baseline.py` diff is 0/57 after any web change |
| F38 | bench | camera; `/app`; `/status` | the same build SHA on the device row, the web and `/status` (needs #346) |
| F40 | bench | device row; `/app` | reboot returns to the ride screen within 25 s with the config intact |
| F41 | bench | camera + `TAP` on Bright | four taps walk 25 → 50 → 75 → 100 and the panel visibly changes (needs #346) |
| F42 | bench | CYD + camera | the ritual completes; a corner tap lands within a few px; BOOT ≥ 1 s restarts it |
| F43 | bench | serial `WATTY` + camera | the game runs at speed; power flies the bird; no reboot on exit |
| F45 | bench | a phone on `/app` | ride mode shows watts, cadence, target and time left readable at arm's length; one tap returns to the cards; the screen stays awake (F18) |
| F46 | bench | an HR strap; both pickers | the strap appears only under the HR filter; bpm shows on both ride views (once built) |
| geometry | bench | camera on each board | Setup, More, Workout, Calibrate, Compare and the QR screen fit the panel on 172, 240 and 320-wide boards with no clipping or dead band |

## 5. How it is used

- **The bench pass** ([`../sessions/bench-ui-pass.md`](../sessions/bench-ui-pass.md)) runs every
  bench row above, agent-driven, before any rider time is spent; it files an issue per failure and
  records the evidence (camera frames, `SCREEN` dumps, browser console) in the run-sheet.
- **Session 14** takes the bike rows (F13, F26, F33 and the human-UX checks of whatever the owner
  placed on the device) as gates; its Annex references this map by id.
- **Placement changes** are the §2i work items in `ROADMAP.md` (WEBRIDE now; PICKER, WEBPARITY and
  DEVROWS next; HR later); the legacy pages retire in one PR once every web-placed row is green on `/app`.

## 6. Maintenance

Update a row in the same PR that changes the feature; add a row when a screen, card or route is
added (the doc guards will not catch a missing row, the bench pass will). When a §3 defect is fixed,
move its row's ⚠ to the Verified column with the date. Classed *living* in `PROJECT-MAP.md` §F.
