#pragma once
// TouchCal — old-school resistive touch calibration (the tap-the-crosshair ritual).
//
// The XPT2046 film on the CYD reports raw 12-bit ADC values whose span/offset (and possibly
// direction) vary film to film, so shipped constants can land taps offset. This module is the
// PURE, host-tested core of the fix:
//   * touchCalFit()  — per-axis least-squares fit raw -> screen from N tapped points (axes are
//     independent on this film; an inverted axis just yields a negative scale, handled naturally)
//   * touchCalApply() — map + clamp a raw sample through a fit
//   * renderTouchCalScreen() — the crosshair UI drawn into the LcdCanvas (band-safe: it only
//     uses canvas primitives)
// The device side (main.cpp, CYD builds) runs the ritual in the LCD task, persists the fit in
// NVS, and applies it in CydDisplay::readTap. Serial hooks (CALTOUCH / RAWTAP / CALINFO) let the
// whole flow be driven headlessly — the calibration itself is twin-testable.
#include <cmath>

#include "LcdCanvas.h"

namespace sb20proxy {

struct TouchCalPoint {
    float rawX, rawY;    // measured (median-filtered) XPT2046 sample
    float targetX, targetY;  // the crosshair's screen position
};

struct TouchCalFit {
    float sx = 0, ox = 0;  // screenX = rawX * sx + ox
    float sy = 0, oy = 0;  // screenY = rawY * sy + oy
    bool valid = false;
};

// Per-axis least squares: screen = raw*scale + offset. Returns invalid when the tapped raw
// values don't spread enough to trust (shorted film, taps on one spot, injected garbage).
inline TouchCalFit touchCalFit(const TouchCalPoint* pts, int n) {
    TouchCalFit f;
    if (n < 2) return f;
    auto fitAxis = [&](auto rawOf, auto tgtOf, float& scale, float& off) -> bool {
        float rm = 0, tm = 0;
        for (int i = 0; i < n; ++i) { rm += rawOf(pts[i]); tm += tgtOf(pts[i]); }
        rm /= n; tm /= n;
        float num = 0, den = 0;
        for (int i = 0; i < n; ++i) {
            num += (rawOf(pts[i]) - rm) * (tgtOf(pts[i]) - tm);
            den += (rawOf(pts[i]) - rm) * (rawOf(pts[i]) - rm);
        }
        // require a real spread of raw values across the tapped points (12-bit range: ~200+)
        if (den < 200.0f * 200.0f) return false;
        scale = num / den;
        off = tm - scale * rm;
        // sanity: a 12-bit swing must cover a plausible fraction of the panel
        float span = std::fabs(scale) * 4095.0f;
        return span > 60.0f && span < 4.0f * (LCD_W > LCD_H ? LCD_W : LCD_H);
    };
    bool okx = fitAxis([](const TouchCalPoint& p) { return p.rawX; },
                       [](const TouchCalPoint& p) { return p.targetX; }, f.sx, f.ox);
    bool oky = fitAxis([](const TouchCalPoint& p) { return p.rawY; },
                       [](const TouchCalPoint& p) { return p.targetY; }, f.sy, f.oy);
    f.valid = okx && oky;
    return f;
}

inline void touchCalApply(const TouchCalFit& f, float rawX, float rawY, int& x, int& y) {
    x = (int)std::lround(rawX * f.sx + f.ox);
    y = (int)std::lround(rawY * f.sy + f.oy);
    if (x < 0) x = 0;
    if (x >= LCD_W) x = LCD_W - 1;
    if (y < 0) y = 0;
    if (y >= LCD_H) y = LCD_H - 1;
}

// The 4 crosshair targets: corners inset far enough that a fingertip can center on them.
inline void touchCalTarget(int idx, int& x, int& y) {
    const int in = 25;
    switch (idx & 3) {
        case 0: x = in;          y = in;          break;
        case 1: x = LCD_W - in;  y = in;          break;
        case 2: x = in;          y = LCD_H - in;  break;
        default: x = LCD_W - in; y = LCD_H - in;  break;
    }
}
constexpr int TOUCH_CAL_POINTS = 4;

// The ritual screen: one crosshair at a time + progress; a "saved/failed" close-out state.
// done: -1 = collecting (show crosshair `step`), 1 = success screen, 0 = failure screen.
// On the success screen, (testX,testY) >= 0 draws a marker where the last verification tap
// mapped — the "tap around to test" feedback.
inline void renderTouchCalScreen(LcdCanvas& c, int step, int done, int testX = -1, int testY = -1) {
    c.clear(LCD_BG);
    if (done < 0) {
        int cx, cy;
        touchCalTarget(step, cx, cy);
        // crosshair: full-bleed thin lines + a boxed center, like the Palm Pilot days
        c.hline(cx - 12, cy, 25, LCD_ACCENT);
        c.vline(cx, cy - 12, 25, LCD_ACCENT);
        c.rect(cx - 6, cy - 6, 13, 13, LCD_FG);
        c.textCentered(LCD_H / 2 - 24, "Touch cal", 2, LCD_FG);
        char buf[24];
        snprintf(buf, sizeof(buf), "tap target %d/%d", step + 1, TOUCH_CAL_POINTS);
        c.textCentered(LCD_H / 2 + 4, buf, 1, LCD_MUT);
        c.textCentered(LCD_H / 2 + 20, "press firmly + hold", 1, LCD_MUT);
    } else if (done == 1) {
        c.textCentered(LCD_H / 2 - 16, "Calibrated", 2, LCD_OK);
        c.textCentered(LCD_H / 2 + 8, "saved - tap around to test", 1, LCD_MUT);
        if (testX >= 0 && testY >= 0) {   // where the last verification tap landed
            c.hline(testX - 8, testY, 17, LCD_OK);
            c.vline(testX, testY - 8, 17, LCD_OK);
            c.rect(testX - 4, testY - 4, 9, 9, LCD_FG);
        }
    } else {
        c.textCentered(LCD_H / 2 - 16, "Cal failed", 2, LCD_BAD);
        c.textCentered(LCD_H / 2 + 8, "taps too clustered - retrying", 1, LCD_MUT);
    }
}

}  // namespace sb20proxy
