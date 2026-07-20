#pragma once
// WattyBirdRender — draws a WattyBird game state into an LcdCanvas (the pure RGB565 framebuffer that
// blits to the CYD through the same flushArea seam LVGL uses). No Arduino, no game logic — pixels
// only, so it renders identically on the host (screenshot/montage tests) and on the device.
#include <string>

#include "LcdCanvas.h"
#include "WattyBird.h"

namespace sb20proxy {

// --- palette (RGB565) -------------------------------------------------------------------------
constexpr uint16_t WB_SKY    = lcdRgb(0x0c, 0x12, 0x22);  // night-ride sky (a touch off --bg)
constexpr uint16_t WB_STAR   = lcdRgb(0x27, 0x31, 0x4a);
constexpr uint16_t WB_PIPE   = lcdRgb(0x3f, 0xb9, 0x5e);  // pipe body (green)
constexpr uint16_t WB_PIPE_D = lcdRgb(0x2c, 0x8f, 0x46);  // pipe shade
constexpr uint16_t WB_PIPE_L = lcdRgb(0x6a, 0xe0, 0x8a);  // pipe lip highlight
constexpr uint16_t WB_BIRD   = lcdRgb(0xff, 0xd1, 0x3b);  // watt-yellow body
constexpr uint16_t WB_BIRD_D = lcdRgb(0xe0, 0xa8, 0x12);  // body shade
constexpr uint16_t WB_BEAK   = lcdRgb(0xf9, 0x73, 0x16);  // orange beak
constexpr uint16_t WB_FLAME  = lcdRgb(0xff, 0x8a, 0x1f);  // thrust flame
constexpr uint16_t WB_GROUND = lcdRgb(0x22, 0x2a, 0x3c);
constexpr uint16_t WB_GRND_L = lcdRgb(0x33, 0x3e, 0x57);

// Build a game config sized to THIS panel (LCD_W x LCD_H) — 240x320 CYD, 172x320 S3-Touch, etc.
// Keeps the pure WattyBird core geometry-agnostic while the device fits it to the real screen.
inline WattyBirdConfig wattyBirdPanelConfig() {
    WattyBirdConfig cf;
    cf.worldW = LCD_W;
    cf.worldH = LCD_H;
    cf.birdX = LCD_W * 27 / 100;                 // ~27% from the left edge
    cf.pipeSpacing = LCD_W < 200 ? 150 : 168;    // a touch tighter on narrow panels
    return cf;
}

// A cheap solid disc (LcdCanvas has no arc primitive).
inline void wbFillCircle(LcdCanvas& c, int cx, int cy, int r, uint16_t col) {
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx)
            if (dx * dx + dy * dy <= r * r) c.set(cx + dx, cy + dy, col);
}

// A dim translucent-looking panel (no real alpha) to seat overlay text on.
inline void wbPanel(LcdCanvas& c, int x, int y, int w, int h) {
    c.card(x, y, w, h, lcdMix(0xe8, 0xec, 0xf4, 0x0c, 0x12, 0x22, 12, 100));
    c.rect(x, y, w, h, LCD_LINE);
}

