// Host LVGL UI test (code/findings/ui-unification.md §U5): compiles the REAL src/ui/LvglUi.cpp on the
// desktop / CI, renders into an in-memory RGB565 framebuffer via the flushArea hook, and drives taps
// through the readTouch hook — so the LVGL head-unit UI (the code that ships on the CYD + S3 ride
// boards) finally has desk-test coverage, the same way LcdCanvas already does. No board, no SDL/X11.
#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include <Arduino.h>   // the host shim (test/lvgl_shim) — millis()/Serial/heap_caps/psramFound
#include <lvgl.h>

#include "LvglUi.h"

using namespace sb20proxy;

static constexpr int W = LCD_W;   // 240 (from -DLCD_PANEL_W)
static constexpr int H = LCD_H;   // 320 (from -DLCD_PANEL_H)

static std::vector<uint16_t> g_fb;   // captured full-frame RGB565
static int g_tx = -1, g_ty = -1;     // injected touch (>=0 == pressed)

// The display flush hook: composite the flushed area into the full-frame buffer.
static void capFlush(int x1, int y1, int x2, int y2, const uint16_t* px) {
    const int w = x2 - x1 + 1;
    for (int y = y1; y <= y2; ++y)
        for (int x = x1; x <= x2; ++x)
            if (x >= 0 && x < W && y >= 0 && y < H)
                g_fb[(size_t)y * W + x] = px[(size_t)(y - y1) * w + (x - x1)];
}
// The touch read hook: report the injected point while "pressed".
static bool capTouch(int& x, int& y) {
    if (g_tx < 0) return false;
    x = g_tx;
    y = g_ty;
    return true;
}

// Pump LVGL n cycles, advancing the fake clock so refresh/timers/input run.
static void pump(int n) {
    for (int i = 0; i < n; ++i) {
        lvglShimAdvanceMs(20);
        lvglUiTick();
    }
}
// Force a full repaint into g_fb (partial-render mode otherwise only flushes dirty areas).
static void renderFull() {
    lv_obj_invalidate(lv_screen_active());
    std::fill(g_fb.begin(), g_fb.end(), 0);
    pump(30);
}
static uint16_t u16(uint32_t rgb) { return lv_color_to_u16(lv_color_hex(rgb)); }

static bool g_inited = false;
void setUp() {
    if (g_inited) return;
    g_fb.assign((size_t)W * H, 0);
    LvglDriverHooks hooks{capFlush, capTouch};
    lvglUiInit(hooks, W, H);   // lv_init + builds all screens; single init for the whole suite
    g_inited = true;
    pump(5);
}
void tearDown() {}

// 1) The Ride screen renders real content (cards + text), not a blank/one-colour screen.
void test_ride_screen_renders_content() {
    lvglUiShowScreen(LcdScreen::Ride);
    LcdViews v;
    v.ride.srcName = "ASSIOMA";
    v.ride.srcOn = true;
    v.ride.outName = "Stages 62144";
    v.ride.watts = 217;
    v.ride.cadence = 90;
    lvglUiUpdate(v);
    renderFull();
    const uint16_t bg = u16(0x0f1320);
    size_t nonBg = std::count_if(g_fb.begin(), g_fb.end(),
                                 [&](uint16_t p) { return p != 0 && p != bg; });
    TEST_ASSERT_GREATER_THAN(2000, (int)nonBg);
}

// 2) A tap on the bottom nav bar routes touch -> LVGL widget event -> navTo (screen change).
void test_nav_tap_switches_screen() {
    lvglUiShowScreen(LcdScreen::Ride);
    pump(5);
    TEST_ASSERT_EQUAL(int(LcdScreen::Ride), int(lvglUiCurrentScreen()));
    // Middle nav tab = Setup (the nav bar is the bottom ~30 px, three equal thirds).
    g_tx = W / 2;
    g_ty = H - 12;
    pump(4);
    g_tx = -1;   // release -> LVGL fires the click on the button
    pump(8);
    TEST_ASSERT_EQUAL(int(LcdScreen::Setup), int(lvglUiCurrentScreen()));
}

// 3) The view-model actually reaches pixels: two different power values render differently.
void test_update_changes_render() {
    lvglUiShowScreen(LcdScreen::Ride);
    LcdViews a;
    a.ride.watts = 100;
    a.ride.cadence = 80;
    lvglUiUpdate(a);
    renderFull();
    std::vector<uint16_t> first = g_fb;
    LcdViews b;
    b.ride.watts = 999;
    b.ride.cadence = 80;
    lvglUiUpdate(b);
    renderFull();
    TEST_ASSERT_FALSE(std::equal(first.begin(), first.end(), g_fb.begin()));
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_ride_screen_renders_content);
    RUN_TEST(test_nav_tap_switches_screen);
    RUN_TEST(test_update_changes_render);
    return UNITY_END();
}

int main() { return runUnityTests(); }
