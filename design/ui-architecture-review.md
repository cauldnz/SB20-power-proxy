# UI architecture review — Web + all ESP32 boards

**Status:** REVIEW + PLAN, authored 2026-07-11 for owner sign-off. **This is the design of record, not a
status report** — much of it has since shipped. For what actually landed and what is still open, read
[`code/findings/decisions.md`](../code/findings/decisions.md) (the 2026-07-13 U0/U2/U4/U5 entries),
[`code/findings/ui-unification.md`](../code/findings/ui-unification.md), and
[`code/findings/architecture-remediation.md`](../code/findings/architecture-remediation.md) (R2).

> **Rescued to `main` 2026-07-24.** These two `design/` docs lived only on the unmerged branch
> `docs/ui-architecture-review` while `code/scripts/gen_bridge.py` and `.github/workflows/tests.yml`
> both cited `design/ui-schema-design.md` by path — a dangling reference one branch-deletion from
> being lost. Imported verbatim; only this banner was added.
>
> **One nuance worth flagging:** §Axis B here lands on *"canvas becomes purely the host test-oracle"*,
> which the **2026-07-13 U0 decision confirmed** — the canvas renderer is **kept** as the only
> host-unit-testable rendering path. A later refinement commit on the source branch argued to *retire*
> canvas draw; treat that as superseded by U0. (The new `native-lvgl` env now tests LVGL directly, which
> makes eventual retirement safe — but it has not been done.)

Nothing here was implemented at authoring time — this is the
"how should we handle UI across web and every board so it's maintainable" review you asked for, with a
target architecture and a phased plan. Decisions you need to make are in **§6**.

**Scope:** the four UIs today — the **web SPA** (`web/index.html`), the **S3-Touch** LCD (172×320), the
**CYD** LCD (240×320), and the **C3 OLED** (72×40, now a 128×64 on the new board) — plus the wire contracts
that feed them.

---

## 1. TL;DR

The situation is **better than "four separate GUIs" but worse than it should be**, and the fix is mostly
*consolidation of things that already almost line up*, not a rewrite:

- **Good news already true:** the LCD **view-models are shared** (S3↔CYD via `lib/proxy/LcdUi.h`), and
  **LVGL is already the single on-device renderer for both LCD panels**. The web SPA is **one file** that
  serves BLE *and* HTTP through a clean `Transport` seam + a `caps{}` capability object.
- **The real waste:** the **same five screens are rendered twice** on-device (~1,400 LOC: the canvas
  renderer in `LcdUi.h` *and* the LVGL renderer in `LvglUi.cpp`), **interaction logic is duplicated**
  (`lcdHandleTap` vs six LVGL callbacks, hand-synced), the **OLED is a wholly separate tiny UI**, and the
  **same data shapes + wire formats are hand-declared 3–4 times** (C++ view structs, JS normalized
  objects, `Proto.h` binary, `WebJson.h` JSON, a Monkey-C mirror).
- **The one-line thesis:** *don't* try to share **render code** across DOM (web) and framebuffer (device) —
  that's a false economy across JS/C++. **Do** share the four things that genuinely should be single-source
  and are currently copied by hand: the **data schema**, the **wire codecs**, the **capability flags**, and
  the **design tokens** — generate those into each target. On-device, collapse to **one renderer + one
  interaction source of truth**, and make the **OLED a capability-scaled view of the same models**, not a
  separate program.

Net: we keep the deliberate differences (screen size, touch vs none, curve vs scalar correction, IMU or
not) as **data/flags**, and delete the accidental duplication.

---

## 2. Current state (condensed)

### 2a. Web SPA — `web/index.html` (726 LOC)
- Single scrolling page of `.card` panels; **no per-transport forks**. A `Transport` interface
  (`index.html:206`) has two impls — `BleTransport` (Web Bluetooth → nRF Bridge) and `HttpTransport`
  (`fetch` → ESP32) — that both translate to the **same normalized objects** (`Status/Config/Scan/Cal/Wk/
  Rec`). `pickTransport()` auto-selects. **This is the strongest seam in the whole codebase.**
- Capability adaptation is a small `caps{}` object per transport (`recording/buttons/antCapable/
  scalarCorrection`); the view adds/removes UI from ~4 booleans instead of forked code.
