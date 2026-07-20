// Host tests for the pure MeterCompare core (pio test -e native). Feeds synthetic dual streams and
// asserts the rolling agreement stats + per-band table + pairing window.
#include <unity.h>

#include "MeterCompare.h"
#include "WebJson.h"

using namespace sb20proxy;

void setUp() {}
void tearDown() {}

static void test_agree() {
    MeterCompare mc;
    for (int i = 0; i < 20; ++i) { uint32_t t = i * 1000; mc.onA(200, t); mc.onB(200, t + 10); }
    auto s = mc.stats();
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, s.meanRatio);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, s.meanBiasPct);
    TEST_ASSERT_EQUAL(0, s.deltaW);
}

static void test_b_reads_high() {
    MeterCompare mc;
    for (int i = 0; i < 20; ++i) {
        int a = 100 + i * 10;
        uint32_t t = i * 1000;
        mc.onA(a, t);
        mc.onB((int)(a * 1.1f + 0.5f), t + 10);
    }
    auto s = mc.stats();
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 1.1f, s.meanRatio);   // B ~10% high
    TEST_ASSERT_FLOAT_WITHIN(1.5f, 10.0f, s.meanBiasPct);
}

static void test_pairing_window() {
    MeterCompare mc(700);
    mc.onA(200, 0);
    mc.onB(200, 5000);   // 5 s apart -> not co-temporal
    TEST_ASSERT_EQUAL(0, mc.pairCount());
}

static void test_needs_both_streams() {
    MeterCompare mc;
    for (int i = 0; i < 5; ++i) mc.onA(200, i * 1000);   // only A
    TEST_ASSERT_FALSE(mc.stats().valid);
}

static void test_delta_is_latest_pair() {
    MeterCompare mc;
    mc.onA(200, 0);
    mc.onB(220, 10);
    auto s = mc.stats();
    TEST_ASSERT_EQUAL(200, s.aWatts);
    TEST_ASSERT_EQUAL(220, s.bWatts);
    TEST_ASSERT_EQUAL(20, s.deltaW);
}

static void test_per_band_divergence() {
    MeterCompare mc;
    for (int i = 0; i < 10; ++i) { uint32_t t = i * 1000; mc.onA(260, t); mc.onB(312, t + 10); }  // ~1.2x
    auto bands = mc.bands();
    int idx = 260 / MeterCompare::kBandW;  // band 5 (250-300 W)
    TEST_ASSERT_TRUE(bands[idx].nPairs > 0);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 1.2f, bands[idx].meanRatio);
}

static void test_low_power_guarded() {
    // near-zero A must not blow up the ratio (coasting)
    MeterCompare mc(700, /*minWattsForRatio=*/20);
    for (int i = 0; i < 5; ++i) { uint32_t t = i * 1000; mc.onA(2, t); mc.onB(3, t + 10); }
    auto s = mc.stats();
    TEST_ASSERT_TRUE(s.nPairs > 0);                 // pairs recorded
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, s.meanRatio);  // but ratio stays at the neutral default
}

// The rolling window is a fixed ring: overflowing it must cap the count and DROP THE OLDEST pairs,
// so a long ride's stats reflect recent riding rather than whatever happened at minute one.
static void test_ring_caps_and_evicts_oldest() {
    MeterCompare mc;
    uint32_t t = 0;
    for (size_t i = 0; i < MeterCompare::kMaxPairs + 200; ++i) {   // 200 past the cap
        const bool late = i >= 200;                 // first 200 pairs agree; the rest read +20% high
        mc.onA(200, t);
        mc.onB(late ? 240 : 200, t + 10);
        t += 1000;
    }
    auto s = mc.stats();
    TEST_ASSERT_EQUAL((int)MeterCompare::kMaxPairs, s.nPairs);      // capped, never grows
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 20.0f, s.meanBiasPct);           // the agreeing pairs aged out
    TEST_ASSERT_EQUAL(240, s.bWatts);                               // newest pair is still the latest
}

// The agree threshold is one shared domain rule, not a magic number re-typed per surface.
static void test_agrees_threshold() {
    MeterCompare tight;
    for (int i = 0; i < 10; ++i) { uint32_t t = i * 1000; tight.onA(200, t); tight.onB(202, t + 10); }
    TEST_ASSERT_TRUE(tight.stats().agrees());     // +1% -> agree
    MeterCompare wide;
    for (int i = 0; i < 10; ++i) { uint32_t t = i * 1000; wide.onA(200, t); wide.onB(222, t + 10); }
    TEST_ASSERT_FALSE(wide.stats().agrees());     // +11% -> does not agree
}

static void test_torque_flat_for_constant_scale_error() {
    // B reads 10% high everywhere; feed a range of cadences (=> range of torques). A pure scale error
    // is FLAT in the torque domain, so every populated torque band should read ~+10%.
    MeterCompare mc;
    uint32_t t = 0;
    for (int rep = 0; rep < 4; ++rep)
        for (int cad = 60; cad <= 100; cad += 10)
            for (int w = 100; w <= 300; w += 50) {
                mc.onA(w, t, cad);
                mc.onB((int)(w * 1.10f + 0.5f), t + 10, cad);
                t += 1000;
            }
    int populated = 0;
    for (const auto& b : mc.torqueBands())
        if (b.nPairs > 0) { ++populated; TEST_ASSERT_FLOAT_WITHIN(2.0f, 10.0f, b.meanBiasPct); }
    TEST_ASSERT_TRUE(populated >= 3);
}