inline void renderWattyBird(LcdCanvas& c, const WattyBird& g) {
    const WattyBirdConfig& cf = g.cfg();
    const int W = cf.worldW, H = cf.worldH, groundY = g.groundY();

    // sky + a few deterministic parallax stars (drift with elapsed time)
    c.clear(WB_SKY);
    const int drift = (int)((g.elapsedMs() / 40) % (uint32_t)W);
    for (int i = 0; i < 18; ++i) {
        int sx = ((i * 61 + 13) - drift / (2 + (i & 3)));
        sx = ((sx % W) + W) % W;
        int sy = (i * 47 + 9) % (groundY - 20) + 6;
        c.set(sx, sy, WB_STAR);
        if (i & 1) c.set(sx + 1, sy, WB_STAR);
    }

    // pipes
    for (const auto& p : g.pipes()) {
        int px = (int)(p.x + 0.5f);
        int gapTop = p.gapCenterY - cf.gapH / 2;
        int gapBot = p.gapCenterY + cf.gapH / 2;
        // top pipe (ceiling -> gapTop) and bottom pipe (gapBot -> ground)
        c.fillRect(px, 0, cf.pipeW, gapTop, WB_PIPE);
        c.fillRect(px, gapBot, cf.pipeW, groundY - gapBot, WB_PIPE);
        // shading down the right third + highlight on the left edge
        c.fillRect(px + cf.pipeW - 6, 0, 6, gapTop, WB_PIPE_D);
        c.fillRect(px + cf.pipeW - 6, gapBot, 6, groundY - gapBot, WB_PIPE_D);
        c.vline(px + 1, 0, gapTop, WB_PIPE_L);
        c.vline(px + 1, gapBot, groundY - gapBot, WB_PIPE_L);
        // lips at the gap edges (a slightly wider band)
        c.fillRect(px - 3, gapTop - 8, cf.pipeW + 6, 8, WB_PIPE);
        c.hline(px - 3, gapTop - 8, cf.pipeW + 6, WB_PIPE_L);
        c.fillRect(px - 3, gapBot, cf.pipeW + 6, 8, WB_PIPE);
        c.hline(px - 3, gapBot, cf.pipeW + 6, WB_PIPE_L);
    }

    // ground
    c.fillRect(0, groundY, W, H - groundY, WB_GROUND);
    c.hline(0, groundY, W, WB_GRND_L);
    for (int x = -(drift % 16); x < W; x += 16) c.vline(x + 8, groundY + 4, 4, WB_GRND_L);

    // bird
    const int bx = cf.birdX, by = (int)(g.birdY() + 0.5f), r = cf.birdR;
    if (g.thrusting()) {  // little thrust flame under the tail when climbing
        c.fillRect(bx - r - 4, by - 2, 5, 4, WB_FLAME);
        c.fillRect(bx - r - 7, by - 1, 3, 2, WB_BIRD);
    }
    wbFillCircle(c, bx, by, r, WB_BIRD);
    c.fillRect(bx - r, by + 1, r, r - 1, WB_BIRD_D);   // soft under-shade
    wbFillCircle(c, bx, by, r, WB_BIRD);               // re-lay the top so the disc stays round
    c.fillRect(bx - r, by + r - 2, 2 * r, 2, WB_BIRD_D);
    c.fillRect(bx + r - 1, by - 2, 5, 4, WB_BEAK);     // beak (points right, direction of travel)
    c.fillRect(bx + 1, by - 4, 3, 3, LCD_WHITE);       // eye white
    c.fillRect(bx + 2, by - 3, 2, 2, lcdRgb(0, 0, 0)); // pupil
    c.fillRect(bx - 5, by - 1, 5, 3, WB_BIRD_D);       // wing

    // --- HUD -----------------------------------------------------------------------------------
    // score, big + centered, with a drop shadow
    if (g.mode() != WbMode::Ready) {
        std::string sc = std::to_string(g.score());
        int scale = 4;
        int tx = (W - LcdCanvas::textWidth(sc, scale)) / 2;
        c.text(tx + 2, 10, sc, scale, lcdRgb(0, 0, 0));
        c.text(tx, 8, sc, scale, LCD_WHITE);
    }
    // vertical power gauge on the right: fill ~ watts (0..2*hover), marker at hover
    {
        const int gx = W - 9, gw = 6, gy = 40, gh = groundY - 60;
        c.fillRect(gx, gy, gw, gh, LCD_CHIP);
        int hover = g.hoverWatts();
        int gaugeMax = hover > 0 ? hover * 2 : 300;
        int wv = g.watts(); if (wv > gaugeMax) wv = gaugeMax;
        int fh = (int)((int64_t)wv * gh / gaugeMax);
        uint16_t fill = g.thrusting() ? LCD_OK : LCD_ACCENT;
        c.fillRect(gx, gy + gh - fh, gw, fh, fill);
        int my = gy + gh - (int)((int64_t)hover * gh / gaugeMax);   // hover marker
        c.hline(gx - 2, my, gw + 4, LCD_WHITE);
        c.text(gx - 2 + gw - LcdCanvas::textWidth("W", 1), gy - 10, "W", 1, LCD_MUT);
    }
    // watts + cadence readout, bottom-left over the ground
    {
        std::string w = std::to_string(g.watts()) + "W";
        if (g.cadence() >= 0) w += "  " + std::to_string(g.cadence()) + "rpm";
        c.text(6, groundY + 8, w, 1, LCD_FG);
    }

    // --- overlays ------------------------------------------------------------------------------
    if (g.mode() == WbMode::Ready) {
        wbPanel(c, 20, 96, W - 40, 118);
        c.textCentered(112, "WATTY BIRD", 2, WB_BIRD);
        c.textCentered(140, "PEDAL TO FLY", 1, LCD_FG);
        c.textCentered(158, "hold ~" + std::to_string(g.hoverWatts()) + "W to hover", 1, LCD_MUT);
        c.textCentered(172, "push to climb - ease to drop", 1, LCD_MUT);
        if (g.best() > 0) c.textCentered(192, "best " + std::to_string(g.best()), 1, LCD_OK);
    } else if (g.mode() == WbMode::Dead) {
        wbPanel(c, 20, 104, W - 40, 96);
        c.textCentered(120, "CRASHED", 2, LCD_BAD);
        c.textCentered(146, "score " + std::to_string(g.score()), 1, LCD_FG);
        c.textCentered(162, "best " + std::to_string(g.best()), 1, LCD_OK);
        c.textCentered(180, "surge to retry", 1, LCD_MUT);
    }
}

}  // namespace sb20proxy
