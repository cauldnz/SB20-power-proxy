#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "LcdFont.h"

namespace sb20proxy {

// The pure 172x320 RGB565 canvas behind the S3-Touch head-unit UI (design/sb20-lcd-*.html —
// the locked 1:1 designs). Everything here runs on the HOST: the LcdUi screens draw into this
// buffer, host tests rasterize + assert on it (and dump BMPs I can look at before a board is
// ever flashed), and the seam (src/disp/LcdDisplay) only blasts the finished buffer over SPI.
// The same discipline as OledScreen.h, scaled up to colour.

constexpr int LCD_W = 172;
constexpr int LCD_H = 320;

// --- palette: the design tokens (design/sb20-lcd-*.html :root) as RGB565 -----------------
constexpr uint16_t lcdRgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
// Blend a foreground over a background at num/den opacity (the design's rgba() tints,
// precomputed — the canvas itself has no alpha).
constexpr uint8_t lcdMixCh(uint8_t f, uint8_t b, int num, int den) {
    return (uint8_t)((f * num + b * (den - num)) / den);
}
constexpr uint16_t lcdMix(uint8_t fr, uint8_t fg, uint8_t fb,
                          uint8_t br, uint8_t bg, uint8_t bb, int num, int den) {
    return lcdRgb(lcdMixCh(fr, br, num, den), lcdMixCh(fg, bg, num, den), lcdMixCh(fb, bb, num, den));
}

constexpr uint16_t LCD_BG     = lcdRgb(0x0f, 0x13, 0x20);  // --bg
constexpr uint16_t LCD_CARD   = lcdRgb(0x1a, 0x20, 0x30);  // --card
constexpr uint16_t LCD_FG     = lcdRgb(0xe8, 0xec, 0xf4);  // --fg
constexpr uint16_t LCD_MUT    = lcdRgb(0x8b, 0x93, 0xa7);  // --mut
constexpr uint16_t LCD_OK     = lcdRgb(0x22, 0xc5, 0x5e);  // --ok
constexpr uint16_t LCD_ACCENT = lcdRgb(0x3b, 0x82, 0xf6);  // --accent
constexpr uint16_t LCD_BAD    = lcdRgb(0xef, 0x44, 0x44);  // --bad
constexpr uint16_t LCD_LINE   = lcdRgb(0x1c, 0x23, 0x34);  // --line
constexpr uint16_t LCD_CHIP   = lcdRgb(0x2a, 0x31, 0x42);  // --chip2
constexpr uint16_t LCD_TITLE  = lcdRgb(0x15, 0x1d, 0x2e);  // title bar #151d2e
constexpr uint16_t LCD_WHITE  = lcdRgb(0xff, 0xff, 0xff);
// the design's static tints, pre-blended over their usual backgrounds
constexpr uint16_t LCD_OK_TINT     = lcdMix(0x22, 0xc5, 0x5e, 0x0f, 0x13, 0x20, 45, 100);  // done bars
constexpr uint16_t LCD_ACC_FILL    = lcdRgb(0x3b, 0x82, 0xf6);                              // current bar
constexpr uint16_t LCD_UP_TINT     = lcdMix(0x8b, 0x93, 0xa7, 0x0f, 0x13, 0x20, 32, 100);  // upcoming bars
constexpr uint16_t LCD_SPARK_FILL  = lcdMix(0x3b, 0x82, 0xf6, 0x0f, 0x13, 0x20, 16, 100);  // sparkline area
constexpr uint16_t LCD_BADGE_IN    = lcdMix(0x22, 0xc5, 0x5e, 0x1a, 0x20, 0x30, 16, 100);  // IN badge bg
constexpr uint16_t LCD_BADGE_OUT   = lcdMix(0x3b, 0x82, 0xf6, 0x1a, 0x20, 0x30, 18, 100);  // OUT badge bg

// --- the canvas ---------------------------------------------------------------------------
struct LcdCanvas {
    std::vector<uint16_t> px;
    LcdCanvas() : px((size_t)LCD_W * LCD_H, LCD_BG) {}

