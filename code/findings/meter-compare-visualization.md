# Visualizing A/B power-meter error — with the torque domain front and centre

**Status: PLANNED (2026-07-16).** A research + design plan for *how* to visualize the live/offline
agreement between two power meters (A vs B), so we can see **where in the pedalling space** the error
lives — not just "B reads +11%". Governs the compare surfaces: the pure core
[`firmware/lib/proxy/MeterCompare.h`](../../firmware/lib/proxy/MeterCompare.h) + its lifecycle seam
[`CompareService.h`](../../firmware/lib/proxy/CompareService.h), the head-unit's LVGL Compare screen
(`buildCompare()` in [`firmware/src/ui/LvglUi.cpp`](../../firmware/src/ui/LvglUi.cpp)), the web app's
deep-dive ([`web/index.html`](../../web/index.html), fed by `GET /compare`), and the desk twin
[`code/scripts/compare_meters.py`](../scripts/compare_meters.py). Companion to
[`sb20-power-topology.md`](sb20-power-topology.md) (the ~11% SB20/Stages-vs-Assioma finding) and
[`domain-primer.md`](domain-primer.md) §2 (torque/cadence). **No code is changed by this doc** — it's the
plan the next slice implements.

> **Surfaces correction (2026-07-16).** This doc originally said "desk tool" for the rich, interactive
> visualizations. The correct home is the **web app** ([`web/index.html`](../../web/README.md)) — the
> standardized companion UI (shares the view-model, `design/tokens.json`, and the Bridge wire contract;
> connects over Web Bluetooth or the ESP's HTTP). So read every "Python desk tool" / "surface D" below as
> **the web app** (HTML `<canvas>`/SVG plots — interactive, full-colour, user-facing, no matplotlib).
> `code/scripts/compare_meters.py` stays a **dev/debug + replay CLI only**, not a visualization surface.
> The three surfaces are therefore: **head-unit (LVGL)** = the glanceable bias-by-band line (live, on the
> bike); **web app** = the deep dive (Bland–Altman + 2-D power×cadence heatmap + scatter/regression);
> **`compare_meters.py`** = a host-test/replay helper. Data flow: the device computes `MeterCompare` and
> exposes the binned tables (+ a coarse power×cadence grid) over the Bridge contract → the web renders.

---

## 1. Why this doc exists — the power-only bin hides the interesting error

Both compare surfaces today bin the bias% by **power band only** (12 bands of 50 W, `MeterCompare::bands()`).
That answers "do the meters diverge at high *power*?" but it **cannot** answer "do they diverge at high
*torque*?" — and torque is where strain-gauge error actually lives.

The physics (`domain-primer.md` §2):

```
P (W)  =  τ (N·m) × ω (rad/s)  =  τ × cadence(rpm) × 2π/60
                                =  τ × cadence(rpm) × 0.10472

⇒  τ (N·m)  ≈  P / (cadence(rpm) × 0.1047)
```

So a single **power** band mixes wildly different **torque** levels:

| Effort | Power | Cadence | Torque |
|---|---|---|---|
| Spinning easy gear | 250 W | 100 rpm | **23.9 N·m** |
| Same power, grinding | 250 W | 60 rpm | **39.8 N·m** |
| Standing sprint | 250 W | 50 rpm | **47.7 N·m** |

All three land in the *same* 250 W power bin, yet the strain gauge sees a nearly **2×** spread in torque.
If B's error is torque-dependent (slope/offset drift, gauge nonlinearity, temperature — see §3), the
power-band chart **averages it away**: the grinding samples and the spinning samples cancel inside one bar.
This is exactly the failure mode DCRainmaker-style reviews warn about — "since power = torque × cadence, if
two meters disagree at a given cadence they're measuring different torque values; calibration errors can be
torque-dependent rather than cadence-dependent."
([DC Rainmaker](https://www.dcrainmaker.com/2023/02/shimano-r9200p-astonishingly.html))

**We already have a hint this is real for our pair.** `domain-primer.md` §2 records the Stages-vs-Assioma
discrepancy as **torque/cadence-shaped** — "~13% high at 60 rpm vs ~5% at 100 rpm at fixed power … exactly
the signature of a `P = τ·ω` slope error, not a flat scale." But the headline finding in
[`sb20-power-topology.md`](sb20-power-topology.md) is a **single aggregate ≈1.11×** (session 7). We do **not
yet know** whether that 11% is a flat scale or the average of a torque ramp. **That is the whole point of
this visualization: to find out.** If it's flat, a scalar correction is honest; if it ramps with torque, the
firmware `Correction` needs a torque- (or cadence-) aware term, and the single-number verdict on the
head-unit is misleading.

---

## 2. The metrology standard: Bland–Altman

When you compare two *methods of measuring the same quantity*, the reference technique is the **Bland–Altman
(difference) plot**: for each paired sample plot the **mean** of the two readings on X against their
**difference** (or **ratio**) on Y, then draw the **bias line** (mean difference) and the **limits of
agreement** (bias ± 1.96·SD of the differences — the band containing ~95% of disagreements).
([MedCalc](https://www.medcalc.org/en/manual/bland-altman-plot.php),
[Bland–Altman overview, IJAM](https://journals.lww.com/ijam/fulltext/2017/03010/bland_altman_plot__a_brief_overview.16.aspx))

Why it's the right tool here rather than "correlation" or a single ratio:

- **It separates bias from spread.** A flat bias line at +11% with tight limits = a clean scale error we can
  correct with a scalar. A **sloped** bias cloud = **proportional error** (the disagreement grows with
  magnitude). A **funnel** (widening limits) = **heteroscedasticity** — noisier at one end.
  ([MedCalc](https://www.medcalc.org/en/manual/bland-altman-plot.php))
- Regression-based Bland–Altman variants explicitly **model the bias and limits as a function of magnitude**,
  which is what you do when the error is proportional/heteroscedastic — a constant LoA band would be
  misleading. ([MedCalc](https://www.medcalc.org/en/manual/bland-altman-plot.php))
- **Correlation is the wrong test** — two meters can be almost perfectly correlated yet disagree by a
  constant scale; Bland–Altman shows the *agreement*, not the *association*.

**The torque twist we add:** vanilla Bland–Altman puts *mean power* on X. For our question we also want the
**difference plotted against torque** (and against cadence) on X — a residual-vs-torque view. That's the
minimal change that turns a generic method-comparison plot into a torque-domain diagnostic.

**How power-meter reviewers show it in practice:** DCRainmaker-style analyses overlay the raw A/B/C power
traces on one time axis (eyeball where one meter misses surges/sprints), plus scatter A-vs-B with a
regression line, and call out **cadence-dependence** and **sprint (low-cadence, high-torque)** disagreement
specifically — sprints above ~500 W are where meters most often diverge because cadence/torque sampling gets
unreliable. ([DC Rainmaker — Stages review](https://www.dcrainmaker.com/2013/01/stages-power-meter-in-depth-review.html),
[Shimano R9200P review](https://www.dcrainmaker.com/2023/02/shimano-r9200p-astonishingly.html))

---

## 3. Why torque (not power, not cadence alone) is the physically-motivated axis

Strain-gauge power meters sense **torque** directly and multiply by measured angular velocity. Their error
mechanisms are **torque-domain** phenomena:

- **Slope error.** Every meter has a unique slope (N·m per unit strain); if the slope drifts, the reading is
  off by a percentage that is **constant in torque** → shows as a tilt vs torque, and (because P = τ·ω) as a
  cadence-dependent error at fixed power. Slope linearity is what keeps a meter honest "at lower power as
  well as during high-power sprint efforts."
  ([SRM](https://www.srm.de/powermeters-the-srm-difference/), [Power Meter City](https://powermetercity.com/2016/03/10/power-meter-calibration-vs-zero-offset/))
- **Zero-offset / bias drift.** A wrong zero adds a **fixed torque offset** → a *bias that shrinks as a
  percentage with rising torque* (big % error at low torque, small at high). This looks completely different
  from a slope error on a residual-vs-torque plot — which is exactly why we want that plot.
- **Temperature.** Gauge output drifts with temperature ("residual torque" from ambient temp/pressure),
  handled by a thermistor-driven offset curve — another torque-offset-like term that a torque-domain view
  surfaces. ([Power Meter City — how to zero](https://powermetercity.com/2016/08/22/how-to-zero-your-power-meter/))
- **Non-linearity.** Real gauges aren't perfectly linear across their range; the deviation is a function of
  **applied torque**, invisible in a power average that mixes torques.

Independent validation work confirms power-meter error is **regime-dependent** (largest under
**high-resistance / low-inertia / low-cadence = high-torque** conditions, small under low-force/high-cadence),
with computed errors exceeding 10% even for meters claiming ±1%.
([arXiv 2409.18414 — power-balance accuracy](https://arxiv.org/pdf/2409.18414))

**Conclusion:** torque is the axis on which the error is *simplest to describe* (slope = constant %, offset =
constant N·m). Binning by torque doesn't just reveal the error — it reveals its **shape**, which tells us
which correction term to add.

---

## 4. Candidate visualizations

Suitability rated for **(H)** the small LVGL head-unit (172×320 or 240×320, ~a few dozen usable colour
cells, no mouse/hover, glanceable) vs **(D)** the Python desk tool (can render PNG/matplotlib, full colour,
interactive/offline).

| # | Visualization | What it reveals | Pros | Cons | H | D |
|---|---|---|---|---|:-:|:-:|
| 1 | **Bland–Altman** (mean power X, ratio/diff Y, bias + LoA) | Bias vs proportional vs heteroscedastic error at a glance; the metrology gold standard | Standard, interpretable, shows spread not just centre | Needs a scatter + regression; too dense for a tiny panel; X is *power*, so torque still hidden unless re-axised | ✗ | ✅✅ |
| 2 | **Scatter A-vs-B + regression** (+ y=x line) | Overall scale (slope) & offset (intercept); outliers/dropouts | Familiar to cyclists; regression slope ≈ the correction scale | Doesn't isolate *where* error lives; overplotting | ✗ | ✅ |
| 3 | **Per-power-band bar** (bias% vs power band) — *today's chart* | Divergence vs power | Cheap; already built; fits the panel | **Hides torque error** — the core problem (§1) | ✅ (have) | ✅ (have) |
| 4 | **Per-torque-band bar** (bias% vs torque band) | Divergence vs **torque** — separates slope (tilt) from offset (low-torque spike) | Same cheap column widget as #3, torque-aware; glanceable | Needs cadence per sample (§5); torque bins need sensible edges | ✅✅ | ✅ |
| 5 | **Per-cadence-band bar** (bias% vs cadence band) | Cadence-dependence directly (the DCRainmaker cut) | Cheap; complements #4 (P=τ·ω, so cadence at fixed power = inverse torque) | One more table; less physically fundamental than torque | ✅ | ✅ |
| 6 | **2-D power×cadence heatmap** (grid cell coloured by bias%) | The *full map* of where error lives; iso-torque runs diagonally across the grid | One glance shows the whole pedalling space; iso-power and iso-torque both readable | Needs enough colour cells + a legend; sparse cells early in a ride; hard to label on 172 px | ~ (coarse) | ✅✅ |
| 7 | **Residual-vs-torque** (scatter/binned line: bias% or ΔW vs derived torque) | The cleanest test of the §3 hypothesis: slope error = tilt, offset error = low-torque hyperbola | Directly answers "is the 11% torque-dependent?" | Scatter too dense for the panel; a *binned line* is the panel-friendly reduction (≈ #4) | ✗ (use #4) | ✅✅ |

Notes:
- On the head-unit, #4 is a **drop-in re-axis of the existing bias-by-band chart** (the LVGL Compare screen
  already draws one) — lowest-cost torque awareness. *(Shipped: the chart is torque-axed, and its band count
  derives from `MeterCompare::kTorqueBands` so it can't diverge from `GET /compare`.)*
- A **coarse** version of #6 fits even 172×320: e.g. a 4-power × 4-cadence grid of colour blocks (16 cells,
  each ≥ ~30 px) with a tiny red/green diverging legend. Iso-torque lines run corner-to-corner, so the eye
  reads the torque gradient for free.

---

## 5. Data-model change — MeterCompare must capture **cadence**

The blocker: `MeterCompare` today stores only watts per sample, so it **cannot derive torque**.

```cpp
// TODAY (MeterCompare.h):
void onA(int watts, uint32_t t_ms);
void onB(int watts, uint32_t t_ms);
struct Pair { int a; int b; };          // watts only
```

CPS already carries cadence on the wire (crank-rev + last-event-time, `domain-primer.md` §3), and the desk
`compare_meters.py` already parses CPS — so the sample source *has* cadence; the compare core just discards
it. Minimal, backward-compatible API sketch:

```cpp
// PROPOSED — cadence optional (default -1 = unknown ⇒ behaves exactly as today):
void onA(int watts, int cadenceRpm, uint32_t t_ms);   // overload/def-arg keeps callers compiling
void onB(int watts, int cadenceRpm, uint32_t t_ms);

struct Pair { int a; int b; int cadA; int cadB; };    // keep both cadences

// derive torque from A (the reference meter) so the bin axis is stable:
static float torqueNm(int watts, int cadRpm) {        // 0 if cadence unknown/near-zero
    return (cadRpm > 5) ? (float)watts / ((float)cadRpm * 0.10472f) : 0.0f;
}

// NEW tables alongside the existing per-power bands():
static constexpr int kTorqueBandNm = 5;   // 0..~60 N·m in 5 N·m bins → 12 bands (mirror kBands)
std::vector<MeterBand> torqueBands() const;   // bin each pair by torqueNm(a, cadA)
// (a cadenceBands() companion was prototyped and dropped — no surface consumed it; the 2-D grid
//  below already answers "does the error move with cadence?" without a third 1-D slice)

// (optional, desk-first) a coarse 2-D grid for the heatmap:
struct MeterCell { int nPairs; float meanBiasPct; };
std::vector<MeterCell> grid(int nPowerBins, int nCadBins) const;   // #6
```

Design points:
- **Reference-meter torque.** Derive the bin from **A's** watts+cadence (A = the reference/Assioma) so a
  pair always lands in one bin regardless of B's error — the same convention the ratio math uses (`b/a`).
- **Reuse `MeterBand`.** `torqueBands()` returns the *same* struct as `bands()`, so the Compare screen's
  existing bias-by-band chart renders either with a label/axis swap — no new render primitive.
- **Backward-compatible & host-testable.** Cadence defaults to unknown; the existing
  `test_metercompare` golden tests keep passing, and new tests feed synthetic (watts, cadence) streams
  (e.g. a *flat 11%* stream vs a *torque-ramped* stream) to prove `torqueBands()` distinguishes them — the
  real-data-first discipline, provable with no hardware.
- **Guard low cadence.** Torque explodes as cadence→0 (coasting/track-stand); reuse the existing
  `minWattsForRatio` spirit with a `minCadenceForTorque` (~5–10 rpm) so garbage samples don't smear the
  high-torque bins.

Both twins move together (parity is a project invariant): mirror the same `torqueBands()` in
`compare_meters.py`.

---

## 6. Recommendation

**Head-unit (glanceable, torque-aware, cheap):**
1. **Add a per-torque-band bar chart (#4)** as the primary divergence chart — re-axis the Compare screen's
   existing chart from power bins to **5 N·m torque bins**. Same widget, torque-aware.
2. Keep the single-number verdict (`B reads +11% HIGH`) but **flag when the bias is *not* flat across torque**
   — e.g. append `(varies with torque)` / a small "▲ slope" glyph when the spread across torque bands exceeds
   a threshold. That turns the headline number honest.
3. *(Stretch, fits even 172 px)* a **coarse 4×4 power×cadence colour grid (#6)** on a second compare page —
   16 diverging-coloured cells + a 3-swatch legend; iso-torque reads diagonally.
   Keep power-band (#3) available as a toggle for continuity, but torque is the default.

**Desk tool (`compare_meters.py`, full fidelity — this is where we *decide* the correction model):**
1. **Bland–Altman (#1)** with **ratio** on Y (bias line + 1.96·SD limits) — the metrology verdict on
   flat-vs-proportional-vs-heteroscedastic.
2. **Residual-vs-torque (#7)** — bias% (and ΔW) against derived torque, binned line + scatter. **This is the
   plot that answers the open question**: is the session-7 ~11% a flat scale or a torque ramp? Slope error →
   tilt; zero-offset/temperature → low-torque hyperbola.
3. **2-D power×cadence heatmap (#6)** at full resolution (PNG via matplotlib) — the complete map, with
   iso-torque contours, so we see the pedalling regions we actually ride vs. where the error is worst.
4. Keep scatter+regression (#2) and the per-band bars as supporting panels.

Rationale: the head-unit's job is a **live, honest, glanceable "do they agree, and is it simple?"**; the desk
tool's job is to **characterise the error's shape** well enough to choose the `Correction` model
(scalar vs torque/cadence-aware). Both are unlocked by the one data-model change in §5.

---

## 7. Grounding & the open question (don't overclaim)

- **Real anchor:** the SB20/Stages reads **≈1.11× the Assioma** (session 7, verbatim 1:1 SB20↔Stages;
  [`sb20-power-topology.md`](sb20-power-topology.md)). Earlier paired data hinted the Stages↔Assioma error is
  **torque/cadence-shaped** (~13% @60 rpm vs ~5% @100 rpm; `domain-primer.md` §2) — the fingerprint of a
  `P=τ·ω` slope error.
- **What we do NOT yet know:** whether that headline **11% is flat or torque-dependent** across a real ride.
  The two data points above are in tension (one aggregate scale, one cadence ramp) and were captured
  differently. **This is not settled** — resolving it is the *reason* to build the torque-domain views, not
  an assumption to bake in. When the real paired capture is binned by torque, the plot decides it; log the
  result in [`decisions.md`](decisions.md).

## Sources

- Bland–Altman — [MedCalc manual](https://www.medcalc.org/en/manual/bland-altman-plot.php);
  [IJAM overview](https://journals.lww.com/ijam/fulltext/2017/03010/bland_altman_plot__a_brief_overview.16.aspx)
- Power-meter reviewer methodology — [DC Rainmaker: Stages review](https://www.dcrainmaker.com/2013/01/stages-power-meter-in-depth-review.html);
  [Shimano R9200P review (cadence/torque dependence)](https://www.dcrainmaker.com/2023/02/shimano-r9200p-astonishingly.html)
- Strain-gauge slope/offset/temperature error — [SRM PowerMeters](https://www.srm.de/powermeters-the-srm-difference/);
  [Power Meter City: calibration vs zero-offset](https://powermetercity.com/2016/03/10/power-meter-calibration-vs-zero-offset/);
  [Power Meter City: how to zero](https://powermetercity.com/2016/08/22/how-to-zero-your-power-meter/)
- Regime-dependent (high-torque) meter error — [arXiv 2409.18414](https://arxiv.org/pdf/2409.18414)
