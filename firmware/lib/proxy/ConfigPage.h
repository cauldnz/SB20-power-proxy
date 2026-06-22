#pragma once
#include <algorithm>
#include <string>
#include <vector>

#include "Provisioning.h"     // reuse urlDecode / htmlEscape / rssiBars (the captive-portal helpers)
#include "RuntimeConfig.h"
#include "SourceCandidate.h"  // SourceCandidate + dedupeAndSortSources (the scanned-source model)

namespace sb20proxy {

// The pure (no-Arduino) half of the on-device SOURCE setup page — the screen a tester uses to
// pick which power source the proxy reads (their meter, or a surviving crank) over the ESP32's
// WiFi. Mirrors Provisioning.h: HTML rendering + form parsing + validation here (host-tested);
// the BLE scan + HTTP routing + NVS save are the seam in src/. The page pins the source by BLE
// ADDRESS (deterministic), with an optional name-substring fallback and a single-sided ×2 toggle.
// SourceCandidate + dedupeAndSortSources live in SourceCandidate.h (shared with the scan seam).

// Parse the source-setup form. Keys: `addr` (the pinned BLE address; "" = match by name), `name`
// (the name-substring fallback), `single` (checkbox; present/"1"/"on" = true). Unknown keys
// ignored. Reuses Provisioning.h's urlDecode.
inline RuntimeConfig parseConfigForm(const std::string& body) {
    RuntimeConfig c;
    bool single = false;
    size_t pos = 0;
    while (pos <= body.size()) {
        size_t amp = body.find('&', pos);
        size_t end = (amp == std::string::npos) ? body.size() : amp;
        std::string pair = body.substr(pos, end - pos);
        size_t eq = pair.find('=');
        std::string key = urlDecode(eq == std::string::npos ? pair : pair.substr(0, eq));
        std::string val = urlDecode(eq == std::string::npos ? std::string() : pair.substr(eq + 1));
        if (key == "addr") c.meterAddress = val;
        else if (key == "name") c.meterNameFilter = val;
        else if (key == "single") single = (val == "1" || val == "on" || val.empty());
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    c.singleSidedDouble = single;
    return c;
}

// Validate a submitted source config: at least one of (pinned address, name filter) must be set,
// else there is nothing to match. Returns a human-readable reason, or nullptr if valid.
inline const char* configValidationError(const RuntimeConfig& c) {
    if (c.meterAddress.empty() && c.meterNameFilter.empty())
        return "Pick a source from the list, or enter a name to match (e.g. ASSIOMA).";
    return nullptr;
}

// Render the source-setup page. Discovered devices become a tap-list (each row fills the hidden
// `addr` field + shows which is selected), with signal bars, a CPS badge, and a "crank" label for
// Stages advertisers. A name-substring field is the fallback when nothing is pinned; a checkbox
// sets the single-sided ×2. `message` surfaces a validation error; `scanning` marks a rescan in
// flight (auto-refresh until results land). Pure string-building → host-tested.
inline std::string renderConfigPage(const RuntimeConfig& cfg,
                                    const std::vector<SourceCandidate>& devices = {},
                                    const std::string& message = std::string(),
                                    bool scanning = false, int logState = -1,
                                    const std::string& currentStatus = std::string()) {
    const std::vector<SourceCandidate> ds = dedupeAndSortSources(devices);
    std::string h =
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>SB20 Proxy &mdash; Power source</title>";
    if (scanning) h += "<meta http-equiv='refresh' content='2'>";
    h += "<style>"
         "body{font-family:system-ui,-apple-system,sans-serif;max-width:480px;margin:0 auto;"
         "padding:16px;color:#111;background:#fafafa}"
         "h1{font-size:1.3rem}"
         ".msg{background:#fdecea;border:1px solid #f5c2c7;color:#842029;padding:8px 12px;"
         "border-radius:6px}"
         ".row{display:flex;align-items:center;justify-content:space-between;margin:10px 0 4px}"
         ".devs{display:flex;flex-direction:column;gap:6px;margin:8px 0}"
         ".dev{display:flex;align-items:center;gap:10px;width:100%;padding:12px;font-size:1rem;"
         "text-align:left;background:#fff;border:1px solid #ccc;border-radius:8px;cursor:pointer}"
         ".dev.sel{border-color:#2a6df4;background:#eef4ff}"
         ".dev .nm{flex:1;overflow:hidden}"
         ".dev .nm small{color:#666;font-weight:400}"
         ".badge{font-size:.7rem;padding:1px 6px;border-radius:10px;background:#e7f5ec;color:#1d6b34}"
         ".badge.crank{background:#fff3cd;color:#7a5b00}"
         ".sig{display:inline-flex;align-items:flex-end;gap:2px;height:14px}"
         ".sig i{width:4px;background:#ccc;border-radius:1px}"
         ".sig i:nth-child(1){height:5px}.sig i:nth-child(2){height:8px}"
         ".sig i:nth-child(3){height:11px}.sig i:nth-child(4){height:14px}"
         ".sig i.on{background:#2a8a3e}"
         "input[type=text]{width:100%;box-sizing:border-box;padding:10px;font-size:1rem;"
         "border:1px solid #ccc;border-radius:8px;margin:4px 0 12px}"
         "label{font-weight:600;font-size:.9rem}"
         ".chk{display:flex;align-items:center;gap:8px;font-weight:400;margin:8px 0 14px}"
         ".chk input{width:auto;margin:0}"
         "button.go{width:100%;padding:12px;font-size:1rem;font-weight:600;color:#fff;"
         "background:#2a6df4;border:0;border-radius:8px;cursor:pointer}"
         "a{color:#2a6df4}.hint{color:#666;font-size:.85rem}"
         ".ok{background:#e7f5ec;border:1px solid #b7e0c4;color:#1d6b34;padding:8px 12px;"
         "border-radius:6px;margin:8px 0}"
         "</style></head><body>"
         "<h1>Choose your power source</h1>"
         "<p class='hint'>Pick the meter (or surviving crank) the SB20 should read.</p>";
    // Live "currently reading X" / "searching" banner — lets a tester verify the source is
    // connected before riding (the pre-flight-verify principle), straight from this page.
    if (!currentStatus.empty()) h += "<p class='ok'>" + htmlEscape(currentStatus) + "</p>";
    if (!message.empty()) h += "<p class='msg'>" + htmlEscape(message) + "</p>";

    h += "<div class='row'><label>Nearby sources</label><a href='/setup/scan'>";
    h += scanning ? "Scanning&hellip;" : "Scan";
    h += "</a></div>";

    if (ds.empty()) {
        h += "<p class='hint'>";
        h += scanning ? "Scanning for power sources&hellip;"
                      : "No sources yet &mdash; spin your meter/crank so it advertises, then tap Scan.";
        h += "</p>";
    } else {
        h += "<div class='devs'>";
        for (const auto& d : ds) {
            const bool sel = (!cfg.meterAddress.empty() && d.address == cfg.meterAddress);
            const int bars = rssiBars(d.rssi);
            h += "<button type='button' class='dev";
            if (sel) h += " sel";
            h += "' data-addr='" + htmlEscape(d.address) + "' onclick='pick(this)'>";
            h += "<span class='nm'>";
            h += d.name.empty() ? "<i>(unnamed)</i>" : htmlEscape(d.name);
            h += "<br><small>" + htmlEscape(d.address) + "</small></span>";
            if (d.isStagesCrank) h += "<span class='badge crank' title='a Stages crank'>crank</span>";
            else if (d.isCps) h += "<span class='badge' title='Cycling Power Service'>power</span>";
            h += "<span class='sig' title='" + std::to_string(d.rssi) + " dBm'>";
            for (int b = 1; b <= 4; ++b) h += (b <= bars) ? "<i class='on'></i>" : "<i></i>";
            h += "</span></button>";
        }
        h += "</div>";
    }

    h += "<form method='POST' action='/setup/save'>"
         "<input type='hidden' id='addr' name='addr' value='" + htmlEscape(cfg.meterAddress) + "'>"
         "<label for='name'>Or match by name <span class='hint'>(used if nothing is picked above)</span></label>"
         "<input type='text' id='name' name='name' autocomplete='off' autocapitalize='none' "
         "autocorrect='off' spellcheck='false' value='" + htmlEscape(cfg.meterNameFilter) + "' "
         "placeholder='e.g. ASSIOMA'>"
         "<label class='chk'><input type='checkbox' name='single' value='1'";
    if (cfg.singleSidedDouble) h += " checked";
    h += "> Single-sided source &mdash; double it for total <span class='hint'>(a right-only / "
         "surviving crank)</span></label>"
         "<button class='go' type='submit'>Save</button></form>"
         "<script>function pick(b){"
         "document.getElementById('addr').value=b.getAttribute('data-addr');"
         "var ds=document.querySelectorAll('.dev');for(var i=0;i<ds.length;i++)ds[i].classList.remove('sel');"
         "b.classList.add('sel');}</script>";

    h += renderLogToggleFooter(logState);
    h += "</body></html>";
    return h;
}

// The page shown right after the user saves a source. The device reboots to apply it, so this
// page can't poll the result. Pure/host-tested.
inline std::string renderConfigSavedPage(const RuntimeConfig& cfg) {
    const std::string src = cfg.meterAddress.empty()
                                ? ("name &ldquo;" + htmlEscape(cfg.meterNameFilter) + "&rdquo;")
                                : htmlEscape(cfg.meterAddress);
    return
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>SB20 Proxy &mdash; Saved</title>"
        "<style>body{font-family:system-ui,-apple-system,sans-serif;max-width:480px;margin:0 auto;"
        "padding:16px;color:#111;background:#fafafa}h1{font-size:1.3rem}"
        ".ok{background:#e7f5ec;border:1px solid #b7e0c4;color:#1d6b34;padding:10px 14px;"
        "border-radius:8px}li{margin:6px 0}a{color:#2a6df4}</style></head><body>"
        "<h1>Saved &#10003;</h1>"
        "<p class='ok'>Source set to <b>" + src + "</b>" +
        (cfg.singleSidedDouble ? " (single-sided &times;2)" : "") +
        " &mdash; the device is restarting to apply it.</p>"
        "<p>When it's back, the dashboard at <a href='/'>/</a> should show your source connected. "
        "Spin it to confirm the power tracks.</p></body></html>";
}

}  // namespace sb20proxy
