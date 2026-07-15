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

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_render_smoke);
    RUN_TEST(test_agree);
    RUN_TEST(test_b_reads_high);
    RUN_TEST(test_pairing_window);
    RUN_TEST(test_needs_both_streams);
    RUN_TEST(test_delta_is_latest_pair);
    RUN_TEST(test_per_band_divergence);
    RUN_TEST(test_low_power_guarded);
    return UNITY_END();
}
