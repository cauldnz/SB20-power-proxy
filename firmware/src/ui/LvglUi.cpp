// LvglUi.cpp — LVGL v9 head-unit UI. See LvglUi.h for the contract.
#if defined(USE_LVGL) && USE_LVGL

#include "LvglUi.h"

#include <Arduino.h>
#include <lvgl.h>

#include <cstdio>
#include <cstring>

#include "TouchCal.h"        // crosshair targets for the touch-cal ritual screen
#include "WorkoutEngine.h"   // segmentTargetW for the interval profile
#include "WorkoutPresets.h"  // preset labels for the picker
#include "LcdTheme.h"        // generated LVGL palette (design/tokens.json) — C_BG()/C_CARD()/...
#include "Onboarding.h"      // shared wifiQrPayload() for the setup-AP QR (U4)

// Inter, baked by lv_font_conv (src/ui/fonts/, inputs in design/fonts/inter)
LV_FONT_DECLARE(lv_inter_12);
LV_FONT_DECLARE(lv_inter_16);
LV_FONT_DECLARE(lv_inter_sb_20);
LV_FONT_DECLARE(lv_inter_sb_28);
LV_FONT_DECLARE(lv_inter_sb_64);

namespace sb20proxy {
namespace {

// ---- design tokens: the palette C_BG()/C_CARD()/... is GENERATED into LcdTheme.h from
// design/tokens.json (the single source shared with the web SPA + LcdCanvas; CI test_tokens_sync
// guards drift). Bring the theme:: accessors into scope so existing C_*() call sites resolve.
using namespace theme;

LvglDriverHooks g_hooks{};
int g_hor = 240, g_ver = 320;

// pending UiActions from widget events (single-slot queue is enough at tap rate)
UiAction g_pending{};
bool g_hasPending = false;
void emitAction(UiAction a) { g_pending = a; g_hasPending = true; }

bool g_wkPaused = false;      // cached from the last update (Pause vs Resume semantics)
uint8_t g_brightness = 100;   // cached for the brightness-cycle row

// ---- display flush + optional serial tee (the SCREEN command) ------------------------------
volatile bool g_dumpActive = false;
void lvFlushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    const int w = area->x2 - area->x1 + 1, h = area->y2 - area->y1 + 1;
    if (g_hooks.flushArea)
        g_hooks.flushArea(area->x1, area->y1, area->x2, area->y2, (const uint16_t*)px_map);
    if (g_dumpActive) {
        // <AREA x1 y1 x2 y2 base64(rgb565-le)> — the Python grabber reassembles the frame
        static const char kB64[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        Serial.printf("<AREA %d %d %d %d ", area->x1, area->y1, area->x2, area->y2);
        const uint8_t* p = px_map;
        size_t n = (size_t)w * h * 2;
        uint32_t acc = 0;
        int bits = 0;
        static uint8_t obuf[512];
        size_t on = 0;
        for (size_t i = 0; i < n; ++i) {
            acc = (acc << 8) | p[i];
            bits += 8;
            while (bits >= 6) {
                bits -= 6;
                obuf[on++] = (uint8_t)kB64[(acc >> bits) & 0x3F];
                if (on == sizeof(obuf)) { Serial.write(obuf, on); on = 0; }
            }
        }
        if (bits > 0) obuf[on++] = (uint8_t)kB64[(acc << (6 - bits)) & 0x3F];
        if (on) Serial.write(obuf, on);
        Serial.println(">");
    }
    lv_display_flush_ready(disp);
}

void lvTouchCb(lv_indev_t*, lv_indev_data_t* data) {
    int x = 0, y = 0;
    if (g_hooks.readTouch && g_hooks.readTouch(x, y)) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = (int32_t)x;
        data->point.y = (int32_t)y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ---- widget helpers --------------------------------------------------------------------------
lv_obj_t* mkPanel(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    return o;
}
lv_obj_t* mkCard(lv_obj_t* parent, int radius = 12) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_color(o, C_CARD(), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_pad_all(o, 10, 0);
    return o;
}
lv_obj_t* mkLabel(lv_obj_t* parent, const lv_font_t* f, lv_color_t c, const char* txt = "") {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, txt);
    return l;
}
lv_obj_t* mkDot(lv_obj_t* parent, lv_color_t c) {
    lv_obj_t* d = lv_obj_create(parent);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, 8, 8);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(d, c, 0);
    return d;
}
lv_obj_t* mkButton(lv_obj_t* parent, const char* txt, lv_color_t bg, lv_color_t fg,
                   const lv_font_t* f, lv_event_cb_t cb, void* user) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_bg_color(b, lv_color_darken(bg, 40), LV_STATE_PRESSED);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* l = mkLabel(b, f, fg, txt);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

// ---- screens --------------------------------------------------------------------------------
lv_obj_t* g_scrObj[5] = {};   // indexed by (int)LcdScreen: Ride, Setup, More, Workout, Calibrate
lv_obj_t* g_calScr = nullptr;
LcdScreen g_cur = LcdScreen::Ride;

struct Nav { lv_obj_t *ride, *setup, *more; };
Nav g_nav[5] = {};

struct {
    lv_obj_t *srcDot, *srcName, *outDot, *outName, *hero, *unitW, *cad, *bal;
    lv_obj_t* chart;
    lv_chart_series_t* ser;
    // details pop-down (title-bar tap): IN/OUT cards replace the chart + chips
    lv_obj_t *chev, *cardCad, *cardBal, *det;
    lv_obj_t *dInName, *dInW, *dInCad, *dInMeta;
    lv_obj_t *dOutName, *dOutW, *dOutCad, *dOutMeta;
} R{};
bool g_rideDetails = false;
struct {
    lv_obj_t *title, *clock, *stepLine, *target, *now, *ergLine;
    lv_obj_t *bStart, *bChange, *bPause, *bSkip, *bStop, *bPauseLabel;
    lv_obj_t *pickPanel, *runPanel;
    lv_obj_t* prof;
    lv_chart_series_t* profSer;
} W{};
struct {
    lv_obj_t* rows[6];
    lv_obj_t* rowName[6];
    lv_obj_t* rowMeta[6];
    lv_obj_t *bScan, *bSave, *empty;
} S{};
struct { lv_obj_t* val[8]; lv_obj_t* ip; } M{};
struct { lv_obj_t *sub, *btn; } CAL{};
struct { lv_obj_t *cross_h, *cross_v, *box, *head, *sub, *test_h, *test_v; } TC{};

void navTo(LcdScreen s);

void navEventCb(lv_event_t* e) { navTo((LcdScreen)(int)(intptr_t)lv_event_get_user_data(e)); }

void mkNav(lv_obj_t* scr, int idx) {
    lv_obj_t* bar = mkPanel(scr);
    lv_obj_set_size(bar, g_hor, 30);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x10141f), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(bar, C_LINE(), 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
    static const char* names[3] = {"Ride", "Setup", "More"};
    static const LcdScreen tgt[3] = {LcdScreen::Ride, LcdScreen::Setup, LcdScreen::More};
    lv_obj_t** slots[3] = {&g_nav[idx].ride, &g_nav[idx].setup, &g_nav[idx].more};
    for (int i = 0; i < 3; ++i) {
        lv_obj_t* b = lv_button_create(bar);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, g_hor / 3, 30);
        lv_obj_align(b, LV_ALIGN_LEFT_MID, i * (g_hor / 3), 0);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t* l = mkLabel(b, &lv_inter_16, C_MUT(), names[i]);
        lv_obj_center(l);
        *slots[i] = l;
        lv_obj_add_event_cb(b, navEventCb, LV_EVENT_CLICKED, (void*)(intptr_t)(int)tgt[i]);
    }
}

