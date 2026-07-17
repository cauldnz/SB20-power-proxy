// Host tests for the CompareService seam (pio test -e native). The interface IS the test surface:
// inject fake Sources, tick(), then assert fillView()/json(). No Arduino, no BLE, no LVGL.
#include <unity.h>

#include <string>

#include "CompareService.h"

using namespace sb20proxy;

void setUp() {}
void tearDown() {}

static PowerReading rd(int w, int cad = 90) {
    PowerReading r;
    r.power_w = (int16_t)w;
    r.cadence_rpm = (int16_t)cad;
    return r;
}

// 1) Injected sources are paired and reach the view.
static void test_injected_sources_reach_the_view() {
    CompareService svc([](uint32_t) { return rd(200); }, [](uint32_t) { return rd(220); });
    for (uint32_t t = 1000; t <= 20000; t += 1000) svc.tick(t);
    CompareView v;
    svc.fillView(v);
    TEST_ASSERT_TRUE(v.valid);
    TEST_ASSERT_FLOAT_WITHIN(1.5f, 10.0f, v.biasPct);   // B reads 10% high
    TEST_ASSERT_TRUE(v.nPairs > 5);
}

// 2) THE SEAM: the B-source is injected, so swapping the adapter changes the outcome with no branch
//    inside the service — the simulated adapter and a "real second meter" adapter both just fit.
static void test_b_source_is_an_injected_seam() {
    CompareService::Source a = [](uint32_t) { return rd(200); };
    int ratio = 1200;
    CompareService sim(a, scaledSource(a, &ratio));           // bench adapter: B = A x 1.2
    for (uint32_t t = 1000; t <= 20000; t += 1000) sim.tick(t);
    CompareView v1;
    sim.fillView(v1);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 20.0f, v1.biasPct);

    CompareService real(a, [](uint32_t) { return rd(200); }); // "real meter" adapter: independent
    for (uint32_t t = 1000; t <= 20000; t += 1000) real.tick(t);
    CompareView v2;
    real.fillView(v2);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, v2.biasPct);         // they agree
}

// 3) tick() self-throttles — hammering it must not flood the rolling window.
static void test_tick_self_throttles() {
    CompareService::Source a = [](uint32_t) { return rd(200); };
    int ratio = 1100;
    CompareService svc(a, scaledSource(a, &ratio), "A", "B", 500);
    for (int i = 0; i < 50; ++i) svc.tick(1000);   // same clock, 50 calls
    CompareView v;
    svc.fillView(v);
    TEST_ASSERT_TRUE(v.nPairs <= 1);
}

// 4) A quiet/disconnected meter simply stops contributing — callers don't special-case it.
static void test_quiet_meter_contributes_nothing() {
    CompareService svc([](uint32_t) { return rd(0); }, [](uint32_t) { return rd(200); });
    for (uint32_t t = 1000; t <= 10000; t += 1000) svc.tick(t);
    CompareView v;
    svc.fillView(v);
    TEST_ASSERT_FALSE(v.valid);
}

// 5) The Source contract (idempotent within a tick): a derived scaledSource re-reads its upstream in
//    the same tick, so a MOVING bench ramp must still yield exactly the configured ratio — not a
//    smeared one from the ramp advancing twice.
static void test_scaled_b_is_exact_against_a_moving_ramp() {
    CompareService::Source ramp = rampSource(500);
    int ratio = 1110;
    CompareService svc(ramp, scaledSource(ramp, &ratio), "A", "B", 500);
    for (uint32_t t = 1000; t <= 40000; t += 500) svc.tick(t);
    CompareView v;
    svc.fillView(v);
    TEST_ASSERT_TRUE(v.valid);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 11.0f, v.biasPct);   // exactly the ratio, despite A moving
}

// 6) The /compare payload comes out of the same seam.
static void test_json_through_the_service() {
    CompareService::Source a = [](uint32_t) { return rd(250, 80); };
    int ratio = 1110;
    CompareService svc(a, scaledSource(a, &ratio), "Assioma", "SB20");
    for (uint32_t t = 1000; t <= 20000; t += 1000) svc.tick(t);
    const std::string j = svc.json();
    TEST_ASSERT_TRUE(j.find("\"aName\":\"Assioma\"") != std::string::npos);
    TEST_ASSERT_TRUE(j.find("\"bName\":\"SB20\"") != std::string::npos);
    TEST_ASSERT_TRUE(j.find("\"tqBias\":[") != std::string::npos);
    TEST_ASSERT_TRUE(j.find("\"grid\":{") != std::string::npos);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_injected_sources_reach_the_view);
    RUN_TEST(test_b_source_is_an_injected_seam);
    RUN_TEST(test_tick_self_throttles);
    RUN_TEST(test_quiet_meter_contributes_nothing);
    RUN_TEST(test_scaled_b_is_exact_against_a_moving_ramp);
    RUN_TEST(test_json_through_the_service);
    return UNITY_END();
}