    void clear(uint16_t c = LCD_BG) { for (auto& p : px) p = c; }

    inline void set(int x, int y, uint16_t c) {
        if (x < 0 || y < 0 || x >= LCD_W || y >= LCD_H) return;
        px[(size_t)y * LCD_W + x] = c;
    }
    inline uint16_t get(int x, int y) const {
        if (x < 0 || y < 0 || x >= LCD_W || y >= LCD_H) return 0;
        return px[(size_t)y * LCD_W + x];
    }

    void fillRect(int x, int y, int w, int h, uint16_t c) {
        if (w <= 0 || h <= 0) return;
        int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
        int x1 = x + w > LCD_W ? LCD_W : x + w, y1 = y + h > LCD_H ? LCD_H : y + h;
        for (int yy = y0; yy < y1; ++yy) {
            uint16_t* row = &px[(size_t)yy * LCD_W];
            for (int xx = x0; xx < x1; ++xx) row[xx] = c;
        }
    }
    void hline(int x, int y, int w, uint16_t c) { fillRect(x, y, w, 1, c); }
    void vline(int x, int y, int h, uint16_t c) { fillRect(x, y, 1, h, c); }
    void rect(int x, int y, int w, int h, uint16_t c) {
        hline(x, y, w, c); hline(x, y + h - 1, w, c);
        vline(x, y, h, c); vline(x + w - 1, y, h, c);
    }
    // A "rounded" card: a filled rect with its 4 corner pixels knocked back to the page
    // background — reads as a soft corner at this pixel density without real arcs.
    void card(int x, int y, int w, int h, uint16_t c, uint16_t page = LCD_BG) {
        fillRect(x, y, w, h, c);
        set(x, y, page); set(x + w - 1, y, page);
        set(x, y + h - 1, page); set(x + w - 1, y + h - 1, page);
    }

    // Map a UTF-8 string to the display's ASCII: the design's typographic glyphs get sensible
    // stand-ins (x . - ') and anything else non-ASCII becomes '~'. Pure; both text() and
    // textWidth() run it so advance widths always match what is drawn.
    static std::string ascii(const std::string& s) {
        std::string o;
        o.reserve(s.size());
        for (size_t i = 0; i < s.size();) {
            unsigned char b = s[i];
            if (b < 0x80) { o += (char)b; i += 1; continue; }
            unsigned char b2 = (i + 1 < s.size()) ? s[i + 1] : 0;
            if (b == 0xC3 && b2 == 0x97) o += 'x';        // ×
            else if (b == 0xC2 && b2 == 0xB7) o += '.';   // ·
            else if (b == 0xE2) o += '-';                 // — – → …
            else o += '~';
            i += (b >= 0xF0) ? 4 : (b >= 0xE0) ? 3 : 2;   // skip the sequence
        }
        return o;
    }

    // --- text: the 8x8 font at integer scales (1x=8px chip text, 2x=16px body, 3x=24px
    // sub-hero, 6x=48px hero numerals). Returns the advance width.
    static int textWidth(const std::string& raw, int scale) {
        return (int)ascii(raw).size() * 8 * scale;
    }
    int text(int x, int y, const std::string& raw, int scale, uint16_t c) {
        const std::string s = ascii(raw);
        int cx = x;
        for (unsigned char ch : s) {
            if (ch < LCD_FONT_FIRST || ch > LCD_FONT_LAST) ch = '?';
            const uint8_t* g = LCD_FONT8X8[ch - LCD_FONT_FIRST];
            for (int gy = 0; gy < 8; ++gy) {
                uint8_t bits = g[gy];
                if (!bits) continue;
                for (int gx = 0; gx < 8; ++gx) {
                    if (!(bits & (1u << gx))) continue;
                    if (scale == 1) set(cx + gx, y + gy, c);
                    else fillRect(cx + gx * scale, y + gy * scale, scale, scale, c);
                }
            }
            cx += 8 * scale;
        }
        return cx - x;
    }
    void textCentered(int y, const std::string& s, int scale, uint16_t c) {
        text((LCD_W - textWidth(s, scale)) / 2, y, s, scale, c);
    }
    void textRight(int xRight, int y, const std::string& s, int scale, uint16_t c) {
        text(xRight - textWidth(s, scale), y, s, scale, c);
    }