void tintNav(int idx, LcdScreen active) {
    if (!g_nav[idx].ride) return;
    lv_obj_set_style_text_color(g_nav[idx].ride,
                                active == LcdScreen::Ride ? C_ACCENT() : C_MUT(), 0);
    lv_obj_set_style_text_color(g_nav[idx].setup,
                                active == LcdScreen::Setup ? C_ACCENT() : C_MUT(), 0);
    lv_obj_set_style_text_color(g_nav[idx].more,
                                active == LcdScreen::More ? C_ACCENT() : C_MUT(), 0);
}

lv_obj_t* mkScreen() {
    lv_obj_t* s = lv_obj_create(nullptr);
    lv_obj_remove_style_all(s);
    lv_obj_set_style_bg_color(s, C_BG(), 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    return s;
}

// --- Ride -------------------------------------------------------------------
void rideDetailsSet(bool on) {
    g_rideDetails = on;
    auto vis = [](lv_obj_t* o, bool show) {
        if (!o) return;
        if (show) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    };
    vis(R.det, on);
    vis(R.chart, !on);
    vis(R.cardCad, !on);
    vis(R.cardBal, !on);
    if (R.chev) lv_label_set_text(R.chev, on ? "^" : "v");
}
void rideTitleCb(lv_event_t*) { rideDetailsSet(!g_rideDetails); }

void buildRide() {
    lv_obj_t* s = g_scrObj[0] = mkScreen();
    lv_obj_t* tb = mkPanel(s);
    lv_obj_set_size(tb, g_hor, 34);
    lv_obj_set_style_bg_color(tb, C_TITLE(), 0);
    lv_obj_set_style_bg_opa(tb, LV_OPA_COVER, 0);
    R.srcDot = mkDot(tb, C_OK());
    lv_obj_align(R.srcDot, LV_ALIGN_LEFT_MID, 8, 0);
    R.srcName = mkLabel(tb, &lv_inter_16, C_FG());
    lv_obj_set_size(R.srcName, g_hor / 2 - 36, 20);  // height-pinned: LONG_DOT only ellipsizes
    lv_label_set_long_mode(R.srcName, LV_LABEL_LONG_DOT);  // when it can't grow (172px panels wrap)
    lv_obj_align(R.srcName, LV_ALIGN_LEFT_MID, 22, 0);
    lv_obj_t* arrow = mkLabel(tb, &lv_inter_16, C_ACCENT(), ">");
    lv_obj_align(arrow, LV_ALIGN_LEFT_MID, g_hor / 2 - 10, 0);
    R.outDot = mkDot(tb, C_OK());
    lv_obj_align(R.outDot, LV_ALIGN_LEFT_MID, g_hor / 2 + 6, 0);
    R.outName = mkLabel(tb, &lv_inter_16, C_FG());
    lv_obj_set_size(R.outName, g_hor / 2 - 40, 20);
    lv_label_set_long_mode(R.outName, LV_LABEL_LONG_DOT);
    lv_obj_align(R.outName, LV_ALIGN_LEFT_MID, g_hor / 2 + 20, 0);
    R.chev = mkLabel(tb, &lv_inter_16, C_MUT(), "v");
    lv_obj_align(R.chev, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_add_flag(tb, LV_OBJ_FLAG_CLICKABLE);           // tap the title -> IN/OUT details
    lv_obj_add_event_cb(tb, rideTitleCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* pw = mkLabel(s, &lv_inter_12, C_MUT(), "P O W E R");
    lv_obj_align(pw, LV_ALIGN_TOP_MID, 0, 46);
    R.hero = mkLabel(s, &lv_inter_sb_64, C_FG(), "0");
    lv_obj_align(R.hero, LV_ALIGN_TOP_MID, -10, 60);
    R.unitW = mkLabel(s, &lv_inter_sb_20, C_MUT(), "W");
    lv_obj_align_to(R.unitW, R.hero, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -8);

    R.chart = lv_chart_create(s);
    lv_obj_remove_style_all(R.chart);
    lv_obj_set_size(R.chart, g_hor - 20, 56);
    lv_obj_align(R.chart, LV_ALIGN_TOP_MID, 0, 140);
    lv_chart_set_type(R.chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(R.chart, 48);
    lv_chart_set_range(R.chart, LV_CHART_AXIS_PRIMARY_Y, 0, 400);
    lv_obj_set_style_line_width(R.chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(R.chart, 0, 0, LV_PART_INDICATOR);
    R.ser = lv_chart_add_series(R.chart, C_ACCENT(), LV_CHART_AXIS_PRIMARY_Y);

    const int cardW = (g_hor - 30) / 2;
    lv_obj_t* c1 = mkCard(s);
    lv_obj_set_size(c1, cardW, 64);
    lv_obj_align(c1, LV_ALIGN_TOP_LEFT, 10, 208);
    mkLabel(c1, &lv_inter_12, C_MUT(), "Cadence");
    R.cad = mkLabel(c1, &lv_inter_sb_28, C_FG(), "--");
    lv_obj_align(R.cad, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_t* c2 = mkCard(s);
    lv_obj_set_size(c2, cardW, 64);
    lv_obj_align(c2, LV_ALIGN_TOP_RIGHT, -10, 208);
    mkLabel(c2, &lv_inter_12, C_MUT(), "Balance");
    R.bal = mkLabel(c2, &lv_inter_sb_28, C_FG(), "--");
    lv_obj_align(R.bal, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    R.cardCad = c1;
    R.cardBal = c2;

    // details pop-down: IN/OUT cards replace the chart + chips while open
    R.det = mkPanel(s);
    lv_obj_set_size(R.det, g_hor, 134);
    lv_obj_align(R.det, LV_ALIGN_TOP_MID, 0, 138);
    lv_obj_add_flag(R.det, LV_OBJ_FLAG_HIDDEN);
    const int dw = (g_hor - 26) / 2;
    auto mkSide = [&](lv_align_t al, int ox, const char* badge, lv_color_t bc, lv_obj_t** name,
                      lv_obj_t** watts, lv_obj_t** cad, lv_obj_t** meta) {
        lv_obj_t* card = mkCard(R.det);
        lv_obj_set_size(card, dw, 134);
        lv_obj_align(card, al, ox, 0);
        mkLabel(card, &lv_inter_12, bc, badge);
        *name = mkLabel(card, &lv_inter_12, C_FG());
        lv_obj_set_width(*name, dw - 20);
        lv_label_set_long_mode(*name, LV_LABEL_LONG_DOT);
        lv_obj_align(*name, LV_ALIGN_TOP_LEFT, 0, 18);
        *watts = mkLabel(card, &lv_inter_sb_28, C_FG(), "0");
        lv_obj_align(*watts, LV_ALIGN_TOP_LEFT, 0, 38);
        *cad = mkLabel(card, &lv_inter_12, C_MUT(), "--");
        lv_obj_align(*cad, LV_ALIGN_TOP_LEFT, 0, 74);
        *meta = mkLabel(card, &lv_inter_12, C_MUT(), "");
        lv_obj_align(*meta, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    };
    mkSide(LV_ALIGN_TOP_LEFT, 10, "IN", C_OK(), &R.dInName, &R.dInW, &R.dInCad, &R.dInMeta);
    mkSide(LV_ALIGN_TOP_RIGHT, -10, "OUT", C_ACCENT(), &R.dOutName, &R.dOutW, &R.dOutCad,
           &R.dOutMeta);

    mkNav(s, 0);
}

// --- Workout ----------------------------------------------------------------
void wkActionCb(lv_event_t* e) {
    const char* act = (const char*)lv_event_get_user_data(e);
    UiAction a;
    if (!strcmp(act, "start")) a.type = UiAction::WorkoutStart;
    else if (!strcmp(act, "pause")) a.type = g_wkPaused ? UiAction::WorkoutResume
                                                        : UiAction::WorkoutPause;
    else if (!strcmp(act, "skip")) a.type = UiAction::WorkoutSkip;
    else if (!strcmp(act, "stop")) a.type = UiAction::WorkoutStop;
    else if (!strcmp(act, "change")) a.type = UiAction::WorkoutUnload;
    emitAction(a);
}
void wkPresetCb(lv_event_t* e) {
    UiAction a;
    a.type = UiAction::WorkoutPreset;
    a.index = (int)(intptr_t)lv_event_get_user_data(e);
    emitAction(a);
}

void buildWorkout() {
    lv_obj_t* s = g_scrObj[3] = mkScreen();
    W.title = mkLabel(s, &lv_inter_sb_20, C_FG(), "Workout");
    lv_obj_align(W.title, LV_ALIGN_TOP_LEFT, 10, 8);
    W.clock = mkLabel(s, &lv_inter_sb_20, C_MUT(), "");
    lv_obj_align(W.clock, LV_ALIGN_TOP_RIGHT, -10, 8);

    // preset picker (not-loaded state) — labels from the real preset table
    W.pickPanel = mkPanel(s);
    lv_obj_set_size(W.pickPanel, g_hor, 246);
    lv_obj_align(W.pickPanel, LV_ALIGN_TOP_MID, 0, 40);
    const auto& presets = workoutPresets();
    const int nPresets = (int)presets.size() < 4 ? (int)presets.size() : 4;
    for (int i = 0; i < nPresets; ++i) {
        lv_obj_t* b = lv_button_create(W.pickPanel);
        lv_obj_remove_style_all(b);
        lv_obj_set_style_bg_color(b, C_CARD(), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(b, 12, 0);
        lv_obj_set_style_bg_color(b, lv_color_darken(C_CARD(), 40), LV_STATE_PRESSED);
        lv_obj_set_size(b, g_hor - 20, 44);
        lv_obj_align(b, LV_ALIGN_TOP_MID, 0, i * 52);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t* l = mkLabel(b, &lv_inter_16, C_FG(), presets[i].label);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 12, 0);
        lv_obj_add_event_cb(b, wkPresetCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
    lv_obj_t* hint = mkLabel(W.pickPanel, &lv_inter_12, C_MUT(), "or send from phone");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, nPresets * 52 + 6);

    // loaded/running panel
    W.runPanel = mkPanel(s);
    lv_obj_set_size(W.runPanel, g_hor, 250);
    lv_obj_align(W.runPanel, LV_ALIGN_TOP_MID, 0, 36);
    W.stepLine = mkLabel(W.runPanel, &lv_inter_16, C_MUT(), "");
    lv_obj_align(W.stepLine, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t* tl = mkLabel(W.runPanel, &lv_inter_12, C_ACCENT(), "T A R G E T");
    lv_obj_align(tl, LV_ALIGN_TOP_MID, 0, 20);
    W.target = mkLabel(W.runPanel, &lv_inter_sb_64, C_FG(), "--");
    lv_obj_align(W.target, LV_ALIGN_TOP_MID, 0, 34);
    W.now = mkLabel(W.runPanel, &lv_inter_16, C_OK(), "");
    lv_obj_align(W.now, LV_ALIGN_TOP_MID, 0, 102);

    W.prof = lv_chart_create(W.runPanel);
    lv_obj_remove_style_all(W.prof);
    lv_obj_set_style_bg_color(W.prof, C_CARD(), 0);
    lv_obj_set_style_bg_opa(W.prof, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(W.prof, 8, 0);
    lv_obj_set_size(W.prof, g_hor - 20, 52);
    lv_obj_align(W.prof, LV_ALIGN_TOP_MID, 0, 124);
    lv_chart_set_type(W.prof, LV_CHART_TYPE_BAR);
    lv_obj_set_style_pad_column(W.prof, 2, LV_PART_MAIN);
    W.profSer = lv_chart_add_series(W.prof, C_ACCENT(), LV_CHART_AXIS_PRIMARY_Y);

    W.ergLine = mkLabel(W.runPanel, &lv_inter_12, C_MUT(), "");
    lv_obj_align(W.ergLine, LV_ALIGN_TOP_LEFT, 10, 182);

    W.bStart = mkButton(W.runPanel, "S T A R T", C_ACCENT(), C_FG(), &lv_inter_sb_20,
                        wkActionCb, (void*)"start");
    lv_obj_set_size(W.bStart, (g_hor - 25) * 2 / 3, 42);
    lv_obj_align(W.bStart, LV_ALIGN_BOTTOM_LEFT, 10, -2);
    W.bChange = mkButton(W.runPanel, "Change", C_CARD(), C_FG(), &lv_inter_16, wkActionCb,
                         (void*)"change");
    lv_obj_set_size(W.bChange, (g_hor - 25) / 3, 42);
    lv_obj_align(W.bChange, LV_ALIGN_BOTTOM_RIGHT, -10, -2);
    const int bw = (g_hor - 20 - 10) / 3;
    W.bPause = mkButton(W.runPanel, "Pause", C_CARD(), C_FG(), &lv_inter_16, wkActionCb,
                        (void*)"pause");
    W.bPauseLabel = lv_obj_get_child(W.bPause, 0);
    lv_obj_set_size(W.bPause, bw, 42);
    lv_obj_align(W.bPause, LV_ALIGN_BOTTOM_LEFT, 10, -2);
    W.bSkip = mkButton(W.runPanel, "Skip", C_CARD(), C_FG(), &lv_inter_16, wkActionCb,
                       (void*)"skip");
    lv_obj_set_size(W.bSkip, bw, 42);
    lv_obj_align(W.bSkip, LV_ALIGN_BOTTOM_LEFT, 10 + bw + 5, -2);
    W.bStop = mkButton(W.runPanel, "Stop", C_CARD(), C_BAD(), &lv_inter_16, wkActionCb,
                       (void*)"stop");
    lv_obj_set_size(W.bStop, bw, 42);
    lv_obj_align(W.bStop, LV_ALIGN_BOTTOM_LEFT, 10 + 2 * (bw + 5), -2);

    mkNav(s, 3);
}

// --- Setup ------------------------------------------------------------------
void setupPickCb(lv_event_t* e) {
    UiAction a;
    a.type = UiAction::SetupPick;
    a.index = (int)(intptr_t)lv_event_get_user_data(e);
    emitAction(a);
}
void setupActCb(lv_event_t* e) {
    UiAction a;
    a.type = (UiAction::Type)(intptr_t)lv_event_get_user_data(e);
    emitAction(a);
}

void buildSetup() {
    lv_obj_t* s = g_scrObj[1] = mkScreen();
    lv_obj_t* h = mkLabel(s, &lv_inter_sb_20, C_FG(), "Pick devices");
    lv_obj_align(h, LV_ALIGN_TOP_LEFT, 10, 8);
    lv_obj_t* hint = mkLabel(s, &lv_inter_12, C_MUT(), "tap a row - meter=power trn=erg");
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 10, 34);
    for (int i = 0; i < 6; ++i) {
        lv_obj_t* row = lv_button_create(s);
        lv_obj_remove_style_all(row);
        lv_obj_set_style_bg_color(row, C_CARD(), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 10, 0);
        lv_obj_set_style_bg_color(row, lv_color_darken(C_CARD(), 40), LV_STATE_PRESSED);
        lv_obj_set_size(row, g_hor - 20, 34);
        lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 54 + i * 38);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
        S.rowName[i] = mkLabel(row, &lv_inter_16, C_FG(), "");
        lv_obj_set_size(S.rowName[i], g_hor - 20 - 108, 20);  // leave room for "trainer -61"
        lv_label_set_long_mode(S.rowName[i], LV_LABEL_LONG_DOT);
        lv_obj_align(S.rowName[i], LV_ALIGN_LEFT_MID, 10, 0);
        S.rowMeta[i] = mkLabel(row, &lv_inter_12, C_MUT(), "");
        lv_obj_align(S.rowMeta[i], LV_ALIGN_RIGHT_MID, -10, 0);
        lv_obj_add_event_cb(row, setupPickCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        S.rows[i] = row;
    }
    S.empty = mkLabel(s, &lv_inter_12, C_MUT(), "no devices - wake the meter + Rescan");
    lv_obj_align(S.empty, LV_ALIGN_TOP_MID, 0, 64);
    S.bScan = mkButton(s, "Rescan", C_CARD(), C_FG(), &lv_inter_16, setupActCb,
                       (void*)(intptr_t)UiAction::SetupScan);
    lv_obj_set_size(S.bScan, (g_hor - 30) / 2, 36);
    lv_obj_align(S.bScan, LV_ALIGN_BOTTOM_LEFT, 10, -36);
    S.bSave = mkButton(s, "Save", C_CARD(), C_MUT(), &lv_inter_16, setupActCb,
                       (void*)(intptr_t)UiAction::SetupSave);
    lv_obj_set_size(S.bSave, (g_hor - 30) / 2, 36);
    lv_obj_align(S.bSave, LV_ALIGN_BOTTOM_RIGHT, -10, -36);
    mkNav(s, 1);
}

// --- More -------------------------------------------------------------------
void moreRowCb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx == 0) navTo(LcdScreen::Workout);
    else if (idx == 1) navTo(LcdScreen::Calibrate);
    else if (idx == 6) {  // brightness cycles 25 -> 50 -> 75 -> 100
        UiAction a;
        a.type = UiAction::SetBrightness;
        a.index = g_brightness >= 100 ? 25 : g_brightness + 25;
        emitAction(a);
    } else if (idx == 7) {  // Touch cal -> run the tap-the-crosshair ritual
        UiAction a;
        a.type = UiAction::TouchCalStart;
        emitAction(a);
    }
}

void buildMore() {
    lv_obj_t* s = g_scrObj[2] = mkScreen();
    lv_obj_t* h = mkLabel(s, &lv_inter_sb_20, C_FG(), "Settings");
    lv_obj_align(h, LV_ALIGN_TOP_LEFT, 10, 8);
    static const char* rowName[8] = {"Workout", "Calibrate", "Mode", "Identity",
                                     "Source",  "Trainer",   "Bright", "Touch cal"};
#if defined(LCD_DRIVER_CYD) && LCD_DRIVER_CYD
    const int nRows = 8;   // resistive film -> expose the cal ritual in the UI
#else
    const int nRows = 7;   // capacitive (S3) needs no touch cal
#endif
    for (int i = 0; i < nRows; ++i) {
        lv_obj_t* row = lv_button_create(s);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, g_hor - 20, 27);
        lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 36 + i * 29);
        lv_obj_set_style_border_color(row, C_LINE(), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t* n = mkLabel(row, &lv_inter_16, C_FG(), rowName[i]);
        lv_obj_align(n, LV_ALIGN_LEFT_MID, 4, 0);
        M.val[i] = mkLabel(row, &lv_inter_16, i == 6 ? C_ACCENT() : C_MUT(), "");
        lv_obj_align(M.val[i], LV_ALIGN_RIGHT_MID, -4, 0);
        lv_obj_add_event_cb(row, moreRowCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
    M.ip = mkLabel(s, &lv_inter_12, C_MUT(), "");
    lv_obj_align(M.ip, LV_ALIGN_BOTTOM_MID, 0, -34);
    mkNav(s, 2);
}

// --- Calibrate (corrector wizard entry) --------------------------------------
void buildCalibrate() {
    lv_obj_t* s = g_scrObj[4] = mkScreen();
    lv_obj_t* h = mkLabel(s, &lv_inter_sb_20, C_FG(), "Calibrate");
    lv_obj_align(h, LV_ALIGN_TOP_LEFT, 10, 8);
    CAL.sub = mkLabel(s, &lv_inter_16, C_MUT(), "correct DUT to match ref\nwake both meters...");
    lv_obj_align(CAL.sub, LV_ALIGN_TOP_LEFT, 10, 40);
    CAL.btn = mkButton(s, "Connect + start", C_CARD(), C_MUT(), &lv_inter_16,
                       [](lv_event_t*) {
                           UiAction a;
                           a.type = UiAction::CalStart;
                           emitAction(a);
                       }, nullptr);
    lv_obj_set_size(CAL.btn, g_hor - 20, 40);
    lv_obj_align(CAL.btn, LV_ALIGN_BOTTOM_MID, 0, -40);
    mkNav(s, 4);
}

// --- WiFi-onboarding (captive portal) screen -------------------------------------
// Shown INSTEAD of the normal UI while the setup portal is up: a phone-scannable QR that
// joins the board's setup AP (WIFI:T:WPA;...), plus the SSID/PIN as text for manual entry.
struct { lv_obj_t *scr, *qr, *ssid, *pin, *url; } PV{};
bool g_provShown = false;

void buildProvision() {
    PV.scr = mkScreen();
    lv_obj_t* h = mkLabel(PV.scr, &lv_inter_sb_20, C_FG(), "Wi-Fi setup");
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_t* sub = mkLabel(PV.scr, &lv_inter_12, C_MUT(), "scan with your phone camera");
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 38);
    // QR on a white card: scanners want dark-on-light with a quiet border
    const int qsz = (g_hor - 60) < 150 ? (g_hor - 60) : 150;
    lv_obj_t* card = lv_obj_create(PV.scr);
    lv_obj_remove_style_all(card);
    lv_obj_set_style_bg_color(card, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_size(card, qsz + 20, qsz + 20);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 58);
    PV.qr = lv_qrcode_create(card);
    lv_qrcode_set_size(PV.qr, qsz);
    lv_qrcode_set_dark_color(PV.qr, lv_color_black());
    lv_qrcode_set_light_color(PV.qr, lv_color_white());
    lv_obj_center(PV.qr);
    PV.ssid = mkLabel(PV.scr, &lv_inter_16, C_FG(), "");
    lv_obj_align(PV.ssid, LV_ALIGN_TOP_MID, 0, 58 + qsz + 30);
    PV.pin = mkLabel(PV.scr, &lv_inter_16, C_FG(), "");
    lv_obj_align(PV.pin, LV_ALIGN_TOP_MID, 0, 58 + qsz + 52);
    PV.url = mkLabel(PV.scr, &lv_inter_12, C_MUT(), "");
    lv_obj_align(PV.url, LV_ALIGN_BOTTOM_MID, 0, -8);
}

void provisionSync(const ProvisionView& v) {
    if (v.portal) {
        if (!g_provShown) {
            const std::string qrtxt = wifiQrPayload(v.apSsid, v.pin);  // shared + escaped (U4)
            lv_qrcode_update(PV.qr, qrtxt.c_str(), (uint32_t)qrtxt.size());
            char buf[64];
            snprintf(buf, sizeof(buf), "Wi-Fi: %s", v.apSsid.c_str());
            lv_label_set_text(PV.ssid, buf);
            snprintf(buf, sizeof(buf), "Password: %s", v.pin.c_str());
            lv_label_set_text(PV.pin, buf);
            snprintf(buf, sizeof(buf), "then open %s", v.url.c_str());
            lv_label_set_text(PV.url, buf);
            lv_screen_load(PV.scr);
            g_provShown = true;
        }
    } else if (g_provShown) {
        g_provShown = false;
        navTo(g_cur);  // provisioned (or portal gone): back to the regular UI
    }
}

// --- touch-cal ritual screen ---------------------------------------------------
void buildTouchCal() {
    g_calScr = mkScreen();
    auto bar = [&](lv_color_t c, int w, int h) {
        lv_obj_t* o = lv_obj_create(g_calScr);
        lv_obj_remove_style_all(o);
        lv_obj_set_style_bg_color(o, c, 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_size(o, w, h);
        return o;
    };
    TC.cross_h = bar(C_ACCENT(), 25, 2);
    TC.cross_v = bar(C_ACCENT(), 2, 25);
    TC.box = lv_obj_create(g_calScr);
    lv_obj_remove_style_all(TC.box);
    lv_obj_set_style_border_color(TC.box, C_FG(), 0);
    lv_obj_set_style_border_width(TC.box, 1, 0);
    lv_obj_set_size(TC.box, 13, 13);
    TC.head = mkLabel(g_calScr, &lv_inter_sb_28, C_FG(), "Touch cal");
    lv_obj_align(TC.head, LV_ALIGN_CENTER, 0, -20);
    TC.sub = mkLabel(g_calScr, &lv_inter_16, C_MUT(), "");
    lv_obj_align(TC.sub, LV_ALIGN_CENTER, 0, 14);
    TC.test_h = bar(C_OK(), 17, 2);
    TC.test_v = bar(C_OK(), 2, 17);
    lv_obj_add_flag(TC.test_h, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(TC.test_v, LV_OBJ_FLAG_HIDDEN);
}

void navTo(LcdScreen s) {
    g_cur = s;
    int idx = (int)s;
    if (idx < 0 || idx > 4 || !g_scrObj[idx]) return;
    lv_screen_load(g_scrObj[idx]);
    tintNav(idx, s);
}

}  // namespace

// ---- public API ------------------------------------------------------------------------------
void lvglUiInit(const LvglDriverHooks& hooks, int hor, int ver) {
    g_hooks = hooks;
    g_hor = hor;
    g_ver = ver;
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return millis(); });
    lv_display_t* d = lv_display_create(hor, ver);
    // Partial render buffer: 1/8 frame with PSRAM headroom, 1/16 without — on the no-PSRAM CYD
    // every KB of internal DRAM matters (lwIP needs ~11 KB of TCP buffers per HTTP socket; at
    // ~27 KB free the web pages stalled, 2026-07-04). Smaller buffer = more flush chunks, which
    // the eye doesn't notice at our 5 Hz data cadence.
    static uint8_t* buf = nullptr;
    const int frac = psramFound() ? 8 : 16;
    const size_t bufSz = (size_t)hor * (ver / frac) * 2;
    if (!buf) buf = (uint8_t*)heap_caps_malloc(bufSz, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!buf) buf = (uint8_t*)malloc(bufSz);
    lv_display_set_buffers(d, buf, nullptr, (uint32_t)bufSz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, lvFlushCb);
    lv_indev_t* in = lv_indev_create();
    lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(in, lvTouchCb);

    buildRide();
    buildWorkout();
    buildSetup();
    buildMore();
    buildCalibrate();
    buildTouchCal();
    buildProvision();
    navTo(LcdScreen::Ride);
}

void lvglUiTick() { lv_timer_handler(); }

LcdScreen lvglUiCurrentScreen() { return g_cur; }
void lvglUiShowScreen(LcdScreen s) { navTo(s); }

bool lvglUiPollAction(UiAction& out) {
    if (!g_hasPending) return false;
    out = g_pending;
    g_hasPending = false;
    return true;
}

void lvglUiUpdate(const LcdViews& v) {
    char buf[64];
    g_wkPaused = v.wk.paused;
    g_brightness = v.more.brightness;
    provisionSync(v.prov);  // captive portal up -> the QR onboarding screen owns the panel

    // Ride
    lv_label_set_text(R.srcName, v.ride.srcName.empty() ? "searching" : v.ride.srcName.c_str());
    lv_obj_set_style_bg_color(R.srcDot, v.ride.srcOn ? C_OK() : C_BAD(), 0);
    lv_label_set_text(R.outName, v.ride.outName.c_str());
    snprintf(buf, sizeof(buf), "%d", (int)v.ride.watts);
    lv_label_set_text(R.hero, buf);
    lv_obj_align(R.hero, LV_ALIGN_TOP_MID, -10, 60);
    lv_obj_align_to(R.unitW, R.hero, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -8);
    if (v.ride.cadence >= 0) snprintf(buf, sizeof(buf), "%d", (int)v.ride.cadence);
    else snprintf(buf, sizeof(buf), "--");
    lv_label_set_text(R.cad, buf);
    if (v.ride.balancePct >= 0)
        snprintf(buf, sizeof(buf), "%d/%d", (int)v.ride.balancePct, 100 - (int)v.ride.balancePct);
    else snprintf(buf, sizeof(buf), "--");
    lv_label_set_text(R.bal, buf);
    lv_chart_set_next_value(R.chart, R.ser, v.ride.watts < 0 ? 0 : v.ride.watts);
    if (g_rideDetails) {  // details pop-down open: refresh the IN/OUT cards
        lv_label_set_text(R.dInName, v.ride.srcName.empty() ? "searching" : v.ride.srcName.c_str());
        lv_label_set_text(R.dOutName, v.ride.outName.c_str());
        snprintf(buf, sizeof(buf), "%dW", (int)v.ride.srcWatts);
        lv_label_set_text(R.dInW, buf);
        snprintf(buf, sizeof(buf), "%dW", (int)v.ride.watts);
        lv_label_set_text(R.dOutW, buf);
        if (v.ride.cadence >= 0) snprintf(buf, sizeof(buf), "%d rpm", (int)v.ride.cadence);
        else snprintf(buf, sizeof(buf), "-- rpm");
        lv_label_set_text(R.dInCad, buf);
        lv_label_set_text(R.dOutCad, buf);
        snprintf(buf, sizeof(buf), "%ld dBm", (long)v.ride.wifiRssi);
        lv_label_set_text(R.dInMeta, buf);
        snprintf(buf, sizeof(buf), "up %lum", (unsigned long)(v.ride.uptimeMs / 60000u));
        lv_label_set_text(R.dOutMeta, buf);
    }

    // Workout
    const WorkoutView& w = v.wk;
    if (w.loaded && w.w) {
        lv_obj_add_flag(W.pickPanel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(W.runPanel, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(W.title, w.w->name.empty() ? "Workout" : w.w->name.c_str());
        snprintf(buf, sizeof(buf), "%ld:%02ld", w.st.totalElapsedS / 60, w.st.totalElapsedS % 60);
        lv_label_set_text(W.clock, buf);
        int nSeg = (int)w.w->segments.size();
        snprintf(buf, sizeof(buf), "%d of %d  -  %ld:%02ld left",
                 w.st.segIndex + (w.st.finished ? 0 : 1), nSeg, w.st.segRemainingS / 60,
                 w.st.segRemainingS % 60);
        lv_label_set_text(W.stepLine, buf);
        if (w.st.targetW >= 0) snprintf(buf, sizeof(buf), "%d", w.st.targetW);
        else snprintf(buf, sizeof(buf), "--");
        lv_label_set_text(W.target, buf);
        snprintf(buf, sizeof(buf), "now %dW . %drpm", (int)v.ride.watts,
                 v.ride.cadence < 0 ? 0 : (int)v.ride.cadence);
        lv_label_set_text(W.now, buf);
        if (!w.ergConfigured) snprintf(buf, sizeof(buf), "erg: no trainer set");
        else if (!w.ergConnected) snprintf(buf, sizeof(buf), "erg: connecting...");
        else if (!w.ergControlled) snprintf(buf, sizeof(buf), "erg: linked, no ctrl");
        else snprintf(buf, sizeof(buf), "erg: ON %dW", (int)w.ergTarget);
        lv_label_set_text(W.ergLine, buf);

        int n = nSeg > 32 ? 32 : nSeg;
        lv_chart_set_point_count(W.prof, (uint32_t)n);
        int maxW = 100;
        for (int i = 0; i < n; ++i) {
            int t = segmentTargetW(w.w->segments[i], w.w->ftpW);
            if (t > maxW) maxW = t;
        }
        lv_chart_set_range(W.prof, LV_CHART_AXIS_PRIMARY_Y, 0, maxW + 20);
        for (int i = 0; i < n; ++i)
            lv_chart_set_value_by_id(W.prof, W.profSer, (uint32_t)i,
                                     segmentTargetW(w.w->segments[i], w.w->ftpW));
        lv_chart_refresh(W.prof);

        if (w.running) {
            lv_obj_add_flag(W.bStart, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(W.bChange, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(W.bPause, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(W.bSkip, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(W.bStop, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(W.bPauseLabel, w.paused ? "Resume" : "Pause");
        } else {
            lv_obj_remove_flag(W.bStart, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(W.bChange, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(W.bPause, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(W.bSkip, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(W.bStop, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_obj_remove_flag(W.pickPanel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(W.runPanel, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(W.title, "Workout");
        lv_label_set_text(W.clock, "");
    }

    // Setup — RENDER THE SAME LIST THE TAP HANDLER RESOLVES (lcdPickerList): only usable
    // devices (meters/cranks/trainers), deduped + sorted. Rendering the raw list while the
    // handler sorted meant row taps could pick a DIFFERENT device than displayed.
    const auto pick = lcdPickerList(v.setup.devices);
    if (pick.empty()) lv_obj_remove_flag(S.empty, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(S.empty, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 6; ++i) {
        if (i < (int)pick.size()) {
            const auto& dev = pick[i];
            lv_obj_remove_flag(S.rows[i], LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(S.rowName[i],
                              dev.name.empty() ? dev.address.c_str() : dev.name.c_str());
            const bool selM = !v.setup.meterAddr.empty() && dev.address == v.setup.meterAddr;
            const bool selT = !v.setup.trainerAddr.empty() && dev.address == v.setup.trainerAddr;
            const char* kind = dev.isFtms ? "trainer" : (dev.isStagesCrank ? "crank" : "meter");
            snprintf(buf, sizeof(buf), "%s %d%s%s", kind, dev.rssi, selM ? " *" : "",
                     selT ? " *" : "");
            lv_label_set_text(S.rowMeta[i], buf);
        } else {
            lv_obj_add_flag(S.rows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    // More
    lv_label_set_text(M.val[0], v.wk.loaded ? "loaded  >" : ">");
    lv_label_set_text(M.val[1], ">");
    lv_label_set_text(M.val[2], v.more.mode.c_str());
    lv_label_set_text(M.val[3], v.more.identity.c_str());
    lv_label_set_text(M.val[4], v.more.source.c_str());
    lv_label_set_text(M.val[5], v.more.trainer.empty() ? "not set" : v.more.trainer.c_str());
    snprintf(buf, sizeof(buf), "%d%%", (int)v.more.brightness);
    lv_label_set_text(M.val[6], buf);
#if defined(LCD_DRIVER_CYD) && LCD_DRIVER_CYD
    if (M.val[7]) lv_label_set_text(M.val[7], ">");
#endif
    lv_label_set_text(M.ip, v.more.ip.empty() ? "no wifi" : v.more.ip.c_str());
}

void lvglUiCalShow(int step, int done, int testX, int testY) {
    if (step < 0) {  // ritual over: return to the regular UI
        navTo(g_cur);
        return;
    }
    if (lv_screen_active() != g_calScr) lv_screen_load(g_calScr);
    char buf[44];
    if (done < 0) {
        int cx, cy;
        touchCalTarget(step, cx, cy);
        lv_obj_remove_flag(TC.cross_h, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(TC.cross_v, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(TC.box, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(TC.cross_h, cx - 12, cy - 1);
        lv_obj_set_pos(TC.cross_v, cx - 1, cy - 12);
        lv_obj_set_pos(TC.box, cx - 6, cy - 6);
        lv_obj_set_style_text_color(TC.head, C_FG(), 0);
        lv_label_set_text(TC.head, "Touch cal");
        snprintf(buf, sizeof(buf), "tap target %d/%d - hold firmly", step + 1, TOUCH_CAL_POINTS);
        lv_label_set_text(TC.sub, buf);
        lv_obj_add_flag(TC.test_h, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(TC.test_v, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(TC.cross_h, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(TC.cross_v, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(TC.box, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(TC.head, done == 1 ? C_OK() : C_BAD(), 0);
        lv_label_set_text(TC.head, done == 1 ? "Calibrated" : "Cal failed");
        lv_label_set_text(TC.sub, done == 1 ? "saved - tap around to test"
                                            : "taps too clustered - retrying");
        if (done == 1 && testX >= 0) {
            lv_obj_remove_flag(TC.test_h, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(TC.test_v, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(TC.test_h, testX - 8, testY - 1);
            lv_obj_set_pos(TC.test_v, testX - 1, testY - 8);
        }
    }
}

void lvglUiScreenDumpBegin() {
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
    // The S3's USB-CDC runs with TX-timeout 0 (never block when headless) — but that DROPS the
    // dump's bulk base64 whenever the tiny CDC buffer is full. A host is attached here (it just
    // sent SCREEN), so make writes blocking for the dump and restore after.
    Serial.setTxTimeoutMs(200);
#endif
    g_dumpActive = true;
    lv_obj_invalidate(lv_screen_active());  // force a full repaint through the tee
    lv_refr_now(nullptr);
    g_dumpActive = false;
    Serial.println("<DUMPDONE>");
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxTimeoutMs(0);
#endif
}
bool lvglUiScreenDumpActive() { return g_dumpActive; }
bool lvglUiRideDetails() { return g_rideDetails; }

}  // namespace sb20proxy
#endif  // USE_LVGL
