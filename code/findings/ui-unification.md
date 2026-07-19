# UI unification (the "U-series") — plan & living checklist

**Status:** IN PROGRESS (U0 ✅, U3 ✅, U5 ✅ merged; **U2 ✅, U4 ✅ in PR #268**; U1 reframed as a parity
test — unblocked by U5 and optional). Born from the
task-#11 review ("unify UI across Web + all ESP32 boards", 2026-07). This doc is the plan's home — it was
previously only a set of one-line labels in a task list, which is why "what is U1" was unanswerable. The
structural sibling of [`architecture-remediation.md`](architecture-remediation.md) (the R-series): tick
boxes here as slices ship.

> **How to read this:** the UI is *already mostly unified* at the data/execution layer. The U-series
> closes the **remaining parallel paths** — one design-token source, one view-model, one wire contract, one
> onboarding flow — and adds the **test coverage** that lets us retire the last duplicate renderer safely.

---

## 1. The surfaces we render

| Surface | Boards | Renderer | Input model |
|---|---|---|---|
| **Web SPA** (`web/index.html`) | any phone/PC (Web BLE + served from ESP at `/app`) | HTML/CSS/JS | DOM events |
| **LVGL head-unit** (`src/ui/LvglUi.cpp`) | CYD, S3-Touch — the **ride hardware** | LVGL v9 widgets | native widget callbacks → `UiAction` |
| **Canvas head-unit** (`lib/proxy/LcdUi.h` + `LcdCanvas.h`) | *(superseded env only — kept as the host-test reference)* | pure RGB565 rasteriser | `lcdHandleTap(x,y)` → `UiAction` |
| **C3 OLED** (`lib/proxy/OledScreen.h`) | C3 + 0.42"/0.96" OLED | 4 text rows (U8g2) | *(status display; no touch)* |
| **Full-screen games** (`WattyBird*.h` → `lcd.blit`) | CYD, S3-Touch (**takeover**) | direct RGB565 framebuffer — **NOT LVGL** (sanctioned exception, see §1a) | power/cadence + touch |

### 1a. Rendering architecture — LVGL for UI screens, direct-blit ONLY for games

**The rule (canonical):** on the ride hardware (CYD, S3-Touch) **every UI screen is an LVGL screen**
(`src/ui/LvglUi.cpp`, built with `lv_obj_*`, themed from `LcdTheme.h`, driven by `LcdViews` + emitting
`UiAction`). The `LcdCanvas` rasteriser (`lib/proxy/LcdUi.h`) is the **host-test reference** — it renders
the same view-models headlessly for pixel/tap tests, but **does not ship as the device UI renderer.**

**The one sanctioned exception — full-screen games.** A real-time arcade screen (Watty Birds) renders
into an `LcdCanvas` and `lcd.blit()`s the whole frame, bypassing LVGL. This is deliberate and correct:
LVGL's widget/retained-mode model is the wrong tool for a 30 fps scrolling game (confirmed by the
CYD game-engine research — `calint/bam` uses a direct framebuffer too). A game is a self-contained
takeover, not part of the UI nav, so it doesn't need LVGL theming/widgets.

**Therefore, for anything that is a *screen of the UI* (data, controls, nav):**
- ✅ build it in **`LvglUi.cpp`** (`buildX()` + an `LcdScreen::X` + nav wiring), themed + `UiAction`-driven;
- ✅ add an **`LcdCanvas` render** as the *host-test reference* (optional but matches Ride/Setup/… ) and
  a **`native-lvgl` test** that the LVGL screen renders + reacts;
- ❌ do **not** ship an `LcdCanvas`→`lcd.blit` takeover as the on-device renderer for a UI screen.

**Resolved (2026-07-16):** the **#10 Compare screen** is now a proper **LVGL screen** — the debt above is
paid. `buildCompare()` in `LvglUi.cpp` builds the cards + verdict + an `lv_chart` bias-by-**torque**-band
line, fed from a shared `CompareView` (`UiModel.h`, so it stays free of `LcdCanvas`/`LCD_PANEL`) that
`CompareService::fillView` fills; it's reached via **More → Compare** (`LcdScreen::Compare`), host-tested
by `native-lvgl` (`test_compare_screen_renders`), and the direct-blit takeover + its serial `CMP`/`CMPSHOT`
frame-grab were removed. Serial `CMP` now just navigates to the LVGL screen; `SCREEN` captures it like any
other. The chart's band count is **derived** from `MeterCompare::kTorqueBands`, so the head-unit and
`GET /compare` can never silently show different torque domains.

**Correction (2026-07-17):** this section previously claimed `MeterCompareRender.h` stayed as the pure
"host-test reference (mirroring how `LcdUi.h` references the other screens)". That was **not true as
written** — nothing but its own test ever called it, `lcdRenderAll` had no `Compare` case, and it had
already drifted (it rendered *power* bands while the live screen moved to *torque*). It has been deleted.
The pure, host-tested layer for Compare is `MeterCompare.h` (the math) + `CompareService.h` (the
lifecycle); the LVGL screen is the only renderer, exactly as the rule above intends.