static void test_torque_reveals_what_power_hides() {
    // Hold POWER constant (250 W) but vary cadence => vary torque, with B's error growing with torque.
    // The POWER view collapses to ONE band (blind); the TORQUE view shows the ramp.
    MeterCompare mc;
    uint32_t t = 0;
    for (int rep = 0; rep < 6; ++rep)
        for (int cad = 50; cad <= 110; cad += 10) {
            const int a = 250;
            const float torque = a / (cad * 0.10472f);
            const int b = (int)(a * (1.0f + 0.003f * torque) + 0.5f);  // error proportional to torque
            mc.onA(a, t, cad);
            mc.onB(b, t + 10, cad);
            t += 1000;
        }
    const auto tb = mc.torqueBands();
    int loIdx = -1, hiIdx = -1;
    for (int i = 0; i < MeterCompare::kTorqueBands; ++i)
        if (tb[i].nPairs > 0) { if (loIdx < 0) loIdx = i; hiIdx = i; }
    TEST_ASSERT_TRUE(hiIdx > loIdx);
    TEST_ASSERT_TRUE(tb[hiIdx].meanBiasPct > tb[loIdx].meanBiasPct + 1.0f);  // a clear torque ramp
    int powerBandsUsed = 0;                                                   // power view: blind
    for (const auto& b : mc.bands()) if (b.nPairs > 0) ++powerBandsUsed;
    TEST_ASSERT_EQUAL(1, powerBandsUsed);
}

static void test_grid2d_and_cadence_backward_compat() {
    MeterCompare mc;                     // no cadence -> torque/grid skip, power view still works
    for (int i = 0; i < 5; ++i) { mc.onA(200, i * 1000); mc.onB(220, i * 1000 + 10); }
    TEST_ASSERT_TRUE(mc.stats().valid);
    for (const auto& b : mc.torqueBands()) TEST_ASSERT_EQUAL(0, b.nPairs);
    MeterCompare g;                      // with cadence -> the 2-D grid populates
    uint32_t t = 0;
    for (int rep = 0; rep < 4; ++rep)
        for (int cad = 60; cad <= 100; cad += 20)
            for (int w = 100; w <= 300; w += 100) {
                g.onA(w, t, cad);
                g.onB((int)(w * 1.1f + 0.5f), t + 10, cad);
                t += 1000;
            }
    const auto grid = g.grid2d();
    int cells = 0;
    for (int pi = 0; pi < MeterCompare::kGridPBins; ++pi)
        for (int ci = 0; ci < MeterCompare::kGridCBins; ++ci)
            if (grid.cell[pi][ci].nPairs > 0) {
                ++cells;
                TEST_ASSERT_FLOAT_WITHIN(2.0f, 10.0f, grid.cell[pi][ci].meanBiasPct);
            }
    TEST_ASSERT_TRUE(cells >= 3);
}

static void test_compare_json() {
    MeterCompare mc;
    uint32_t t = 0;
    for (int rep = 0; rep < 4; ++rep)
        for (int cad = 60; cad <= 100; cad += 10)
            for (int w = 100; w <= 300; w += 50) {
                mc.onA(w, t, cad);
                mc.onB((int)(w * 1.1f + 0.5f), t + 10, cad);
                t += 1000;
            }
    const std::string j = renderCompareJson(mc, "Assioma", "SB20");
    TEST_ASSERT_TRUE(j.find("\"valid\":true") != std::string::npos);
    TEST_ASSERT_TRUE(j.find("\"aName\":\"Assioma\"") != std::string::npos);
    TEST_ASSERT_TRUE(j.find("\"bName\":\"SB20\"") != std::string::npos);
    TEST_ASSERT_TRUE(j.find("\"biasPct\":1") != std::string::npos);        // ~+10-11%
    TEST_ASSERT_TRUE(j.find("\"tqBias\":[") != std::string::npos);         // torque bands present
    TEST_ASSERT_TRUE(j.find("\"grid\":{") != std::string::npos);           // heatmap grid present
    TEST_ASSERT_TRUE(j.find("\"pairs\":[[") != std::string::npos);         // Bland-Altman pairs present
    TEST_ASSERT_TRUE(j.back() == '}');                                     // well-formed close
}

