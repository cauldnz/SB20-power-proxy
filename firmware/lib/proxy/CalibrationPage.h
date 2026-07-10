#pragma once
#include <string>
#include <vector>

#include "CalibrationSession.h"  // CalState + defaultCoverageEdges
#include "Config.h"              // CORRECTOR_NAME default
#include "Correction.h"          // CorrectionCurve
#include "Provisioning.h"        // urlDecode / htmlEscape / rssiBars
#include "SourceCandidate.h"     // SourceCandidate + dedupeAndSortSources
#include "WebUi.h"               // shared palette + base layout + bottom-nav CSS

namespace sb20proxy {

// The pure (no-Arduino) half of the on-device CALIBRATION wizard — the screen that walks the rider
// through making a meter read like a reference: pick the DUT (e.g. XCadey) + the reference (e.g.
// Assioma), ride a power sweep while the device pairs the two streams, then fit + save a correction
// the corrector applies. Mirrors ConfigPage.h: HTML render + form parse + validation here
// (host-tested); the two BLE centrals + the CalibrationSession + NVS save are the seam in src/.
//
// Coverage-guided (the rider presses Finish once enough bands are covered) and the saved corrector
// device name is editable. Three screens by CalState: Idle (pick) -> Collecting (ride) -> Fitted (save).

// A snapshot the seam fills each request; the page is a pure function of it.
struct CalWizardView {
    CalState state = CalState::Idle;
    std::vector<SourceCandidate> devices;     // scanned sources, for the DUT + reference pickers
    std::string dutAddr;                       // chosen DUT pin (echoed into a hidden field)
    std::string refAddr;                       // chosen reference pin
    bool dutConnected = false;
    bool refConnected = false;
    int pairCount = 0;
    int minPairs = 30;
    bool enoughToFit = false;
    std::vector<int> coverage;                 // per-band pair counts (Collecting/Fitted)
    std::vector<float> edges = defaultCoverageEdges();
    CorrectionCurve curve;                     // Fitted: the non-linear curve (if any)
    bool linear = false;                       // Fitted via the linear fallback instead of a curve
    float scale = 1.0f;
    float offset = 0.0f;
    float residualW = 0.0f;
    std::string deviceName = Config::CORRECTOR_NAME;  // editable name saved to NVS
    std::string message;                       // validation / error banner
    bool scanning = false;
};

// Parsed wizard form. `action` ∈ {start, finish, save, cancel, scan}; the rest are the fields each
// action needs. Keys: action, dut, ref, name.
struct CalForm {
    std::string action;
    std::string dutAddr;
    std::string refAddr;
    std::string deviceName;
};

inline CalForm parseCalibrationForm(const std::string& body) {
    CalForm f;
    forEachFormField(body, [&](const std::string& key, const std::string& val) {
        if (key == "action") f.action = val;
        else if (key == "dut") f.dutAddr = val;
        else if (key == "ref") f.refAddr = val;
        else if (key == "name") f.deviceName = stripConfigDelims(val);
    });
    return f;
}

// Validate a Start request: a DUT and a reference must be chosen, and they must differ (you can't
// calibrate a meter against itself). Returns a reason, or nullptr if OK.
inline const char* calibrationStartError(const CalForm& f) {
    if (f.dutAddr.empty() || f.refAddr.empty())
        return "Pick BOTH a meter to correct (DUT) and a reference meter.";
    if (f.dutAddr == f.refAddr)
        return "The DUT and reference must be two different meters.";
    return nullptr;
}

namespace detail {
inline std::string calStyle() {
    return std::string("<style>") + webuiCss() +
           ".row{display:flex;align-items:center;justify-content:space-between;margin:10px 0 4px}"
           ".row a{color:var(--ok);font-size:.85rem;text-decoration:none}"
           ".msg{background:rgba(239,68,68,.12);border:1px solid rgba(239,68,68,.4);color:#fca5a5;"
           "padding:9px 12px;border-radius:9px;margin:8px 0}"
           ".ok{background:rgba(34,197,94,.12);border:1px solid rgba(34,197,94,.4);color:var(--ok);"
           "padding:9px 12px;border-radius:9px;margin:8px 0}"
           ".devs{display:flex;flex-direction:column;gap:8px;margin:10px 0}"
           ".dev{display:flex;align-items:center;gap:8px;padding:10px 12px;background:var(--card);"
           "border:1px solid var(--line);border-radius:11px}"
           ".dev .nm{flex:1;min-width:0;overflow:hidden;font-size:.92rem;font-weight:600}"
           ".dev .nm small{color:var(--mut);font-weight:400;font-size:.78rem}"
           ".pick{display:flex;gap:6px;flex-shrink:0}.pick button{padding:7px 11px;border:1px solid var(--accent);"
           "background:transparent;color:var(--accent);border-radius:7px;cursor:pointer;font-size:.82rem}"
           ".pick button.on{background:var(--accent);color:#fff}"
           ".bands{display:flex;flex-direction:column;gap:6px;margin:12px 0}"
           ".band{display:flex;align-items:center;gap:10px}.band .lab{width:78px;font-size:.82rem;color:var(--mut)}"
           ".bar{flex:1;height:14px;background:var(--chip2);border-radius:7px;overflow:hidden}"
           ".bar i{display:block;height:100%;background:var(--ok)}.band .n{width:28px;text-align:right;"
           "font-size:.82rem;color:var(--mut)}"
           "button.go{width:100%;padding:13px;font-size:1rem;font-weight:600;color:#fff;background:var(--accent);"
           "border:0;border-radius:10px;cursor:pointer;margin-top:10px}"
           "button.go[disabled]{background:#2a3550;color:#7a86a3;cursor:default}"
           "button.sec{width:100%;padding:11px;background:transparent;border:1px solid var(--line);"
           "color:var(--fg);border-radius:10px;cursor:pointer;margin-top:8px}"
           "input[type=text]{width:100%;padding:11px;font-size:1rem;background:#0a0d16;color:var(--fg);"
           "border:1px solid var(--line);border-radius:9px;margin:5px 0 12px}"
           "label{font-weight:600;font-size:.9rem}a{color:var(--accent)}.hint{color:var(--mut);font-size:.85rem}"
           "table{width:100%;border-collapse:collapse;margin:8px 0}"
           "td{padding:5px 6px;border-bottom:1px solid var(--line);font-size:.9rem}"
           "</style>";
}

// Band label like "150-200" / "<100" / "300+" from the coverage edges.
inline std::string bandLabel(const std::vector<float>& edges, size_t i) {
    if (i + 1 >= edges.size()) return "";  // need edges[i] and edges[i+1] — guard a short edges list
    char buf[32];
    const long lo = (long)edges[i];
    if (i == 0) {
        std::snprintf(buf, sizeof(buf), "&lt;%ld", (long)edges[1]);
    } else if (i + 2 >= edges.size()) {
        std::snprintf(buf, sizeof(buf), "%ld+", lo);
    } else {
        std::snprintf(buf, sizeof(buf), "%ld-%ld", lo, (long)edges[i + 1]);
    }
    return buf;
}
}  // namespace detail

inline std::string renderCalibrationPage(const CalWizardView& v) {
    std::string h =
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>SB20 Proxy &mdash; Calibrate</title>";
    if (v.scanning || v.state == CalState::Collecting) h += "<meta http-equiv='refresh' content='2'>";
    h += detail::calStyle() +
         "</head><body><div class='wrap'>"
         "<div class='tb'><span>Calibrate a meter</span></div>";
    if (!v.message.empty()) h += "<p class='msg'>" + htmlEscape(v.message) + "</p>";

    if (v.state == CalState::Idle) {
        // --- pick the DUT + the reference -----------------------------------------------------
        h += "<p class='hint'>Make one meter read like another. Pick the meter to <b>correct</b> "
             "(DUT) and a <b>reference</b> you trust, connect both, then ride a power sweep.</p>"
             "<div class='row'><label>Nearby meters</label> <a href='/calibrate/scan'>";
        h += v.scanning ? "Scanning&hellip;" : "Scan";
        h += "</a></div>";
        const std::vector<SourceCandidate> ds = dedupeAndSortSources(v.devices);
        if (ds.empty()) {
            h += "<p class='hint'>No meters yet &mdash; spin them so they advertise, then Scan.</p>";
        } else {
            h += "<div class='devs'>";
            for (const auto& d : ds) {
                h += "<div class='dev'><span class='nm'>";
                h += d.name.empty() ? "<i>(unnamed)</i>" : htmlEscape(d.name);
                h += "<br><small>" + htmlEscape(d.address) + "</small></span><span class='pick'>"
                     "<button type='button' class='bd' data-a='" + htmlEscape(d.address) +
                     "' onclick='setRole(this,\"dut\")'>DUT</button>"
                     "<button type='button' class='br' data-a='" + htmlEscape(d.address) +
                     "' onclick='setRole(this,\"ref\")'>Ref</button></span></div>";
            }
            h += "</div>";
        }
        h += "<form method='POST' action='/calibrate/start'>"
             "<input type='hidden' id='dut' name='dut' value='" + htmlEscape(v.dutAddr) + "'>"
             "<input type='hidden' id='ref' name='ref' value='" + htmlEscape(v.refAddr) + "'>"
             "<table><tr><td>DUT (correct this)</td><td id='dutL'>" +
             (v.dutAddr.empty() ? "&mdash;" : htmlEscape(v.dutAddr)) + "</td></tr>"
             "<tr><td>Reference (match this)</td><td id='refL'>" +
             (v.refAddr.empty() ? "&mdash;" : htmlEscape(v.refAddr)) + "</td></tr></table>"
             "<button class='go' type='submit'>Connect both &amp; start</button></form>"
             "<script>function setRole(b,role){var a=b.getAttribute('data-a');"
             "document.getElementById(role).value=a;"
             "document.getElementById(role+'L').textContent=a;"
             "var cls=(role=='dut')?'bd':'br';var bs=document.querySelectorAll('.'+cls);"
             "for(var i=0;i<bs.length;i++)bs[i].classList.remove('on');b.classList.add('on');}</script>";
    } else if (v.state == CalState::Collecting) {
        // --- ride + watch coverage fill -------------------------------------------------------
        h += "<p class='ok'>Collecting &mdash; ride a sweep from easy to hard. ";
        h += std::string("DUT ") + (v.dutConnected ? "connected" : "<b>connecting&hellip;</b>");
        h += std::string(", reference ") + (v.refConnected ? "connected" : "<b>connecting&hellip;</b>");
        h += ".</p><p><b>" + std::to_string(v.pairCount) + "</b> paired samples (need &ge; " +
             std::to_string(v.minPairs) + ").</p><div class='bands'>";
        for (size_t i = 0; i + 1 < v.edges.size(); ++i) {
            const int n = (i < v.coverage.size()) ? v.coverage[i] : 0;
            const int pct = n >= 8 ? 100 : n * 12;  // ~8 samples fills a band's bar
            h += "<div class='band'><span class='lab'>" + detail::bandLabel(v.edges, i) +
                 " W</span><span class='bar'><i style='width:" + std::to_string(pct) +
                 "%'></i></span><span class='n'>" + std::to_string(n) + "</span></div>";
        }
        h += "</div><form method='POST' action='/calibrate/finish'>"
             "<button class='go' type='submit'";
        if (!v.enoughToFit) h += " disabled";
        h += ">Finish &amp; fit</button></form>";
        if (!v.enoughToFit)
            h += "<p class='hint'>Keep riding &mdash; cover more of the power range to enable Finish.</p>";
        h += "<form method='POST' action='/calibrate/cancel'>"
             "<button class='sec' type='submit'>Cancel</button></form>";
    } else {  // Fitted
        // --- review the fit + name + save -----------------------------------------------------
        char res[48];
        std::snprintf(res, sizeof(res), "%.1f", v.residualW);
        h += "<p class='ok'>Fit complete. After correction the DUT is within <b>";
        h += res;
        h += " W</b> of the reference, on average.</p>";
        if (v.linear) {
            char so[64];
            std::snprintf(so, sizeof(so), "power &times; %.3f + %.1f", v.scale, v.offset);
            h += "<p>Correction: linear (" + std::string(so) + ").</p>";
        } else {
            h += "<p>Correction curve:</p><table><tr><td><b>Power (W)</b></td><td><b>&times; factor</b></td></tr>";
            for (const auto& pt : v.curve.points) {
                char row[64];
                std::snprintf(row, sizeof(row), "<tr><td>%.0f</td><td>%.3f</td></tr>", pt.power_w, pt.factor);
                h += row;
            }
            h += "</table>";
        }
        h += "<form method='POST' action='/calibrate/save'>"
             "<label for='name'>Device name <span class='hint'>(what your Garmin sees)</span></label>"
             "<input type='text' id='name' name='name' autocomplete='off' autocapitalize='none' "
             "autocorrect='off' spellcheck='false' value='" + htmlEscape(v.deviceName) +
             "' placeholder='SB20 Corrector'>"
             "<button class='go' type='submit'>Save &amp; switch to corrector mode</button></form>"
             "<p class='hint'>Saving reboots into corrector mode. Then remove the reference meter "
             "(e.g. take the pedals off) &mdash; the DUT, corrected, is rebroadcast under this name.</p>"
             "<form method='POST' action='/calibrate/cancel'>"
             "<button class='sec' type='submit'>Discard &amp; recalibrate</button></form>";
    }
    h += "</div>"  // .wrap
         "<nav class='nav'><a href='/'>Ride</a><a href='/setup'>Setup</a>"
         "<a href='/more'>More</a></nav>"
         "</body></html>";
    return h;
}

}  // namespace sb20proxy
