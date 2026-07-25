// Host LVGL UI test (code/findings/ui-unification.md §U5): compiles the REAL src/ui/LvglUi.cpp on the
// desktop / CI, renders into an in-memory RGB565 framebuffer via the flushArea hook, and drives taps
// through the readTouch hook — so the LVGL head-unit UI (the code that ships on the CYD + S3 ride
// boards) finally has desk-test coverage, the same way LcdCanvas already does. No board, no SDL/X11.
#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

// If $LVGL_DUMP is set, write the current framebuffer to that path as a PPM (a pixel-accurate capture
// of the REAL LvglUi.cpp render — the same code the S3/CYD run). No-op in CI (env unset).
static void dumpFbIfRequested() {
    const char* path = getenv("LVGL_DUMP");
    if (!path) return;
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (uint16_t v : g_fb) {
        uint8_t r = (uint8_t)(((v >> 11) & 0x1F) << 3), g = (uint8_t)(((v >> 5) & 0x3F) << 2),
                b = (uint8_t)((v & 0x1F) << 3);
        r |= r >> 5; g |= g >> 6; b |= b >> 5;
        uint8_t px[3] = {r, g, b};
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

static bool g_inited = false;
void setUp() {
    if (g_inited) return;
    g_fb.assign((size_t)W * H, 0);
    LvglDriverHooks hooks{capFlush, capTouch};
    lvglUiInit(hooks, W, H);   // lv_init + builds the Ride screen; the rest build lazily on first nav
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

// 4) The #10 Compare screen renders A/B agreement content from a CompareView (the LVGL path, not the
//    LcdCanvas takeover) — this is the coverage the standardization requires.
void test_compare_screen_renders() {
    lvglUiShowScreen(LcdScreen::Compare);
    pump(5);
    TEST_ASSERT_EQUAL(int(LcdScreen::Compare), int(lvglUiCurrentScreen()));
    LcdViews v;
    v.compare.valid = true;
    v.compare.aName = "Assioma";
    v.compare.bName = "SB20";
    v.compare.aWatts = 250;
    v.compare.bWatts = 278;
    v.compare.deltaW = 28;
    v.compare.ratio = 1.11f;
    v.compare.biasPct = 11.0f;
    v.compare.nPairs = 80;
    // synthetic per-band shape: a rising bias (a torque/power-dependent error — the case the chart
    // exists to reveal). Whether the real SB20's +11% is flat or ramps is the open question (viz plan).
    for (int i = 0; i < CompareView::NBANDS; ++i) {
        v.compare.bands[i].loW = i * MeterCompare::kTorqueBandNm;
        v.compare.bands[i].nPairs = 4;
        v.compare.bands[i].meanBiasPct = 2.0f + i * 1.4f;
    }
    lvglUiUpdate(v);
    renderFull();
    dumpFbIfRequested();   // $LVGL_DUMP -> pixel-accurate capture of the real LVGL Compare screen
    const uint16_t bg = u16(0x0f1320);
    size_t nonBg = std::count_if(g_fb.begin(), g_fb.end(),
                                 [&](uint16_t p) { return p != 0 && p != bg; });
    TEST_ASSERT_GREATER_THAN(2000, (int)nonBg);
}

// Tap the More row shown at visible position `visRow` (0-based), clearing any queued action first.
// y mirrors buildMore()'s layout (rows at 36 + i*29, height 27) — the centre is 36 + i*29 + 13.
static void tapMoreRow(int visRow) {
    lvglUiShowScreen(LcdScreen::More);
    pump(6);
    UiAction drain;
    while (lvglUiPollAction(drain)) {}
    g_tx = W / 2;
    g_ty = 36 + visRow * 29 + 13;
    pump(4);
    g_tx = -1;   // release -> LVGL fires the CLICKED event on the row
    pump(8);
}

// 5) Every More/Settings row dispatches to the right place: the nav rows (Workout / Calibrate /
//    Compare) load their target screen; the brightness row emits SetBrightness without leaving More;
//    a plain value row does neither. This is the per-row coverage the data-driven More table needs —
//    a future row insert/reorder that breaks the row->target mapping fails HERE, not on the bike.
//    (Touch cal is CYD-only and absent on this build, so rows 0..7 == table indices 0..7.)
void test_more_rows_dispatch() {
    tapMoreRow(0);
    TEST_ASSERT_EQUAL(int(LcdScreen::Workout), int(lvglUiCurrentScreen()));
    tapMoreRow(1);
    TEST_ASSERT_EQUAL(int(LcdScreen::Calibrate), int(lvglUiCurrentScreen()));
    tapMoreRow(2);
    TEST_ASSERT_EQUAL(int(LcdScreen::Compare), int(lvglUiCurrentScreen()));

    // brightness row (index 7): emits SetBrightness, stays on More
    tapMoreRow(7);
    TEST_ASSERT_EQUAL(int(LcdScreen::More), int(lvglUiCurrentScreen()));
    UiAction a;
    TEST_ASSERT_TRUE(lvglUiPollAction(a));
    TEST_ASSERT_EQUAL(int(UiAction::SetBrightness), int(a.type));

    // plain value row (Mode, index 3): navigates nowhere, emits nothing
    tapMoreRow(3);
    TEST_ASSERT_EQUAL(int(LcdScreen::More), int(lvglUiCurrentScreen()));
    UiAction none;
    TEST_ASSERT_FALSE(lvglUiPollAction(none));
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_ride_screen_renders_content);
    RUN_TEST(test_nav_tap_switches_screen);
    RUN_TEST(test_update_changes_render);
    RUN_TEST(test_compare_screen_renders);
    RUN_TEST(test_more_rows_dispatch);
    return UNITY_END();
}

int main() { return runUnityTests(); }