- Coupled to firmware three ways: the SPA is **byte-inlined** into `WebSpa.h` by `gen_spa_header.py`
  (CI-enforced in sync via `test_spa_sync.py`); a **JSON field-name contract** (`HTTP-API.md` ↔
  `WebJson.h`); and **hand-mirrored binary codecs** (`Proto.h` ↔ the JS `parse*/pack*`).

### 2b. On-device — three display families
| Family | Panel | Renderer today | Env |
|---|---|---|---|
| S3-Touch | 172×320, cap-touch | **LVGL** (`esp32s3-touch` canvas env is **⛔ SUPERSEDED**) | `esp32s3-pio*` |
| CYD | 240×320, resistive-touch | **LVGL** | `esp32cyd*` |
| C3 OLED | 72×40 → 128×64 | **text rows** (`formatOledLines`) | `esp32c3-oled*` |

- **Shared (S3↔CYD):** the whole view-model + action layer — `RideView/WorkoutView/SetupView/MoreView/
  CalWizardView/ProvisionView`, `UiAction`, `LcdUiState`, layout constants, pure format helpers
  (`lib/proxy/LcdUi.h`, ~155 LOC). `main.cpp buildLcdViews()` fills them once per frame.
- **Duplicated:** every screen is rendered twice — the pixel **canvas** renderer (`LcdUi.h`, ~518 LOC) and
  the **LVGL** widget renderer (`LvglUi.cpp`, ~880 LOC). Interaction is duplicated too: one pure
  hit-tester `lcdHandleTap()` **vs** six LVGL `lv_event_cb`s + `navTo`, kept in lock-step by hand.
- **OLED stands apart:** `OledScreen.h formatOledLines()` returns 4 short strings from a 3-state enum;
  it does **not** touch `LcdViews`/`UiAction`. It's a third, ~30-LOC UI. (And it still assumes 72×40 — the
  new 128×64 panel is 2× the room and unmodeled.)
- **Size/capability differences today** are handled reasonably: `LCD_W/H` constants (compile-time) or LVGL
  runtime `hor/ver`; a touch-cal ritual gated `#if LCD_DRIVER_CYD`; two *different* no-PSRAM strategies
  (banded canvas vs LVGL partial buffer) that coexist, with `LCD_BANDS=4` now **dead config** on the LVGL
  CYD env.

### 2c. The renderer situation is the crux
`LvglUi.h` itself says LVGL "replaces the hand-rolled LcdCanvas renderer **on device**; the pure renderer
stays host-tested and drives nothing here." So the *intended* end-state is already: **LVGL on device, canvas
as the host-side screenshot/BMP test-oracle.** We are paying to maintain both as if they were peers, plus a
deprecated `esp32s3-touch` canvas build env, plus dead `LCD_BANDS`.

---

## 3. The problems, ranked by maintainability cost

1. **Wire/data shapes declared 3–4× by hand** (highest blast radius). Adding one field means editing
   `Proto.h` (binary), the JS `parse*/pack*`, `WebJson.h` (JSON), `HTTP-API.md`, and the C++ view struct —
   with only prose ("must agree byte-for-byte") as the guard. This is already flagged as **R2** in
   `architecture-remediation.md`.
2. **Two on-device renderers for the same 5 screens** (~1,400 LOC), where one (LVGL) is the only one that
   ships and the other (canvas) is really a test-double + a deprecated env — but they're maintained as
   equals and drift.
3. **Interaction logic duplicated** — pure `lcdHandleTap` vs scattered LVGL callbacks; e.g. the
   brightness-cycle 25→100 rule is copy-pasted in both.
4. **OLED is a separate program** — no shared view models, and it's already stale for the new 128×64 panel.
5. **View-model vocabulary declared twice** across the C++/JS boundary (same semantics, zero shared
   definition; nothing enforces "nav mirrors the web UI").
6. **Design tokens duplicated** — `design/tokens.json` is hand-copied into `LcdCanvas.h` (RGB565) and
   `LvglUi.cpp` (hex); visual identity can silently diverge.
7. **Portal/onboarding is inconsistent three ways** (LVGL QR screen, OLED text, canvas none), and the
   `IProvisioningDisplay` seam is only implemented by a serial stub.

---