// --- Parity golden: the SHARED dataset the Python twin (code/tests/test_compare_parity.py) also
// asserts. Same sweep, same expected histograms + values, so MeterCompare.h and sb20proxy.compare
// can't silently drift — the drift that let these cadence/torque/grid views land here while the
// Python twin lagged. A pure +10% scale error (b := a + a/10, exact) over a spread of power +
// cadence: every populated band/cell reads +10.0%, so the asserts pin the BINNING + the ratio/bias
// math independent of the float32-vs-float64 gap between the two implementations.
static void feedGoldenSweep(MeterCompare& mc) {
    // (a_watts, cadence) — keep identical to _SWEEP in test_compare_parity.py.
    static const int sweep[][2] = {
        {100, 90}, {150, 90}, {200, 90}, {250, 90}, {300, 90},
        {100, 60}, {150, 60}, {200, 60}, {250, 60}, {300, 60},
    };
    uint32_t t = 0;
    for (int rep = 0; rep < 4; ++rep)                 // -> 40 pairs
        for (const auto& row : sweep) {
            const int a = row[0], cad = row[1];
            const int b = a + a / 10;                 // a * 1.1, exact (every a is a multiple of 10)
            mc.onA(a, t, cad);
            mc.onB(b, t + 10, cad);
            t += 1000;
        }
}

static void test_parity_golden() {
    TEST_ASSERT_EQUAL_FLOAT(2.0f, kAgreeBandPct);     // the one shared agree threshold

    MeterCompare mc;
    feedGoldenSweep(mc);

    const auto s = mc.stats();
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL(40, s.nPairs);
    TEST_ASSERT_EQUAL(300, s.aWatts);                 // latest pair
    TEST_ASSERT_EQUAL(330, s.bWatts);
    TEST_ASSERT_EQUAL(30, s.deltaW);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 10.0f, s.meanBiasPct);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.10f, s.meanRatio);
    TEST_ASSERT_FALSE(s.agrees());                    // +10% is well past the 2% band

    const int powerBandN[MeterCompare::kBands] = {0, 0, 8, 8, 8, 8, 8, 0, 0, 0, 0, 0};
    const auto pb = mc.bands();
    for (int i = 0; i < MeterCompare::kBands; ++i) {
        TEST_ASSERT_EQUAL(powerBandN[i], pb[i].nPairs);
        if (pb[i].nPairs) {
            TEST_ASSERT_FLOAT_WITHIN(1e-3f, 10.0f, pb[i].meanBiasPct);
            TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.10f, pb[i].meanRatio);
        }
    }

    const int torqueBandN[MeterCompare::kTorqueBands] = {0, 0, 4, 8, 8, 4, 8, 4, 0, 4, 0, 0};
    const auto tb = mc.torqueBands();
    for (int i = 0; i < MeterCompare::kTorqueBands; ++i) {
        TEST_ASSERT_EQUAL(torqueBandN[i], tb[i].nPairs);
        if (tb[i].nPairs) TEST_ASSERT_FLOAT_WITHIN(1e-3f, 10.0f, tb[i].meanBiasPct);
    }

    // cadence 60 -> c-bin 1, cadence 90 -> c-bin 3; power 100..300 W -> p-bins 2..6.
    const int gridN[MeterCompare::kGridPBins][MeterCompare::kGridCBins] = {
        {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0},
        {0, 4, 0, 4, 0, 0}, {0, 4, 0, 4, 0, 0}, {0, 4, 0, 4, 0, 0},
        {0, 4, 0, 4, 0, 0}, {0, 4, 0, 4, 0, 0}, {0, 0, 0, 0, 0, 0},
    };
    const auto g = mc.grid2d();
    for (int pi = 0; pi < MeterCompare::kGridPBins; ++pi)
        for (int ci = 0; ci < MeterCompare::kGridCBins; ++ci) {
            TEST_ASSERT_EQUAL(gridN[pi][ci], g.cell[pi][ci].nPairs);
            if (g.cell[pi][ci].nPairs)
                TEST_ASSERT_FLOAT_WITHIN(1e-3f, 10.0f, g.cell[pi][ci].meanBiasPct);
        }

    // Downsample: step = 40 / 12 = 3 -> 14 samples, oldest-first (identical index math both sides).
    const int expectPairs[][2] = {
        {100, 110}, {250, 275}, {150, 165}, {300, 330}, {200, 220},
        {100, 110}, {250, 275}, {150, 165}, {300, 330}, {200, 220},
        {100, 110}, {250, 275}, {150, 165}, {300, 330},
    };
    const auto sp = mc.samplePairs(12);
    TEST_ASSERT_EQUAL(14, (int)sp.size());
    for (int i = 0; i < 14; ++i) {
        TEST_ASSERT_EQUAL(expectPairs[i][0], sp[i].a);
        TEST_ASSERT_EQUAL(expectPairs[i][1], sp[i].b);
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_parity_golden);
    RUN_TEST(test_compare_json);
    RUN_TEST(test_torque_flat_for_constant_scale_error);
    RUN_TEST(test_torque_reveals_what_power_hides);
    RUN_TEST(test_grid2d_and_cadence_backward_compat);
    RUN_TEST(test_agree);
    RUN_TEST(test_b_reads_high);
    RUN_TEST(test_pairing_window);
    RUN_TEST(test_needs_both_streams);
    RUN_TEST(test_delta_is_latest_pair);
    RUN_TEST(test_per_band_divergence);
    RUN_TEST(test_low_power_guarded);
    RUN_TEST(test_ring_caps_and_evicts_oldest);
    RUN_TEST(test_agrees_threshold);
    return UNITY_END();
}
