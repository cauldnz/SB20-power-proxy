# UI unification (the "U-series") — plan & living checklist

**Status:** IN PROGRESS (U0 ✅, U3 ✅ — in review; U1 reframed → U5; U2/U4 need scoping). Born from the
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
| **U5** | **Host LVGL test harness** (headless, in-memory) | 🆕 **proposed** | M | — |
| **U1** | One interaction model (reframed → *parity test*) | 🔄 **reframed** | S | U5 |
| **U2** | Schema→codegen for the remaining wire/data mirrors (= R2 widened) | 📋 needs scoping | M | — |
| **U4** | Unify onboarding/portal across panels | 📋 needs scoping | M | (U3) |

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

### U5 — Host LVGL test harness 🆕 (proposed; replaces the literal U1)
**Goal:** run `LvglUi.cpp` headless on the dev machine / in CI and assert on what it renders + what taps do —
so the *code that actually ships on the ride boards* gets desk-test coverage (today it has none).
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

### U2 — Schema→codegen for the remaining wire/data mirrors 📋 (needs scoping)
**Goal:** extend the Bridge-style **schema → codegen + golden vectors + CI parity** (R2:
`ui-schema/bridge.json` → `Proto.h`/`bridge-codec.js`) to the **other** hand-maintained ESP↔web mirrors that
can still drift — chiefly **`firmware/lib/proxy/WebJson.h`** (the ESP32 status/config JSON that `web/index.html`
parses). Today that JSON contract is hand-written on both ends. **Scoping needed:** inventory every
hand-maintained wire/data mirror (WebJson.h status, config compact-string, `/scan`,`/config` payloads),
decide which get a schema + golden vectors, and whether this rides `gen_bridge.py` or a sibling generator.
**Open question for the owner** below.

### U4 — Unify onboarding/portal across panels 📋 (needs scoping)
**Goal:** one onboarding *model + flow* across all panels. U3 already made `ProvisionView` shared (the
*data*). What remains is presentation + flow: LCD shows a QR + SSID/PIN screen, the OLED shows the same as
text, the web/captive portal has its own; and the join-fail / `obcDevmode` / "hold BLE off in the portal"
handling was patched per-panel (task #10). **Scoping needed:** define the shared onboarding state machine
(states: fresh → AP-up → joining → joined/failed) as pure code both renderers project, then per-panel
presentation only. **Open question** below.

---

## 5. Sequencing

```
   U0 ✅ ─── U3 ✅            (both shipped, independent)
                 │
   U5 (harness) ─┴─> U1 (parity test)      ── and unblocks safe canvas retirement
   U2 (wire codegen)   ] independent, need scoping
   U4 (onboarding)     ]
```

- **U0, U3:** shipped (PRs #263, #265 — in review).
- **U5 next** if we want to close the interaction-coverage gap; **U1** follows it.
- **U2, U4** are independent tracks; each needs a scoping pass before code.

## 6. Open decisions for the owner (please weigh in)

1. **U1/U5:** Agree to *replace literal U1 with U5 (build the harness) → U1-as-parity-test*? Or leave LVGL
   input untested for now and close U1 as "already satisfied by shared `UiAction`/`lcdExecute`"?
2. **Canvas renderer:** keep indefinitely as the host-test reference (current decision), or retire it *once
   U5 lands* (then LVGL is the only renderer and it's directly tested)?
3. **U2 priority:** is the ESP↔web JSON drift risk (WebJson.h) worth a schema+golden-vector pass now, or
   parked until a concrete drift bite? (No incident yet — this is preventative.)
4. **U4 priority:** unify onboarding now, or is the per-panel portal "good enough" post-U3 (the data is
   already shared)?
5. **Ordering:** if we proceed, my suggested order is **U5 → U1 → (U2 | U4)**. Agree, or reprioritise?

*Nothing below U0/U3 is started — this doc is for review first.*