## 4. Target architecture

Two independent axes. **Don't conflate them** — the answer is different for each.

### Axis A — share the *contract*, not the *pixels* (web ⇄ device)
Rendering can't be shared across DOM/JS and framebuffer/C++, and forcing it would be worse on both. What
**should** be single-source (and today is copied) is the contract layer. Make **one schema the source of
truth** and **code-generate** every mirror:

```
                 ┌─────────────────────────────────────┐
                 │  ui-schema/  (the single source)     │
                 │  • message shapes (Status/Config/…)  │
                 │  • field types + wire offsets        │
                 │  • capability flags enum             │
                 │  • design tokens (colors/typography) │
                 └───────────────┬─────────────────────┘
       codegen  ┌────────────────┼───────────────────┬───────────────┐
                ▼                ▼                   ▼               ▼
        Proto.h (C++ binary)  bridge-codec.js   WebJson.h (JSON)  LcdTheme.h /
        + golden vectors      (+ Node test)     serializers       tokens.css
```

- Kills problems **#1, #5, #6** at the root. This is the natural completion of the **R2** item already in
  the remediation plan (make the JS Bridge codec testable + assert both sides against shared golden
  vectors) — we widen it from "add a parity test" to "generate the mirrors."
- Start small and non-breaking: **a committed `ui-schema` (JSON/TOML) + a generator + golden vectors**, with
  the existing `Proto.h`/JS as the first assert targets (byte-for-byte), exactly like
  `test_wire_format_parity.py` does for ANT today.

### Axis B — one on-device renderer + one interaction model (S3 / CYD / OLED)
Commit to the direction `LvglUi.h` already declares:

- **LVGL is the single on-device renderer** for every panel (S3, CYD, **and the new C3 128×64** — LVGL has
  a monochrome/SSD1306 path, so the 0.96" OLED can join the *same* view-model world instead of being a
  fourth UI). The tiny 0.42" original OLED can stay on the text-row `formatOledLines` as an explicit
  "too-small-for-the-scene-graph" exception, or render a reduced LVGL scene — decision in §6.
- **Canvas becomes purely the host test-oracle** — its job is screenshot/BMP regression tests of the view
  models on the host, not an on-device or shippable renderer. Delete the **⛔ SUPERSEDED** `esp32s3-touch`
  env and the dead `LCD_BANDS` config.
- **One interaction source of truth.** Today `lcdHandleTap()` (pure, testable) and the LVGL callbacks both
  encode nav + actions. Pick the pure hit-tester as the authority and have the LVGL layer *call it* (feed it
  touch coords, apply the returned `UiAction`), OR generate the LVGL widget hit-regions from the same layout
  constants. Either way: **one place decides "tap here → this action."**
- **Screens are declared once, scaled by capability.** The `*View` structs already carry the data; the
  per-screen layout should read `LCD_W/H` + a small `caps` (touch?, panel size bucket, color depth) so
  "172 vs 240 vs 128×64" and "touch vs button" are **data**, not forked renderers. The OLED becomes "the
  smallest size bucket" of the same scene, showing a reduced subset.

### The layer cake we're aiming for
```
 domain/view models  ── shared C++ (LcdUi view structs)  ⟷  generated JS normalized objects
 capability flags     ── one enum, generated both sides
 design tokens        ── one tokens file, generated → LcdTheme.h + tokens.css
 renderer             ── device: LVGL (all panels) · host: canvas (test oracle) · web: DOM
 interaction          ── one pure hit/nav model (lcdHandleTap) the renderer defers to
 wire codecs          ── generated from schema, golden-vector-tested on every side
```

---

## 5. Phased plan (each phase is its own PR, independently shippable, keeps CI green)

**Phase 0 — stop the bleeding (cheap, no behavior change)**
- Delete the **⛔ SUPERSEDED** `esp32s3-touch` env + dead `LCD_BANDS` config; document canvas as
  "host test-oracle only." Removes the illusion that two renderers are peers.
