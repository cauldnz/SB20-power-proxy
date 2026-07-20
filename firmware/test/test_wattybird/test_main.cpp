// Host unit tests for the pure WattyBird game core (+ a compile/render smoke of the renderer).
// Runs under `pio test -e native` — no board, no Arduino. Physics is asserted directionally
// (power climbs, gravity sinks, hover roughly holds) plus scoring, collision, and determinism.
#ifndef LCD_PANEL_W
#define LCD_PANEL_W 240
#endif
#ifndef LCD_PANEL_H
#define LCD_PANEL_H 320
#endif

#include <unity.h>

#include "WattyBird.h"
#include "WattyBirdRender.h"

using namespace sb20proxy;

void setUp() {}
void tearDown() {}

// Advance `ms` of sim in fixed dt ticks at a constant power.
static void run(WattyBird& g, int watts, int ms, int dt = 20) {
    for (int t = 0; t < ms; t += dt) g.step(dt, watts);
}

static void test_ready_until_pedalling() {
    WattyBird g;
    TEST_ASSERT_EQUAL(int(WbMode::Ready), int(g.mode()));
    g.step(20, 0);  // not pedalling
    TEST_ASSERT_EQUAL(int(WbMode::Ready), int(g.mode()));
    g.step(20, 120);  // above startWatts -> fly
    TEST_ASSERT_EQUAL(int(WbMode::Playing), int(g.mode()));
}

static void test_gravity_sinks_to_death() {
    WattyBird g;
    g.start(1);
    float y0 = g.birdY();
    g.step(20, 0);
    TEST_ASSERT_TRUE(g.birdY() > y0);  // no power -> falls
    run(g, 0, 4000);
    TEST_ASSERT_EQUAL(int(WbMode::Dead), int(g.mode()));  // hits the ground
}

static void test_power_climbs() {
    WattyBird g;
    g.start(1);
    float y0 = g.birdY();
    run(g, 320, 300);  // well above hover
    TEST_ASSERT_TRUE(g.birdY() < y0);  // climbs (smaller y = higher)
}

static void test_hover_roughly_holds() {
    WattyBird g;
    g.start(1);
    float y0 = g.birdY();
    run(g, g.hoverWatts(), 2000);
    // at hover, lift ~cancels gravity -> stays within a small band (not sink to ground / shoot up)
    TEST_ASSERT_TRUE(g.mode() == WbMode::Playing);
    TEST_ASSERT_FLOAT_WITHIN(45.0f, y0, g.birdY());
}

static void test_scoring_counts_passed_pipes() {
    // Huge gap => no pipe collision; hover keeps the bird alive while pipes scroll past.
    WattyBirdConfig cf;
    cf.gapH = cf.worldH * 2;  // gap taller than the world -> impossible to hit a pipe body
    WattyBird g(cf);
    g.start(7);
    TEST_ASSERT_EQUAL(0, g.score());
    run(g, g.hoverWatts(), 8000);
    TEST_ASSERT_TRUE(g.mode() == WbMode::Playing);   // never crashed
    TEST_ASSERT_TRUE(g.score() > 0);                 // pipes scrolled past and scored
    TEST_ASSERT_TRUE(g.best() >= g.score());
}

static void test_pipe_collision_kills() {
    // One tight gap parked away from the bird's line -> holding hover flies into the pipe body.
    WattyBirdConfig cf;
    cf.gapH = 40;
    cf.gapMargin = 10;
    WattyBird g(cf);
    g.start(3);
    run(g, g.hoverWatts(), 6000);
    TEST_ASSERT_EQUAL(int(WbMode::Dead), int(g.mode()));
}

static void test_deterministic_course_for_seed() {
    WattyBird a, b;
    a.start(0xABCD);
    b.start(0xABCD);
    for (int i = 0; i < 200; ++i) {
        a.step(20, 150);
        b.step(20, 150);
    }
    TEST_ASSERT_EQUAL(a.score(), b.score());
    TEST_ASSERT_EQUAL((int)a.pipes().size(), (int)b.pipes().size());
    if (!a.pipes().empty())
        TEST_ASSERT_EQUAL(a.pipes().front().gapCenterY, b.pipes().front().gapCenterY);
}

static void test_renderer_smoke() {
    // The renderer must run without touching out-of-bounds pixels in every mode.
    LcdCanvas c;
    WattyBird g;
    renderWattyBird(c, g);                 // Ready overlay
    g.start(5);
    run(g, 200, 1000);
    renderWattyBird(c, g);                 // Playing
    TEST_ASSERT_EQUAL((size_t)LCD_W * LCD_H, c.px.size());
    // something got drawn (not a flat clear): expect some pipe-green + bird-yellow present
    bool sawBird = false;
    for (uint16_t p : c.px) if (p == WB_BIRD) { sawBird = true; break; }
    TEST_ASSERT_TRUE(sawBird);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_ready_until_pedalling);
    RUN_TEST(test_gravity_sinks_to_death);
    RUN_TEST(test_power_climbs);
    RUN_TEST(test_hover_roughly_holds);
    RUN_TEST(test_scoring_counts_passed_pipes);
    RUN_TEST(test_pipe_collision_kills);
    RUN_TEST(test_deterministic_course_for_seed);
    RUN_TEST(test_renderer_smoke);
    return UNITY_END();
}
