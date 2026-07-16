// Host tests for the pure MeterCompare core (pio test -e native). Feeds synthetic dual streams and
// asserts the rolling agreement stats + per-band table + pairing window.
#ifndef LCD_PANEL_W
#define LCD_PANEL_W 240
#endif
#ifndef LCD_PANEL_H
#define LCD_PANEL_H 320
#endif

#include <unity.h>

#include "MeterCompare.h"
#include "MeterCompareRender.h"

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

static void test_render_smoke() {
    // The Compare screen must render in-bounds for both the empty and populated cases.
    MeterCompare mc;
    LcdCanvas c;
    renderMeterCompare(c, "Assioma", "SB20", mc.stats(), mc.bands());  // invalid/empty path
    for (int i = 0; i < 20; ++i) { uint32_t t = i * 1000; mc.onA(200, t); mc.onB(222, t + 10); }
    renderMeterCompare(c, "Assioma", "SB20", mc.stats(), mc.bands());  // populated (+11%)
    TEST_ASSERT_EQUAL((size_t)LCD_W * LCD_H, c.px.size());
    bool sawRed = false;                       // the "B reads HIGH" red should appear somewhere
    for (uint16_t p : c.px) if (p == MCMP_HI) { sawRed = true; break; }
    TEST_ASSERT_TRUE(sawRed);
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

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_render_smoke);
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
    return UNITY_END();
}