## 2. What is ALREADY shared (don't rebuild these)

```
                design/tokens.json ──gen_tokens.py──> web CSS · WebUi CSS · LcdCanvas RGB565 · LcdTheme.h(LVGL)
                                                             [U0 closed the LVGL gap]

   gesture ─┬─ LVGL widget cb ─┐
            ├─ lcdHandleTap ────┼──> UiAction (typed intent) ──> lcdExecute(action)  ← ONE executor
            └─ web fetch/POST ──┘        [shared enum]              [device logic, single source]

   device state ──buildLcdViews──> LcdViews { RideView, ProvisionView, WorkoutView, ... }  ← ONE view-model
                                       └─ OLED projects RideView/ProvisionView too [U3 closed this]

   ESP ⇆ web JSON:  ui-schema/bridge.json ──gen_bridge.py──> Proto.h · bridge-codec.js + golden vectors
                    (the Bridge contract — R2). WebJson.h status/config is still hand-written [U2 target]
```

- **Design tokens:** one `design/tokens.json` → generated into every frontend (U0 added the LVGL consumer).
- **Intent + execution:** every surface produces a typed **`UiAction`**, and one **`lcdExecute`** does the
  device work. This *is* the "one interaction model" — see U1.
- **View-model:** `buildLcdViews` fills `LcdViews`; the OLED now projects from the same `RideView`/
  `ProvisionView` (U3).
- **Wire contract (partial):** the Bridge GATT is schema-generated with golden vectors (R2); other JSON
  mirrors are not yet (U2).

## 3. What is still parallel (the U-series targets)

1. ~~LVGL palette hand-duplicated~~ → **U0 (done)**.
2. ~~OLED had its own bespoke view-model path~~ → **U3 (done)**.
3. **LVGL input is untested** while the tested tap-logic (`lcdHandleTap`) is canvas-only → **U1/U5**.
4. **`WebJson.h`** (ESP status/config JSON) is hand-written, can drift from the web SPA → **U2**.
5. **Onboarding/portal presentation** differs per panel; join-fail/devmode handling was patched per-panel
   → **U4**.

---

## 4. The slices