    // --- widgets ---------------------------------------------------------------------
    // Filled sparkline (the ride power history): values scaled into [x,y,w,h].
    void sparkline(int x, int y, int w, int h, const int16_t* v, int n, int16_t vmax,
                   uint16_t line, uint16_t fill) {
        if (n < 2 || w < 2 || vmax <= 0) return;
        int prevY = -1;
        for (int i = 0; i < w; ++i) {
            int idx = (int)((int64_t)i * (n - 1) / (w - 1));
            int16_t val = v[idx];
            if (val < 0) val = 0;
            if (val > vmax) val = vmax;
            int barH = (int)((int32_t)val * (h - 2) / vmax);
            int topY = y + h - 1 - barH;
            fillRect(x + i, topY, 1, barH + 1, fill);
            if (prevY >= 0) {  // connect the line vertically for steep steps
                int a = prevY < topY ? prevY : topY, b = prevY < topY ? topY : prevY;
                fillRect(x + i, a, 1, b - a + 1, line);
            }
            set(x + i, topY, line);
            prevY = topY;
        }
    }

    // Horizontal progress bar with a filled fraction (num/den).
    void bar(int x, int y, int w, int h, int num, int den, uint16_t fill, uint16_t track) {
        fillRect(x, y, w, h, track);
        if (den > 0 && num > 0) {
            int fw = (int)((int64_t)w * (num > den ? den : num) / den);
            fillRect(x, y, fw, h, fill);
        }
    }
};

// --- BMP encode (24bpp, bottom-up) — pure; shared by the host test dumps and the device's
// GET /screen.bmp debug route (how I *see* the panel remotely). ~162 KB for 172x320.
inline std::vector<uint8_t> lcdCanvasToBmp(const LcdCanvas& c) {
    const int rowBytes = ((LCD_W * 3 + 3) / 4) * 4;  // 4-byte aligned rows
    const uint32_t dataSize = (uint32_t)rowBytes * LCD_H;
    const uint32_t fileSize = 54 + dataSize;
    std::vector<uint8_t> out(fileSize, 0);
    uint8_t* p = out.data();
    auto w32 = [&](size_t off, uint32_t v) {
        p[off] = v & 0xFF; p[off + 1] = (v >> 8) & 0xFF;
        p[off + 2] = (v >> 16) & 0xFF; p[off + 3] = (v >> 24) & 0xFF;
    };
    p[0] = 'B'; p[1] = 'M';
    w32(2, fileSize); w32(10, 54);           // pixel data offset
    w32(14, 40);                              // BITMAPINFOHEADER
    w32(18, (uint32_t)LCD_W); w32(22, (uint32_t)LCD_H);
    p[26] = 1;                                // planes
    p[28] = 24;                               // bpp
    w32(34, dataSize);
    for (int y = 0; y < LCD_H; ++y) {
        uint8_t* row = p + 54 + (size_t)(LCD_H - 1 - y) * rowBytes;  // bottom-up
        for (int x = 0; x < LCD_W; ++x) {
            uint16_t v = c.px[(size_t)y * LCD_W + x];
            uint8_t r = (uint8_t)(((v >> 11) & 0x1F) << 3);
            uint8_t g = (uint8_t)(((v >> 5) & 0x3F) << 2);
            uint8_t b = (uint8_t)((v & 0x1F) << 3);
            row[x * 3 + 0] = b; row[x * 3 + 1] = g; row[x * 3 + 2] = r;  // BGR
        }
    }
    return out;
}

}  // namespace sb20proxy
