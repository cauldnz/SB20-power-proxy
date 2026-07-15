#pragma once
// MeterCompareRender — draws a live A/B meter-compare view into an LcdCanvas (the head-unit's
// "Compare" screen). Pure: no game/BLE logic — it renders a MeterCompareStats + per-band table, so
// it host-tests + montages like WattyBirdRender, and blits to the CYD/S3 through the same seam.
#include <cstdio>
#include <string>
#include <vector>

#include "LcdCanvas.h"
#include "MeterCompare.h"

namespace sb20proxy {

constexpr uint16_t MCMP_A   = lcdRgb(0x3b, 0x82, 0xf6);  // meter A tint (blue/accent)
constexpr uint16_t MCMP_B   = lcdRgb(0xf5, 0xa5, 0x24);  // meter B tint (amber)
constexpr uint16_t MCMP_HI  = lcdRgb(0xef, 0x44, 0x44);  // B reads HIGH (red)
constexpr uint16_t MCMP_LO  = lcdRgb(0x3b, 0x82, 0xf6);  // B reads LOW (blue)
constexpr uint16_t MCMP_OK  = lcdRgb(0x22, 0xc5, 0x5e);  // agreement (green)

inline std::string mcmpPct(float v) {  // "+11.0%" / "-4.2%"
    char b[16];
    std::snprintf(b, sizeof(b), "%+.1f%%", (double)v);
    return b;
}

inline std::string mcmpFit(const std::string& s, int maxChars) {  // trim/ellipsize a meter name
    if (maxChars < 1) return "";
    if ((int)s.size() <= maxChars) return s;
    return s.substr(0, maxChars - 1) + "~";
}

// aName/bName label the two meters; s + bands come from MeterCompare.
inline void renderMeterCompare(LcdCanvas& c, const std::string& aName, const std::string& bName,
                               const MeterCompareStats& s, const std::vector<MeterBand>& bands) {
    using sb20proxy::LCD_W;
    const int W = LCD_W;
    const int PAD = 6;
    c.clear(LCD_BG);

    // title
    c.fillRect(0, 0, W, 24, LCD_TITLE);
    c.hline(0, 23, W, LCD_LINE);
    c.textCentered(8, "METER COMPARE", 1, LCD_FG);

    if (!s.valid) {
        c.textCentered(90, "waiting for", 1, LCD_MUT);
        c.textCentered(104, "both meters...", 1, LCD_MUT);
        return;
    }

    // two meter cards side by side: name + big watts
    const int cw = (W - PAD * 3) / 2;
    const int cy = 30, ch = 74;
    auto meterCard = [&](int x, const std::string& name, int watts, uint16_t tint) {
        c.card(x, cy, cw, ch, LCD_CARD);
        c.fillRect(x, cy, cw, 3, tint);                         // colour strip
        c.text(x + 6, cy + 8, mcmpFit(name, (cw - 12) / 8), 1, LCD_MUT);
        std::string w = std::to_string(watts);
        int sc = w.size() <= 3 ? 4 : 3;
        c.text(x + 6, cy + 26, w, sc, LCD_FG);
        c.text(x + 6 + LcdCanvas::textWidth(w, sc) + 3, cy + 26 + sc * 8 - 12, "W", 1, LCD_MUT);
    };
    meterCard(PAD, aName, s.aWatts, MCMP_A);
    meterCard(PAD * 2 + cw, bName, s.bWatts, MCMP_B);

    // verdict row: delta + bias% + ratio
    const bool agree = (s.meanBiasPct > -2.0f && s.meanBiasPct < 2.0f);
    const uint16_t vc = agree ? MCMP_OK : (s.meanBiasPct > 0 ? MCMP_HI : MCMP_LO);
    int vy = cy + ch + 10;
    char delta[16];
    std::snprintf(delta, sizeof(delta), "%+dW", s.deltaW);
    c.text(PAD, vy, delta, 2, LCD_FG);
    char ratio[16];
    std::snprintf(ratio, sizeof(ratio), "x%.3f", (double)s.meanRatio);
    c.textRight(W - PAD, vy, ratio, 2, vc);

    // agreement banner
    int by = vy + 22;
    std::string verdict;
    if (agree) verdict = "AGREE - within 2%";
    else verdict = bName + (s.meanBiasPct > 0 ? " reads " : " reads ") +
                   mcmpPct(s.meanBiasPct) + (s.meanBiasPct > 0 ? " HIGH" : " LOW");
    c.card(PAD, by, W - PAD * 2, 20, lcdMix(agree ? 0x22 : 0xef, agree ? 0xc5 : 0x44,
                                            agree ? 0x5e : 0x44, 0x0f, 0x13, 0x20, 22, 100));
    c.textCentered(by + 6, verdict, 1, vc);

    // per-band divergence chart: a column per non-empty band, from a 0% mid-line (up=HIGH, down=LOW)
    int chartY = by + 30, chartH = 96;
    int midY = chartY + chartH / 2;
    c.text(PAD, chartY - 10, "bias by power band", 1, LCD_MUT);
    c.hline(PAD, midY, W - PAD * 2, LCD_LINE);                  // 0% line
    // count populated bands to lay columns edge to edge
    int n = 0;
    for (const auto& b : bands) if (b.nPairs > 0) ++n;
    if (n > 0) {
        const int usable = W - PAD * 2;
        const int colW = usable / n;
        const float capPct = 20.0f;                            // clamp bar height to +/-20%
        int col = 0;
        for (const auto& b : bands) {
            if (b.nPairs == 0) continue;
            int x = PAD + col * colW;
            float bp = b.meanBiasPct;
            if (bp > capPct) bp = capPct;
            if (bp < -capPct) bp = -capPct;
            int h = (int)(bp / capPct * (chartH / 2 - 2));
            uint16_t bc = (b.meanBiasPct > 1.0f) ? MCMP_HI
                          : (b.meanBiasPct < -1.0f) ? MCMP_LO : MCMP_OK;
            if (h >= 0) c.fillRect(x + 1, midY - h, colW - 2, h + 1, bc);
            else c.fillRect(x + 1, midY, colW - 2, -h + 1, bc);
            // band label (the low edge, e.g. "100") every other column to avoid crowding
            if (col % 2 == 0) {
                std::string lab = std::to_string(b.loW);
                c.text(x + (colW - LcdCanvas::textWidth(lab, 1)) / 2, chartY + chartH + 2, lab, 1,
                       LCD_MUT);
            }
            ++col;
        }
    } else {
        c.textCentered(midY - 4, "pedal to fill bands", 1, LCD_MUT);
    }

    // footer
    char foot[24];
    std::snprintf(foot, sizeof(foot), "n=%d pairs", s.nPairs);
    c.textCentered(chartY + chartH + 14, foot, 1, LCD_MUT);
}

}  // namespace sb20proxy
