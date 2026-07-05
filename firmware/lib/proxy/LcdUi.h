#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "CalibrationPage.h"  // CalWizardView (the wizard's shared view model)
#include "LcdCanvas.h"
#include "SourceCandidate.h"
#include "WorkoutEngine.h"
#include "WorkoutPresets.h"

namespace sb20proxy {

// The S3-Touch head-unit UI — the locked 172x320 designs (design/sb20-lcd-*.html) as PURE
// code: screens render into an LcdCanvas from small view structs, and taps hit-test into
// typed UiActions that main.cpp executes. No Arduino anywhere — the whole UI runs and is
// screenshot-tested on the host; the seam (src/disp/LcdDisplay) only pushes pixels + feeds
// touches. Nav mirrors the web UI: three tabs (Ride / Setup / More); Workout + Calibrate
// open from More (and from the Ride screen's live workout strip).

// ---------- state + actions ----------------------------------------------------------------
enum class LcdScreen : uint8_t { Ride, Setup, More, Workout, Calibrate };

struct LcdUiState {
    LcdScreen screen = LcdScreen::Ride;
    bool rideDetails = false;   // Ride title tapped -> IN/OUT detail cards
    uint8_t brightness = 100;   // % — the More screen's brightness row cycles it
};

struct UiAction {
    enum Type : uint8_t {
        None,
        WorkoutPreset,   // index = preset index (workoutPresets()[index])
        WorkoutStart, WorkoutPause, WorkoutResume, WorkoutSkip, WorkoutStop,
        WorkoutUnload,   // drop the loaded workout -> back to the preset picker
        SetupScan,
        SetupPick,       // index = device index (CPS -> meter, FTMS -> trainer)
        SetupSave,
        CalPickDut,      // index = device index
        CalPickRef,      // index = device index
        CalStart, CalFinish, CalSave, CalCancel,
        SetBrightness,   // index = new %
        TouchCalStart,   // run the resistive touch-cal ritual (CYD builds)
    };
    Type type = None;
    int index = -1;
};

// ---------- view models (filled by main.cpp each frame) ------------------------------------
struct RideView {
    std::string srcName;      // "ASSIOMA17039L" or "searching…"
    std::string outName;      // the identity we advertise ("Stages 62144")
    bool srcOn = false;
    bool outOn = true;
    int16_t watts = 0;        // broadcast power (the hero)
    int16_t srcWatts = 0;     // received power (details)
    int16_t cadence = -1;
    int16_t balancePct = -1;  // left %
    const int16_t* hist = nullptr;  // power history ring, oldest first
    int nHist = 0;
    int16_t histMax = 300;
    // details panel extras
    int32_t wifiRssi = 0;
    uint32_t uptimeMs = 0;
    uint32_t freeHeap = 0;
    std::string version;
    // live workout strip
    bool wkRunning = false;
    bool wkPaused = false;
    int16_t wkTarget = -1;
    long wkRemainS = 0;
};

struct WorkoutView {
    bool loaded = false;
    bool running = false;
    bool paused = false;
    const Workout* w = nullptr;  // profile + name (null when !loaded)
    WkState st;
    int16_t nowW = 0;
    int16_t nowCad = -1;
    // erg (the FTMS trainer link)
    bool ergConfigured = false;
    bool ergConnected = false;
    bool ergControlled = false;
    int16_t ergTarget = -1;
};

struct SetupView {
    std::vector<SourceCandidate> devices;  // isCps -> meter candidates; isFtms -> trainer
    std::string meterAddr;                 // currently selected/pinned
    std::string trainerAddr;
    bool scanning = false;
    bool dirty = false;                    // a pick was made -> show Save
};

struct MoreView {
    std::string mode;      // "Crank spoof" / "Corrector"
    std::string identity;
    std::string source;
    std::string trainer;   // trainer name/addr or "—"
    std::string ip;        // "192.168.1.42" or "no wifi"
    std::string version;
    uint8_t brightness = 100;
};

// WiFi onboarding (the captive setup portal): when active, the LCD boards replace the normal UI
// with a join-this-AP screen (QR + SSID/PIN); the C3's OLED shows the same facts as text.
struct ProvisionView {
    bool portal = false;
    std::string apSsid;   // "SB20-Setup"
    std::string pin;      // the AP's WPA2 password (per-device PIN on OLED builds)
    std::string url;      // where the portal lives (Config::SETUP_PORTAL_URL)
};

struct LcdViews {
    RideView ride;
    WorkoutView wk;
    SetupView setup;
    MoreView more;
    CalWizardView cal;
    ProvisionView prov;
};

// ---------- shared layout ----------------------------------------------------------------
namespace lcdlay {
constexpr int NAV_H = 26;
constexpr int NAV_Y = LCD_H - NAV_H;       // 294
constexpr int TITLE_H = 28;
constexpr int PAD = 6;
// workout console buttons
constexpr int WK_BTN_Y = 246;
constexpr int WK_BTN_H = 34;
// setup rows
constexpr int SETUP_ROW0 = 62;
constexpr int SETUP_ROW_H = 42;
constexpr int SETUP_MAX_ROWS = 4;
constexpr int SETUP_BTN_Y = 254;
// more rows
constexpr int MORE_ROW0 = 34;
constexpr int MORE_ROW_H = 30;
// workout picker rows
constexpr int PICK_ROW0 = 56;
constexpr int PICK_ROW_H = 40;
// calibrate
constexpr int CAL_ROW0 = 56;
constexpr int CAL_ROW_H = 40;
constexpr int CAL_MAX_ROWS = 3;
constexpr int CAL_BTN_Y = 254;
}  // namespace lcdlay

// ---------- small format helpers (pure) ----------------------------------------------------
inline std::string lcdClock(long s) {
    if (s < 0) s = 0;
    char b[16];
    if (s >= 3600) std::snprintf(b, sizeof(b), "%ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
    else std::snprintf(b, sizeof(b), "%ld:%02ld", s / 60, s % 60);
    return b;
}
inline std::string lcdNum(int v) { char b[12]; std::snprintf(b, sizeof(b), "%d", v); return b; }

// Trim/ellipsize a name to fit `maxChars` (scale-1 chars).
inline std::string lcdFit(const std::string& s, size_t maxChars) {
    if (s.size() <= maxChars) return s;
    return s.substr(0, maxChars > 1 ? maxChars - 1 : 0) + "~";
}

// ---------- chrome -------------------------------------------------------------------------
inline void lcdDrawNav(LcdCanvas& c, LcdScreen screen) {
    using namespace lcdlay;
    c.fillRect(0, NAV_Y, LCD_W, NAV_H, lcdRgb(0x10, 0x14, 0x1f));
    c.hline(0, NAV_Y, LCD_W, LCD_LINE);
    const char* labels[3] = {"Ride", "Setup", "More"};
    const bool active[3] = {
        screen == LcdScreen::Ride,
        screen == LcdScreen::Setup,
        screen == LcdScreen::More || screen == LcdScreen::Workout || screen == LcdScreen::Calibrate,
    };
    for (int i = 0; i < 3; ++i) {
        int cx = i * (LCD_W / 3) + (LCD_W / 3 - LcdCanvas::textWidth(labels[i], 1)) / 2;
        c.text(cx, NAV_Y + 9, labels[i], 1, active[i] ? LCD_ACCENT : LCD_MUT);
    }
}

inline void lcdDrawTitle(LcdCanvas& c, const std::string& left, const std::string& right,
                         uint16_t rightColor = LCD_MUT) {
    using namespace lcdlay;
    c.fillRect(0, 0, LCD_W, TITLE_H, LCD_TITLE);
    c.hline(0, TITLE_H - 1, LCD_W, LCD_LINE);
    c.text(PAD, 10, lcdFit(left, 14), 1, LCD_FG);
    if (!right.empty()) c.textRight(LCD_W - PAD, 10, lcdFit(right, 7), 1, rightColor);
}

// ---------- Ride ---------------------------------------------------------------------------
inline void lcdRenderRide(LcdCanvas& c, const RideView& v, const LcdUiState& st) {
    using namespace lcdlay;
    c.clear();

    // Title bar: ● src → ● out (tap = toggle details)
    c.fillRect(0, 0, LCD_W, TITLE_H, LCD_TITLE);
    c.hline(0, TITLE_H - 1, LCD_W, LCD_LINE);
    int x = PAD;
    c.fillRect(x, 12, 5, 5, v.srcOn ? LCD_OK : LCD_BAD);
    x += 9;
    x += c.text(x, 10, lcdFit(v.srcName, 8), 1, LCD_FG);
    x += 3;
    x += c.text(x, 10, ">", 1, LCD_ACCENT);
    x += 4;
    c.fillRect(x, 12, 5, 5, v.outOn ? LCD_OK : LCD_BAD);
    x += 9;
    c.text(x, 10, lcdFit(v.outName, 8), 1, LCD_FG);
    c.textRight(LCD_W - PAD, 10, st.rideDetails ? "^" : "v", 1, LCD_MUT);

    int y = TITLE_H;
    if (st.rideDetails) {
        // IN / OUT cards side by side
        const int cw = (LCD_W - PAD * 2 - 6) / 2;  // 77
        const int cy = y + 6, ch = 96;
        const int inX = PAD, outX = PAD + cw + 6;
        c.card(inX, cy, cw, ch, LCD_CARD);
        c.card(outX, cy, cw, ch, LCD_CARD);
        // badges
        c.fillRect(inX + 6, cy + 6, 20, 12, LCD_BADGE_IN);
        c.text(inX + 9, cy + 8, "IN", 1, LCD_OK);
        c.fillRect(outX + 6, cy + 6, 28, 12, LCD_BADGE_OUT);
        c.text(outX + 9, cy + 8, "OUT", 1, LCD_ACCENT);
        // names (2 lines max, 9 chars per line at scale 1)
        c.text(inX + 6, cy + 24, lcdFit(v.srcName, 9), 1, LCD_FG);
        c.text(outX + 6, cy + 24, lcdFit(v.outName, 9), 1, LCD_FG);
        // watts
        c.text(inX + 6, cy + 40, lcdNum(v.srcWatts), 2, LCD_FG);
        c.text(outX + 6, cy + 40, lcdNum(v.watts), 2, LCD_FG);
        c.text(inX + 6 + LcdCanvas::textWidth(lcdNum(v.srcWatts), 2) + 3, cy + 48, "W", 1, LCD_MUT);
        c.text(outX + 6 + LcdCanvas::textWidth(lcdNum(v.watts), 2) + 3, cy + 48, "W", 1, LCD_MUT);
        // cadence
        std::string cad = (v.cadence < 0 ? std::string("--") : lcdNum(v.cadence)) + " rpm";
        c.text(inX + 6, cy + 62, cad, 1, LCD_MUT);
        c.text(outX + 6, cy + 62, cad, 1, LCD_MUT);
        // rule + device line
        c.hline(inX + 6, cy + 76, cw - 12, LCD_LINE);
        c.hline(outX + 6, cy + 76, cw - 12, LCD_LINE);
        char rssi[16];
        std::snprintf(rssi, sizeof(rssi), "%lddBm", (long)v.wifiRssi);
        c.text(inX + 6, cy + 82, rssi, 1, LCD_MUT);
        char up[16];
        std::snprintf(up, sizeof(up), "%lum", (unsigned long)(v.uptimeMs / 60000u));
        c.text(outX + 6, cy + 82, up, 1, LCD_MUT);
        y = cy + ch + 2;
    }

    // POWER hero
    c.textCentered(y + 10, "P O W E R", 1, LCD_MUT);
    {
        std::string w = lcdNum(v.watts);
        int scale = (w.size() <= 3) ? 6 : 5;  // 3 digits at 48px, 4 digits at 40px
        int tw = LcdCanvas::textWidth(w, scale) + 3 + LcdCanvas::textWidth("W", 2);
        int wx = (LCD_W - tw) / 2;
        int wy = y + 24;
        c.text(wx, wy, w, scale, LCD_WHITE);
        c.text(wx + LcdCanvas::textWidth(w, scale) + 3, wy + scale * 8 - 16, "W", 2, LCD_MUT);
    }
    y += 24 + 52;

    // sparkline
    if (!st.rideDetails) {
        c.sparkline(PAD, y, LCD_W - PAD * 2, 42, v.hist, v.nHist, v.histMax, LCD_ACCENT, LCD_SPARK_FILL);
        c.hline(PAD, y + 42, LCD_W - PAD * 2, LCD_LINE);
        y += 50;
    }

    // Cadence | Balance chips
    {
        const int cw = (LCD_W - PAD * 2 - 6) / 2;
        const int ch = 44;
        c.card(PAD, y, cw, ch, LCD_CARD);
        c.card(PAD + cw + 6, y, cw, ch, LCD_CARD);
        c.text(PAD + 6, y + 6, "Cadence", 1, LCD_MUT);
        std::string cad = v.cadence < 0 ? "--" : lcdNum(v.cadence);
        c.text(PAD + 6, y + 19, cad, 2, LCD_FG);
        c.text(PAD + 6 + LcdCanvas::textWidth(cad, 2) + 4, y + 27, "rpm", 1, LCD_MUT);
        c.text(PAD + cw + 12, y + 6, "Balance", 1, LCD_MUT);
        std::string bal = v.balancePct < 0
                              ? "--"
                              : lcdNum(v.balancePct) + "/" + lcdNum(100 - v.balancePct);
        c.text(PAD + cw + 12, y + 19, bal, 2, LCD_FG);
        y += ch + 6;
    }

    // live workout strip (tap -> Workout screen)
    if (v.wkRunning) {
        c.card(PAD, y, LCD_W - PAD * 2, 30, lcdMix(0x3b, 0x82, 0xf6, 0x0f, 0x13, 0x20, 22, 100));
        std::string s = std::string(v.wkPaused ? "|| " : "> ") +
                        (v.wkTarget >= 0 ? lcdNum(v.wkTarget) + "W" : "free") + "  " +
                        lcdClock(v.wkRemainS) + " left";
        c.text(PAD + 8, y + 11, s, 1, LCD_FG);
        c.textRight(LCD_W - PAD - 6, y + 11, ">", 1, LCD_ACCENT);
    }

    lcdDrawNav(c, LcdScreen::Ride);
}

// ---------- Workout ------------------------------------------------------------------------
inline void lcdRenderWorkout(LcdCanvas& c, const WorkoutView& v) {
    using namespace lcdlay;
    c.clear();

    if (!v.loaded || v.w == nullptr) {
        lcdDrawTitle(c, "Workout", "");
        c.text(PAD, 40, "PICK A WORKOUT", 1, LCD_MUT);
        const auto& presets = workoutPresets();
        for (size_t i = 0; i < presets.size() && i < 4; ++i) {
            int ry = PICK_ROW0 + (int)i * PICK_ROW_H;
            c.card(PAD, ry, LCD_W - PAD * 2, PICK_ROW_H - 6, LCD_CARD);
            c.text(PAD + 8, ry + 8, lcdFit(presets[i].label, 19), 1, LCD_FG);
            c.text(PAD + 8, ry + 20, "tap to load", 1, LCD_MUT);
        }
        c.textCentered(PICK_ROW0 + 4 * PICK_ROW_H + 8, "or send from phone", 1, LCD_MUT);
        lcdDrawNav(c, LcdScreen::Workout);
        return;
    }

    const Workout& w = *v.w;
    lcdDrawTitle(c, w.name, lcdClock(v.st.totalElapsedS), LCD_MUT);

    // segment line
    char seg[32];
    if (v.st.finished) std::snprintf(seg, sizeof(seg), "Done!");
    else std::snprintf(seg, sizeof(seg), "%d of %d", v.st.segIndex + 1, (int)w.segments.size());
    c.text(PAD, 36, seg, 1, LCD_FG);
    if (!v.st.finished)
        c.textRight(LCD_W - PAD, 36, lcdClock(v.st.segRemainingS) + " left", 1, LCD_FG);

    // TARGET hero
    c.textCentered(52, "T A R G E T", 1, LCD_ACCENT);
    {
        std::string t = v.st.targetW >= 0 ? lcdNum(v.st.targetW) : "--";
        int scale = t.size() <= 3 ? 6 : 5;
        int tw = LcdCanvas::textWidth(t, scale) + 3 + LcdCanvas::textWidth("W", 2);
        int tx = (LCD_W - tw) / 2;
        c.text(tx, 64, t, scale, LCD_WHITE);
        c.text(tx + LcdCanvas::textWidth(t, scale) + 3, 64 + scale * 8 - 16, "W", 2, LCD_MUT);
    }
    // now line
    {
        std::string now = "now " + lcdNum(v.nowW) + "W";
        if (v.nowCad >= 0) now += " . " + lcdNum(v.nowCad) + "rpm";
        c.textCentered(118, now, 1, LCD_OK);
    }

    // profile chart: bars flex by duration; done/current/upcoming
    {
        const int chY = 134, chH = 56;
        long total = w.totalS();
        if (total > 0) {
            int maxW = 1;
            for (const auto& s : w.segments) {
                int t = segmentTargetW(s, w.ftpW);
                if (t > maxW) maxW = t;
            }
            int x = PAD;
            const int usable = LCD_W - PAD * 2;
            for (size_t i = 0; i < w.segments.size(); ++i) {
                const auto& s = w.segments[i];
                int bw = (int)((int64_t)s.durationS * usable / total);
                if (bw < 3) bw = 3;
                if (x + bw > LCD_W - PAD) bw = LCD_W - PAD - x;
                if (bw <= 0) break;
                int t = segmentTargetW(s, w.ftpW);
                int bh = 8 + (int)((int64_t)(t < 0 ? 0 : t) * (chH - 8) / maxW);
                uint16_t col = ((int)i < v.st.segIndex) ? LCD_OK_TINT
                               : ((int)i == v.st.segIndex && !v.st.finished) ? LCD_ACC_FILL
                                                                             : LCD_UP_TINT;
                c.fillRect(x, chY + chH - bh, bw - 1, bh, col);
                if ((int)i == v.st.segIndex && !v.st.finished)
                    c.rect(x, chY + chH - bh, bw - 1, bh, LCD_WHITE);
                x += bw;
            }
            c.hline(PAD, chY + chH + 1, usable, LCD_LINE);
        }
    }

    // next line
    {
        const int nextIdx = v.st.segIndex + 1;
        std::string next;
        if (v.st.finished) next = "workout complete";
        else if (nextIdx < (int)w.segments.size()) {
            int t = segmentTargetW(w.segments[nextIdx], w.ftpW);
            next = "next " + lcdFit(w.segments[nextIdx].label, 10) + " " +
                   (t >= 0 ? lcdNum(t) + "W" : "free");
        } else next = "last block";
        c.text(PAD, 202, next, 1, LCD_MUT);
    }

    // erg status line
    {
        std::string erg;
        uint16_t col = LCD_MUT;
        if (!v.ergConfigured) erg = "erg: no trainer set";
        else if (!v.ergConnected) { erg = "erg: connecting..."; col = LCD_MUT; }
        else if (!v.ergControlled) { erg = "erg: linked, no ctrl"; col = LCD_MUT; }
        else { erg = "erg: ON " + lcdNum(v.ergTarget) + "W"; col = LCD_OK; }
        c.text(PAD, 218, erg, 1, col);
    }

    // buttons
    {
        using namespace lcdlay;
        auto btn = [&](int slot, int nSlots, const std::string& label, uint16_t bg, uint16_t fg) {
            int bw = (LCD_W - PAD * 2 - (nSlots - 1) * 5) / nSlots;
            int bx = PAD + slot * (bw + 5);
            c.card(bx, WK_BTN_Y, bw, WK_BTN_H, bg);
            c.text(bx + (bw - LcdCanvas::textWidth(label, 1)) / 2, WK_BTN_Y + 13, label, 1, fg);
        };
        if (!v.running) {
            btn(0, 2, "S T A R T", LCD_ACCENT, LCD_WHITE);
            btn(1, 2, "Change", LCD_CHIP, LCD_FG);   // back to the preset picker
        } else {
            btn(0, 3, v.paused ? "Resume" : "Pause", LCD_CHIP, LCD_FG);
            btn(1, 3, "Skip", LCD_CHIP, LCD_FG);
            btn(2, 3, "Stop", lcdMix(0xef, 0x44, 0x44, 0x0f, 0x13, 0x20, 25, 100), LCD_BAD);
        }
    }

    lcdDrawNav(c, LcdScreen::Workout);
}

// ---------- Setup (meter + trainer picker) -------------------------------------------------
inline void lcdRenderSetup(LcdCanvas& c, const SetupView& v) {
    using namespace lcdlay;
    c.clear();
    lcdDrawTitle(c, "Pick devices", v.scanning ? "scan..." : "", LCD_OK);
    c.text(PAD, 34, "tap a row to select", 1, LCD_MUT);
    c.text(PAD, 46, "meter=power trn=erg", 1, LCD_MUT);

    const auto ds = lcdPickerList(v.devices);  // meters/cranks/trainers only; matches the tap path
    int shown = 0;
    for (size_t i = 0; i < ds.size() && shown < SETUP_MAX_ROWS; ++i, ++shown) {
        const auto& d = ds[i];
        int ry = SETUP_ROW0 + shown * SETUP_ROW_H;
        const bool selMeter = !v.meterAddr.empty() && d.address == v.meterAddr;
        const bool selTrainer = !v.trainerAddr.empty() && d.address == v.trainerAddr;
        c.card(PAD, ry, LCD_W - PAD * 2, SETUP_ROW_H - 6, LCD_CARD);
        if (selMeter || selTrainer) c.rect(PAD, ry, LCD_W - PAD * 2, SETUP_ROW_H - 6, LCD_ACCENT);
        // badge (right) first, then the name fits what's left of the row
        std::string badge = d.isFtms ? "trainer" : (d.isStagesCrank ? "crank" : (d.isCps ? "meter" : "?"));
        c.textRight(LCD_W - PAD - 7, ry + 6, badge, 1, d.isFtms ? LCD_ACCENT : LCD_OK);
        c.text(PAD + 7, ry + 6, lcdFit(d.name.empty() ? d.address : d.name, 11), 1, LCD_FG);
        // rssi bars (baseline-aligned, growing up) + selected mark
        int bars = rssiBars(d.rssi);
        for (int b = 0; b < 4; ++b) {
            int bh = 4 + b * 2;
            c.fillRect(PAD + 7 + b * 5, ry + 32 - bh, 3, bh, b < bars ? LCD_OK : LCD_CHIP);
        }
        if (selMeter) c.textRight(LCD_W - PAD - 7, ry + 22, "* meter", 1, LCD_ACCENT);
        if (selTrainer) c.textRight(LCD_W - PAD - 7, ry + 22, "* trainer", 1, LCD_ACCENT);
    }
    if (ds.empty())
        c.text(PAD, SETUP_ROW0 + 8, v.scanning ? "scanning..." : "wake devices + scan", 1, LCD_MUT);

    // buttons: Rescan | Save
    {
        int bw = (LCD_W - PAD * 2 - 5) / 2;
        c.card(PAD, SETUP_BTN_Y, bw, 32, LCD_CHIP);
        c.text(PAD + (bw - LcdCanvas::textWidth("Rescan", 1)) / 2, SETUP_BTN_Y + 12, "Rescan", 1, LCD_FG);
        c.card(PAD + bw + 5, SETUP_BTN_Y, bw, 32, v.dirty ? LCD_ACCENT : LCD_CHIP);
        c.text(PAD + bw + 5 + (bw - LcdCanvas::textWidth("Save", 1)) / 2, SETUP_BTN_Y + 12, "Save", 1,
               v.dirty ? LCD_WHITE : LCD_MUT);
    }
    lcdDrawNav(c, LcdScreen::Setup);
}

// ---------- More / Settings ----------------------------------------------------------------
inline void lcdRenderMore(LcdCanvas& c, const MoreView& v) {
    using namespace lcdlay;
    c.clear();
    lcdDrawTitle(c, "Settings", "sb20");
    struct Row { const char* label; std::string value; bool link; };
    const Row rows[] = {
        {"Workout", ">", true},
        {"Calibrate", ">", true},
        {"Mode", v.mode, false},
        {"Identity", v.identity, false},
        {"Source", v.source, false},
        {"Trainer", v.trainer, false},
        {"Bright", lcdNum(v.brightness) + "%", true},
        {"Firmware", "v" + v.version, false},
    };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
        int ry = MORE_ROW0 + (int)i * MORE_ROW_H;
        c.text(PAD + 2, ry + 9, rows[i].label, 1, LCD_FG);
        // value width = what the label leaves free on the 160px row (8px/char at scale 1)
        size_t labelChars = std::string(rows[i].label).size();
        size_t valMax = (size_t)((LCD_W - PAD * 2 - 4) / 8) - labelChars - 1;
        c.textRight(LCD_W - PAD - 2, ry + 9, lcdFit(rows[i].value, valMax), 1,
                    rows[i].link ? LCD_ACCENT : LCD_MUT);
        c.hline(PAD, ry + MORE_ROW_H - 1, LCD_W - PAD * 2, LCD_LINE);
    }
    c.textCentered(MORE_ROW0 + 8 * MORE_ROW_H + 8, v.ip, 1, LCD_MUT);
    lcdDrawNav(c, LcdScreen::More);
}

// ---------- Calibrate ----------------------------------------------------------------------
inline void lcdRenderCalibrate(LcdCanvas& c, const CalWizardView& v) {
    using namespace lcdlay;
    c.clear();

    if (v.state == CalState::Idle) {
        lcdDrawTitle(c, "Calibrate", "pick 2");
        c.text(PAD, 34, "correct DUT to match Ref", 1, LCD_MUT);
        const auto ds = dedupeAndSortSources(v.devices);
        int shown = 0;
        for (size_t i = 0; i < ds.size() && shown < CAL_MAX_ROWS; ++i, ++shown) {
            const auto& d = ds[i];
            int ry = CAL_ROW0 + shown * CAL_ROW_H;
            c.card(PAD, ry, LCD_W - PAD * 2, CAL_ROW_H - 6, LCD_CARD);
            c.text(PAD + 7, ry + 6, lcdFit(d.name.empty() ? d.address : d.name, 12), 1, LCD_FG);
            const bool isDut = !v.dutAddr.empty() && d.address == v.dutAddr;
            const bool isRef = !v.refAddr.empty() && d.address == v.refAddr;
            // [DUT] [Ref] tap targets
            c.fillRect(LCD_W - PAD - 74, ry + 18, 34, 14, isDut ? LCD_ACCENT : LCD_CHIP);
            c.text(LCD_W - PAD - 74 + 5, ry + 21, "DUT", 1, isDut ? LCD_WHITE : LCD_MUT);
            c.fillRect(LCD_W - PAD - 36, ry + 18, 34, 14, isRef ? LCD_ACCENT : LCD_CHIP);
            c.text(LCD_W - PAD - 36 + 5, ry + 21, "Ref", 1, isRef ? LCD_WHITE : LCD_MUT);
        }
        if (ds.empty()) c.text(PAD, CAL_ROW0 + 8, "wake both meters...", 1, LCD_MUT);
        const bool ready = !v.dutAddr.empty() && !v.refAddr.empty() && v.dutAddr != v.refAddr;
        c.card(PAD, CAL_BTN_Y, LCD_W - PAD * 2, 32, ready ? LCD_ACCENT : LCD_CHIP);
        c.textCentered(CAL_BTN_Y + 12, "Connect + start", 1, ready ? LCD_WHITE : LCD_MUT);
    } else if (v.state == CalState::Collecting) {
        lcdDrawTitle(c, "Calibrating", "riding", LCD_OK);
        std::string link = std::string("DUT ") + (v.dutConnected ? "OK" : "...") +
                           "   Ref " + (v.refConnected ? "OK" : "...");
        c.text(PAD, 36, link, 1, v.dutConnected && v.refConnected ? LCD_OK : LCD_MUT);
        c.text(PAD, 54, lcdNum(v.pairCount), 3, LCD_WHITE);
        c.text(PAD + LcdCanvas::textWidth(lcdNum(v.pairCount), 3) + 6, 62,
               "of " + lcdNum(v.minPairs) + " samples", 1, LCD_MUT);
        // coverage bands
        int by = 92;
        for (size_t i = 0; i + 1 < v.edges.size() && i < 6; ++i) {
            int n = (i < v.coverage.size()) ? v.coverage[i] : 0;
            char lab[16];
            std::snprintf(lab, sizeof(lab), "%3ld", (long)v.edges[i]);
            c.text(PAD, by + 2, lab, 1, LCD_MUT);
            c.bar(PAD + 32, by, LCD_W - PAD * 2 - 60, 10, n > 8 ? 8 : n, 8, LCD_OK, LCD_CHIP);
            c.textRight(LCD_W - PAD, by + 2, lcdNum(n), 1, LCD_MUT);
            by += 16;
        }
        // buttons: Finish (gated) | Cancel
        int bw = (LCD_W - PAD * 2 - 5) / 2;
        c.card(PAD, CAL_BTN_Y, bw, 32, v.enoughToFit ? LCD_ACCENT : LCD_CHIP);
        c.text(PAD + (bw - LcdCanvas::textWidth("Finish", 1)) / 2, CAL_BTN_Y + 12, "Finish", 1,
               v.enoughToFit ? LCD_WHITE : LCD_MUT);
        c.card(PAD + bw + 5, CAL_BTN_Y, bw, 32, LCD_CHIP);
        c.text(PAD + bw + 5 + (bw - LcdCanvas::textWidth("Cancel", 1)) / 2, CAL_BTN_Y + 12, "Cancel",
               1, LCD_FG);
    } else {  // Fitted
        lcdDrawTitle(c, "Fit complete", "", LCD_OK);
        char res[24];
        std::snprintf(res, sizeof(res), "+/- %.1f W", (double)v.residualW);
        c.text(PAD, 40, res, 2, LCD_OK);
        c.text(PAD, 62, "vs the reference, avg", 1, LCD_MUT);
        int ty = 84;
        if (v.linear) {
            char lin[32];
            std::snprintf(lin, sizeof(lin), "power x %.3f %+0.1f", (double)v.scale, (double)v.offset);
            c.text(PAD, ty, lin, 1, LCD_FG);
        } else {
            c.text(PAD, ty, "W      x factor", 1, LCD_MUT);
            ty += 14;
            for (size_t i = 0; i < v.curve.points.size() && i < 5; ++i) {
                char row[32];
                std::snprintf(row, sizeof(row), "%-6.0f %.3f",
                              (double)v.curve.points[i].power_w, (double)v.curve.points[i].factor);
                c.text(PAD, ty, row, 1, LCD_FG);
                ty += 12;
            }
        }
        int bw = (LCD_W - PAD * 2 - 5) / 2;
        c.card(PAD, CAL_BTN_Y, bw, 32, LCD_ACCENT);
        c.text(PAD + (bw - LcdCanvas::textWidth("Save", 1)) / 2, CAL_BTN_Y + 12, "Save", 1, LCD_WHITE);
        c.card(PAD + bw + 5, CAL_BTN_Y, bw, 32, LCD_CHIP);
        c.text(PAD + bw + 5 + (bw - LcdCanvas::textWidth("Discard", 1)) / 2, CAL_BTN_Y + 12,
               "Discard", 1, LCD_FG);
    }
    lcdDrawNav(c, LcdScreen::Calibrate);
}

// ---------- top-level render + tap routing --------------------------------------------------
inline void lcdRender(LcdCanvas& c, const LcdUiState& st, const LcdViews& v) {
    switch (st.screen) {
        case LcdScreen::Ride:      lcdRenderRide(c, v.ride, st); break;
        case LcdScreen::Workout:   lcdRenderWorkout(c, v.wk); break;
        case LcdScreen::Setup:     lcdRenderSetup(c, v.setup); break;
        case LcdScreen::More:      lcdRenderMore(c, v.more); break;
        case LcdScreen::Calibrate: lcdRenderCalibrate(c, v.cal); break;
    }
}

// Handle a tap at (x,y). Mutates the UI state (screen switches, toggles) and returns the
// device-level action for main.cpp to execute (or None). Pure + host-tested.
inline UiAction lcdHandleTap(LcdUiState& st, const LcdViews& v, int x, int y) {
    using namespace lcdlay;
    UiAction a;

    // bottom nav — always live
    if (y >= NAV_Y) {
        int tab = x / (LCD_W / 3);
        st.screen = tab == 0 ? LcdScreen::Ride : (tab == 1 ? LcdScreen::Setup : LcdScreen::More);
        return a;
    }

    switch (st.screen) {
        case LcdScreen::Ride:
            if (y < TITLE_H) { st.rideDetails = !st.rideDetails; return a; }
            // the live workout strip sits above the nav when a workout runs
            if (v.ride.wkRunning && y >= NAV_Y - 46) { st.screen = LcdScreen::Workout; return a; }
            return a;

        case LcdScreen::Workout: {
            const WorkoutView& w = v.wk;
            if (!w.loaded) {
                if (y >= PICK_ROW0 && y < PICK_ROW0 + 4 * PICK_ROW_H) {
                    int idx = (y - PICK_ROW0) / PICK_ROW_H;
                    if (idx >= 0 && idx < (int)workoutPresets().size()) {
                        a.type = UiAction::WorkoutPreset;
                        a.index = idx;
                    }
                }
                return a;
            }
            if (y >= WK_BTN_Y && y < WK_BTN_Y + WK_BTN_H) {
                if (!w.running) {
                    a.type = (x < LCD_W / 2) ? UiAction::WorkoutStart : UiAction::WorkoutUnload;
                    return a;
                }
                int slot = (x - PAD) / ((LCD_W - PAD * 2) / 3);
                if (slot <= 0) a.type = w.paused ? UiAction::WorkoutResume : UiAction::WorkoutPause;
                else if (slot == 1) a.type = UiAction::WorkoutSkip;
                else a.type = UiAction::WorkoutStop;
            }
            return a;
        }

        case LcdScreen::Setup: {
            if (y >= SETUP_ROW0 && y < SETUP_ROW0 + SETUP_MAX_ROWS * SETUP_ROW_H) {
                int idx = (y - SETUP_ROW0) / SETUP_ROW_H;
                a.type = UiAction::SetupPick;
                a.index = idx;
                return a;
            }
            if (y >= SETUP_BTN_Y && y < SETUP_BTN_Y + 32) {
                a.type = (x < LCD_W / 2) ? UiAction::SetupScan : UiAction::SetupSave;
                return a;
            }
            return a;
        }

        case LcdScreen::More: {
            if (y >= MORE_ROW0 && y < MORE_ROW0 + 8 * MORE_ROW_H) {
                int idx = (y - MORE_ROW0) / MORE_ROW_H;
                if (idx == 0) { st.screen = LcdScreen::Workout; return a; }
                if (idx == 1) { st.screen = LcdScreen::Calibrate; return a; }
                if (idx == 6) {  // brightness cycles 25 -> 50 -> 75 -> 100
                    st.brightness = (uint8_t)(st.brightness >= 100 ? 25 : st.brightness + 25);
                    a.type = UiAction::SetBrightness;
                    a.index = st.brightness;
                    return a;
                }
            }
            return a;
        }

        case LcdScreen::Calibrate: {
            const CalWizardView& cal = v.cal;
            if (cal.state == CalState::Idle) {
                if (y >= CAL_ROW0 && y < CAL_ROW0 + CAL_MAX_ROWS * CAL_ROW_H) {
                    int idx = (y - CAL_ROW0) / CAL_ROW_H;
                    a.index = idx;
                    a.type = (x >= LCD_W - PAD - 74 && x < LCD_W - PAD - 38) ? UiAction::CalPickDut
                             : (x >= LCD_W - PAD - 38) ? UiAction::CalPickRef
                                                       : UiAction::None;
                    return a;
                }
                if (y >= CAL_BTN_Y && y < CAL_BTN_Y + 32) { a.type = UiAction::CalStart; return a; }
            } else if (cal.state == CalState::Collecting) {
                if (y >= CAL_BTN_Y && y < CAL_BTN_Y + 32) {
                    a.type = (x < LCD_W / 2) ? UiAction::CalFinish : UiAction::CalCancel;
                    return a;
                }
            } else {
                if (y >= CAL_BTN_Y && y < CAL_BTN_Y + 32) {
                    a.type = (x < LCD_W / 2) ? UiAction::CalSave : UiAction::CalCancel;
                    return a;
                }
            }
            return a;
        }
    }
    return a;
}

}  // namespace sb20proxy