| # | Title | Status | Effort | Depends on |
|---|---|---|---|---|
| **U0** | LVGL palette → token codegen (`LcdTheme.h`) | ✅ **done** (PR #263) | S | — |
| **U3** | Fold the OLED onto the shared view-model | ✅ **done** (PR #265) | S–M | — |
| **U5** | **Host LVGL test harness** (headless, in-memory) | ✅ **done** (PR #267) | M | — |
| **U1** | One interaction model (reframed → *parity test*) | 🔄 **reframed — now unblocked** | S | U5 ✅ |
| **U2** | Schema→codegen for the remaining wire/data mirrors (= R2 widened) | ✅ **done** (PR #268) | M | — |
| **U4** | Unify onboarding/portal across panels | ✅ **done** (PR #268) | S | (U3) |

### U0 — LVGL palette → token codegen ✅
Done. `design/gen_tokens.py` now generates `firmware/src/ui/LcdTheme.h` (`lv_color_hex` accessors) as a 4th
consumer, killing the hand-maintained palette copy in `LvglUi.cpp`. CI (`test_tokens_sync.py`) guards it.
**Decision recorded:** the **canvas renderer stays** (it is the only host-testable rendering path) — so U0's
original "delete canvas env + LCD_BANDS" is *dropped*; only the codegen shipped. See decisions.md 2026-07-13.

### U3 — Fold the OLED onto the shared view-model ✅
Done. `RideView`/`ProvisionView` moved to a shared `lib/proxy/UiModel.h` (compiled by every panel build);
`formatOledLines` gained a struct-based projection; the OLED task now fills the same model the LCD does.
**Deliberately out of scope:** merging the *builder* (`buildLcdViews`) — it would drag workout/cal/meter deps
into the lean OLED build. Left as a possible follow-up.

### U5 — Host LVGL test harness ✅ (shipped 2026-07-13, PR #267; replaces the literal U1)
**SHIPPED.** `pio test -e native-lvgl` compiles the real `src/ui/LvglUi.cpp` on the host and renders it into
an in-memory RGB565 framebuffer via the `LvglDriverHooks{flushArea, readTouch}` seam — with a ~40-line host
`<Arduino.h>` shim (`firmware/test/lvgl_shim/`) and `test_build_src=yes`. 3 tests green (Ride renders, nav
tap switches screens, view-model reaches pixels); runs in CI. **Gotcha:** `pio test` skips `src/` unless
`test_build_src=yes` — else `build_src_filter` never runs (see decisions.md 2026-07-13). More screen/
interaction tests are cheap from here.

**(Original goal, for the record):** run `LvglUi.cpp` headless on the dev machine / in CI and assert on what
it renders + what taps do — so the *code that actually ships on the ride boards* gets desk-test coverage.
**Feasibility (researched 2026-07-13):** solid. LVGL v9 renders fully in-memory (no SDL/X11); the existing
`LvglDriverHooks{flushArea, readTouch}` seam is the capture/inject point. Recommended shape: a dedicated
`pio test -e native-lvgl` env that compiles `LvglUi.cpp` against a memory-buffer flush hook + a scripted
touch hook, asserting in-memory (mirrors how `LcdCanvas` is tested). The real work is a ~40-line `Arduino.h`
shim (millis/Serial/heap_caps→malloc) + pulling the generated Inter fonts into the build — **not** the
rendering. Bonus over canvas: taps go through the real `readTouch` hook, so a test can *tap a button and
assert the emitted `UiAction`*. Fallback: LVGL's own `lv_test_screenshot_compare` golden-PNG module.
**Effort:** medium (~½ day to first green test; cheap per-test after). **Payoff:** unblocks U1-as-parity, and
once it exists, *retiring the canvas renderer becomes safe* (closes the old U0 pt.2 deletion cleanly).

### U1 — One interaction model 🔄 (reframed)
**Original label:** "LVGL defers to pure `lcdHandleTap`." **Finding (2026-07-13): implementing that
literally is a regression, not an improvement.** Both renderers already converge at the right layer:

| | `lcdHandleTap` | LVGL callbacks |
|---|---|---|
| runs on the **ride boards**? | ❌ (canvas-only) | ✅ |
| **host-tested**? | ✅ (12 tests) | ❌ |

Both emit the **same `UiAction`** and share **`lcdExecute`** — that is "one interaction model." Forcing LVGL's
touches through `lcdHandleTap` means bypassing LVGL's widget system (losing press-states, scrolling,
gestures) to reach the testable-but-coordinate-based path. Wrong direction. The *real* issue the label was
groping at is a **coverage/trust gap** (the running code isn't tested; the tested code doesn't run), fixed
from the other side by **U5**. **Reframed U1 (do after U5):** a **parity test** asserting `lcdHandleTap` and
the LVGL callbacks emit the *same `UiAction` for the same control on each screen*, so the two production
paths can never silently diverge. *(Or, once LVGL is directly tested, make `lcdHandleTap` explicitly
canvas-only and the question dissolves.)*

### U2 — Schema→parity for the web-JSON mirror ✅ (PR #268)
Done — the R2 schema-parity idea, widened to the ESP↔web JSON contract. **`ui-schema/web-json.json`** is the
one source of the `/scan` `/config` `/curve` field names; **`code/scripts/gen_webjson.py --check`** fails if
either side drifts from it — (1) `firmware/lib/proxy/WebJson.h` must emit exactly the schema's keys, (2)
`web/index.html` must reference every field, (3) the generated `ui-schema/web-json.md` reference stays in
sync. Guarded by `code/tests/test_webjson_sync.py` + the CI `bridge-parity` job. **Scope note:** this is the
*contract linter* (safe, no rewrite of the serializers); generating the serializer bodies themselves is a
possible future step. `/status` isn't in the schema yet (Status.h) — add if it earns it.

### U4 — Unify onboarding/portal across panels ✅ (PR #268)
Largely delivered by **U3** (the onboarding *data* — `ProvisionView`: apSsid/pin/url — is already shared, and
per-panel *presentation* rightly differs). This PR closes the last gap: the one un-shared, un-tested
onboarding primitive — the **`WIFI:` QR payload** a phone scans to join the setup AP — is now
**`lib/proxy/Onboarding.h::wifiQrPayload()`**, pure + host-tested, with **proper `WIFI:`-grammar escaping**
(the old inline `snprintf` didn't escape `;`/`:`/`,`/`\`/`"` — a latent QR-corruption bug if an SSID/PIN ever
held one). LvglUi consumes it. A deeper onboarding *state machine* was considered unnecessary — the state is
just `portal` + `wifiUp`, already cleanly derived per panel.

---

## 5. Sequencing

```
   U0 ✅ ─── U3 ✅ ─── U5 ✅        (merged)
                          │
                          └─> U1 (parity test — unblocked, cheap) + safe canvas retirement
   U2 ✅  U4 ✅  (PR #268)
```

- **U0, U3, U5:** shipped + merged (PRs #263, #265, #267).
- **U2, U4:** in PR #268 (this branch).
- **U1** (parity test) is the only remaining item; do it whenever, or close as satisfied.

## 6. Open decisions for the owner (please weigh in)

1. **U1:** the only U-series item left. U5 made LVGL testable, so the small U1 parity test (assert
   `lcdHandleTap` and the LVGL callbacks emit the same `UiAction` per control) is now cheap — do it, or
   close U1 as satisfied by the shared `UiAction`/`lcdExecute` layer?
2. **Canvas renderer:** keep indefinitely as the host-test reference (current decision), or now that U5 has
   landed (LVGL directly tested), retire it in a future pass?

*U0/U3/U5 merged; U2/U4 in PR #268. Only U1 remains an owner decision.*
