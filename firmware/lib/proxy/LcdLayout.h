#pragma once
#include <algorithm>

namespace sb20proxy {

// Vertical layout for the LVGL head-unit screens, derived from the panel size and the row count
// instead of baked into absolute offsets.
//
// Why: the offsets were chosen on a 240x320 panel and hard-coded, which fails in BOTH directions as
// soon as a second geometry exists (#358, #364, measured 2026-09-25).
//   * Guition, 480 tall: Ride's cards ended at y=272 with the nav at 450 — ~40% of the panel dead.
//   * CYD, 320 tall and NINE More rows (Touch cal is resistive-only): the ninth row ran to y=295
//     while the IP footer sat at ~280 — the two were drawn on top of each other.
// Same assumption, opposite symptoms. Adjusting the constants fixes one board and breaks the other;
// a 4.3" Guition is already on the roadmap as a third geometry.
//
// Pure, so the invariants below are host-tested (test_proxy) rather than eyeballed on a panel, and
// code/scripts/bench_ui.py mirrors moreLayout so the bench walker taps where the firmware draws.

constexpr int kNavH = 30;       // the bottom nav bar (mkNav)

struct RideLayout {
    int powerY;     // the "P O W E R" caption
    int heroY;      // the big watts number
    int chartY;
    int chartH;
    int cardsY;     // Cadence / Balance cards
    int cardsH;
    int detailsY;   // the IN/OUT pop-down, which replaces chart + cards
    int detailsH;
};

// Anchor the cards just above the nav and let the chart take the slack. The hero and the captions are
// font-sized, so growing them is not an option; the chart is the one element whose height is free,
// and a taller trace is easier to read at a glance from the saddle. (#358 notes the eventual use for
// this space is a live workout strip, #351 - when that lands it takes the slack instead.)
inline RideLayout rideLayout(int h) {
    RideLayout L{};
    L.powerY = 46;
    L.heroY = 60;
    L.chartY = 140;
    L.cardsH = 64;

    const int gap = 8;
    L.cardsY = h - kNavH - gap - L.cardsH;         // bottom-anchored: no dead band under the cards
    L.cardsY = std::max(L.cardsY, L.chartY + 40);  // never collide with the chart on a short panel

    L.chartH = std::max(40, L.cardsY - L.chartY - 12);
    L.detailsY = L.chartY - 2;
    L.detailsH = (L.cardsY + L.cardsH) - L.detailsY;   // covers exactly what it replaces
    return L;
}

struct MoreLayout {
    int rowTop;     // y of the first row
    int pitch;      // row-to-row spacing
    int rowH;
    int ipFromBottom;   // the IP caption's offset above the panel bottom
};

// Fit `rows` between the header and the footer (IP caption + nav), sizing the pitch to the panel:
// a short panel with many rows tightens it, a tall panel spends the room on bigger touch targets
// instead of stranding it (the two halves of #364 and #358). A settings list stays top-aligned -
// leftover space at the bottom of a short list is a list, not a bug.
inline MoreLayout moreLayout(int h, int rows) {
    MoreLayout L{};
    L.rowTop = 36;
    L.ipFromBottom = kNavH + 4;                   // the caption sits just above the nav

    const int ipH = 16;
    const int footerGap = 2;   // clear air below the last row: 9 rows otherwise land 2 px off the IP
    const int footerTop = h - L.ipFromBottom - ipH;   // rows must end above this
    const int avail = footerTop - L.rowTop - footerGap;
    const int n = std::max(1, rows);

    // What the panel can afford: 29 px is the pitch the 240x320 design was drawn at and the floor
    // for a comfortable tap; h/13 scales it with the panel (320 -> 24, so 29 wins; 480 -> 36) and 40
    // caps it so a very tall panel gets a list, not a row of buttons.
    const int roomy = std::min(40, std::max(29, h / 13));
    L.pitch = std::min(roomy, avail / n);         // ...but never more than the rows can have
    L.pitch = std::max(L.pitch, 18);              // below this the 16 px label is unreadable
    L.rowH = std::max(14, L.pitch - 2);
    return L;
}

// The invariant both issues violated: nothing the layout places may reach the footer.
inline bool moreRowsFit(int h, int rows) {
    const MoreLayout L = moreLayout(h, rows);
    const int lastBottom = L.rowTop + (std::max(1, rows) - 1) * L.pitch + L.rowH;
    return lastBottom <= h - L.ipFromBottom - 16;
}

}  // namespace sb20proxy