- Generate `LcdTheme.h` + `tokens.css` from `design/tokens.json` (kills problem #6). One tiny generator +
  a sync test, mirroring `gen_spa_header.py`.

**Phase 1 — one interaction model**
- Make the LVGL layer defer to the pure `lcdHandleTap()` (or generate its hit-regions from the shared
  layout constants). Delete the duplicated per-callback nav/action logic. Host tests already cover
  `lcdHandleTap`; add a small LVGL smoke test.

**Phase 2 — schema-driven wire codecs (this is R2, widened)**
- Introduce `ui-schema/` + a generator emitting `Proto.h` vectors, `bridge-codec.js`, and `WebJson.h`
  serializers; commit shared golden vectors; wire a Node parity job into CI. Kills problems #1 + #5.
  (Sequence after R2's "make the JS codec testable" decision.)

**Phase 3 — bring the OLED into the model**
- Render the 128×64 (and, if we choose, the 42) from the shared view models — either an LVGL mono scene or
  a thin "smallest bucket" layout that reads the same `*View` structs. Retire the bespoke `formatOledLines`
  path (or keep it only for the 72×40 as a documented exception).

**Phase 4 — unify onboarding**
- Make the portal/QR screen one capability-scaled screen across all panels via a real `IProvisioningDisplay`
  implementation per size bucket, instead of three hand-written variants.

Each phase is small, testable, and leaves the tree shippable. Phases 0–1 are pure cleanup; 2 is the
high-value contract unification; 3–4 fold the stragglers in.

---

## 6. Decisions I need from you

1. **Renderer commitment.** Confirm **LVGL on-device everywhere, canvas = host test-oracle only** (my
   recommendation). The alternative — reviving canvas-on-device and dropping LVGL — loses the Inter fonts /
   anti-aliasing and I don't recommend it.
2. **OLED strategy.** For the tiny **0.42" (72×40)**: fold it into the scene graph (uniform, a bit heavier)
   **or** keep `formatOledLines` as a documented "too small" exception? (The new **0.96" 128×64** I'd fold
   in either way — it has room.)
3. **Schema investment.** Do you want the full **schema → codegen** for the wire/data layer (Phase 2, the
   biggest maintainability win but the most upfront work), or start with just the parity-test version of R2
   and defer generation?
4. **Web scope.** Confirm we are **not** trying to share render code between web and device (only the
   contract/tokens) — i.e. the web SPA stays its own DOM app, fed by the shared schema. (Strongly
   recommended.)
5. **Sequencing vs the nRF work.** This overlaps `architecture-remediation.md` **R2** (codec parity) and is
   adjacent to **R1** (nRF seam extraction). Do you want UI unification interleaved with those, or run as
   its own track after R1 lands?

Once you pick on these, I'll turn the chosen phases into tracked slices (one branch/PR each) the same way we
run the R-items.

---

## 7. Decisions — LOCKED (owner, 2026-07-11)

1. **Renderer:** ✅ **LVGL is the single on-device renderer** for every colour panel.
   **REFINED (2026-07-11, after the LVGL-vs-canvas discussion):** originally we said "keep canvas as the
   host-test oracle" — but since canvas no longer ships on *any* device (only the ⛔ superseded
   `esp32s3-touch` env used it on-device), preserving it as an oracle means **maintaining a second draw
   implementation to test a renderer nobody runs** — the opposite of less code. So instead: **retire the
   canvas *draw* code** (`lcdRender*`), **keep** the shared, ship-relevant pure logic it sat on (the
   `*View` view models, the `lcdlay` layout constants, `lcdHandleTap` routing, the format helpers —
   LVGL consumes these), and **move testing to where bugs live**: plain host unit tests on
   `buildLcdViews` + `lcdHandleTap` + the format helpers, plus (optionally) an **LVGL host-simulator +
   `lv_snapshot`** harness for real pixel regression of the *actual shipped* UI (strictly better than
   canvas screenshots). Rationale: LVGL owns the hard parts (AA fonts, touch/gestures, redraw) and already
   looks good on-device; the only thing canvas did better was host-testability, which the shared-logic
   tests (+ optional LVGL sim) cover without a duplicate renderer. Delete the ⛔ SUPERSEDED `esp32s3-touch`
   env; the on-device canvas path (`LcdDisplay`, `LCD_BANDS`, `lcdRender`) retires with it.
2. **OLED (chosen for the owner):** **fold both OLEDs into the shared view-model + interaction layer** —
   the separate `OledMode` enum + hand-fed scalars go away; the OLED renderer consumes the *same*
   `RideView/MoreView/ProvisionView` structs `buildLcdViews()` fills for the LCDs. The new **0.96" 128×64**
   becomes a full member (LVGL mono theme — it has the room). The tiny **0.42" 72×40** keeps a *thin
   text-row* renderer as the **documented "smallest size bucket"** (a scene graph can't fit 72×40), but it
   reads the shared view models — so **data + interaction are unified across every panel; only the final
   row layout is size-specialised.**
3. **Schema → codegen:** ✅ **full implementation.** A single `ui-schema` source generates the wire/data
   mirrors — `Proto.h` (C++ binary + golden vectors), `bridge-codec.js` (+ Node parity test), and
   `WebJson.h` JSON serializers — plus the capability-flags enum. This *is* R2, widened from "parity test"
   to "generate the mirrors."
4. **Web scope (my recommended default, taken):** the web SPA **stays its own DOM app**; we share only the
   *contract* (schema-generated codecs/objects) + design tokens, **not** render code.
5. **Sequencing (my recommended default, taken):** fold UI-contract unification into the
   `architecture-remediation.md` **R2** track; run the on-device renderer/OLED consolidation adjacent to it.

### ⚠️ Corrected by code investigation (2026-07-11) — the tree is closer to target than first written
- **Token generation is ALREADY DONE.** `design/gen_tokens.py` propagates `design/tokens.json` → the SPA
  `:root` CSS, the ESP32 `WebUi.h` CSS string, **and** the LVGL `LcdCanvas.h` RGB565 constants, with a
  `--check` CI mode (marker-block splice). U0's "generate the theme" is a no-op — this was already built.
  (The earlier draft's `gen_spa_header` name was wrong; the real generator is `gen_tokens.py`.)
- **LVGL is ALREADY the single on-device renderer everywhere current.** `USE_LVGL=1` is set on the
  `esp32s3-pio` family **and** the `esp32cyd` family; `LvglUi` "replaces the LcdCanvas renderer on device."
  The canvas (`LcdUi`/`LcdCanvas`/`LcdDisplay`, `LCD_BANDS`) is used on-device **only** by the
  ⛔ SUPERSEDED `esp32s3-touch` env (`-DUSE_LCD=1`, no LVGL). So "LVGL everywhere" is the shipped reality;
  the decision just needs the *superseded* canvas env retired and the canvas documented as test-oracle.
- **Net:** the *renderer* consolidation (U0/U1) is nearly free; the real remaining value is the **data-model
  unification** — **U2** (schema→codegen for the wire/JSON/caps contract) and **U3** (fold the OLEDs onto
  the shared view models). Prioritise U2/U3.

### Execution order (each a branch → PR → green CI → merge)
- **U0 — retire the superseded canvas env:** ✅ **env family deleted 2026-07-26** (`esp32s3-touch`/`-live`
  /`-live-bench`/`-ota`/`-live-ota`; `platformio.ini` keeps a name-tombstone so stale runbooks fail loudly).
  The live S3 build is the `esp32s3-pio` family. **Still open:** the on-device canvas path (`LcdDisplay`,
  `LCD_BANDS`) is *not* orphaned — `USE_LCD=1` is still set by the `esp32cyd*` and `esp32s3-pio*` envs — so
  it can only be removed once those ship LVGL. Keep `LcdUi.h`/`LcdCanvas.h` as the **host-test oracle** and
  document them as such. (Token-gen already done.)
- **U1 — one interaction model:** LVGL defers to the pure `lcdHandleTap()` (or generates hit-regions from
  the shared layout constants); delete the duplicated per-callback nav/action logic.
- **U2 — schema → codegen (the big one, = R2 widened):** `ui-schema/` + generator → `Proto.h` vectors +
  `bridge-codec.js` + `WebJson.h` + caps enum; committed golden vectors; Node parity job in CI.
- **U3 — OLED into the model:** retire `OledMode`; drive both OLEDs from the shared view models (128×64 via
  LVGL mono; 72×40 via the thin documented text-row bucket).
- **U4 — unify onboarding:** one capability-scaled portal/QR screen via a real `IProvisioningDisplay` per
  size bucket.

Tracked in the task list; U0/U1 are pure cleanup, U2 is the high-value contract unification.
