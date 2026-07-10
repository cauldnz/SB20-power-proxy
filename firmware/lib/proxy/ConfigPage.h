#pragma once
#include <algorithm>
#include <string>
#include <vector>

#include "Provisioning.h"     // reuse urlDecode / htmlEscape / rssiBars (the captive-portal helpers)
#include "RuntimeConfig.h"
#include "SourceCandidate.h"  // SourceCandidate + dedupeAndSortSources (the scanned-source model)
#include "WebUi.h"            // shared palette + base layout + bottom-nav CSS

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
        else if (key == "name") c.meterNameFilter = stripConfigDelims(val);
        else if (key == "single") single = (val == "1" || val == "on" || val.empty());
        else if (key == "spoof_name") c.spoofName = stripConfigDelims(val);
        else if (key == "spoof_serial") c.spoofSerial = stripConfigDelims(val);
        else if (key == "trainer") c.trainerNameFilter = stripConfigDelims(val);
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    c.singleSidedDouble = single;
    // The spoof identity must always be present (we advertise it); fall back to the default when the
    // form leaves it blank, so the device can never end up nameless.
    if (c.spoofName.empty()) c.spoofName = Config::SPOOF_NAME;
    if (c.spoofSerial.empty()) c.spoofSerial = Config::SPOOF_SERIAL;
    return c;
}

// Merge the shared web SPA's `POST /config` body into an EXISTING config, changing only the fields the
// SPA sends and PRESERVING everything it doesn't (the fitted correction curve, reference meter, trainer,
// spoof serial, pinned address). This is the difference from parseConfigForm, which builds a FRESH
// config (right for the full /setup page, but as a partial update it would silently wipe the curve +
// revert the mode to the default). Keys (all optional; absent = keep current): `single` (checkbox,
// "1"/"on"/"true" = on), `src_filter` (source name filter), `out_name` (advertised identity name),
// `mode` ("corrector" | "spoof"). Pure + host-tested. Reuses urlDecode / stripConfigDelims.
inline RuntimeConfig mergeSpaConfigForm(const RuntimeConfig& current, const std::string& body) {
    RuntimeConfig c = current;
    size_t pos = 0;
    while (pos <= body.size()) {
        size_t amp = body.find('&', pos);
        size_t end = (amp == std::string::npos) ? body.size() : amp;
        std::string pair = body.substr(pos, end - pos);
        size_t eq = pair.find('=');
        std::string key = urlDecode(eq == std::string::npos ? pair : pair.substr(0, eq));
        std::string val = urlDecode(eq == std::string::npos ? std::string() : pair.substr(eq + 1));
        if (key == "single") c.singleSidedDouble = (val == "1" || val == "on" || val == "true");
        else if (key == "src_filter") c.meterNameFilter = stripConfigDelims(val);
        else if (key == "out_name") c.spoofName = stripConfigDelims(val);
        else if (key == "mode") c.mode = (val == "corrector") ? ProxyMode::Corrector : ProxyMode::Spoof;
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    if (c.spoofName.empty()) c.spoofName = Config::SPOOF_NAME;  // we always advertise a name
    return c;
}

// Does the urlencoded form body carry `key` at all? Distinguishes "present but empty" (an explicit
// CLEAR — e.g. wiping the trainer field) from "absent" (an old page / a curl without the field —
// PRESERVE the stored value). The /setup/save route uses this so a save from a form that predates
// a field can never silently wipe it. Pure + host-tested.
inline bool formHasField(const std::string& body, const std::string& key) {
    size_t pos = 0;
    while (pos <= body.size()) {
        size_t amp = body.find('&', pos);
        size_t end = (amp == std::string::npos) ? body.size() : amp;
        std::string pair = body.substr(pos, end - pos);
        size_t eq = pair.find('=');
        if (urlDecode(eq == std::string::npos ? pair : pair.substr(0, eq)) == key) return true;
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return false;
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
    h += "<style>";
    h += webuiCss();  // shared palette + base layout + bottom-nav
    h += ".tb a.scan{color:var(--ok);font-size:.85rem;font-weight:500;text-decoration:none}"
         ".msg{background:rgba(239,68,68,.12);border:1px solid rgba(239,68,68,.4);color:#fca5a5;"
         "padding:9px 12px;border-radius:9px;margin:8px 0}"
         ".ok{background:rgba(34,197,94,.12);border:1px solid rgba(34,197,94,.4);color:var(--ok);"
         "padding:9px 12px;border-radius:9px;margin:8px 0}"
         ".devs{display:flex;flex-direction:column;gap:8px;margin:10px 0}"
         ".dev{display:flex;align-items:center;gap:10px;width:100%;padding:11px 12px;font-size:1rem;"
         "text-align:left;background:var(--card);border:1px solid var(--line);border-radius:11px;"
         "color:var(--fg);cursor:pointer;font-family:inherit}"
         ".dev.sel{border-color:var(--accent)}"
         ".dev .nm{flex:1;min-width:0;overflow:hidden;font-size:.92rem;font-weight:600}"
         ".dev .nm small{color:var(--mut);font-weight:400;font-size:.78rem}"
         ".badge{font-size:.68rem;padding:2px 7px;border-radius:7px;background:rgba(34,197,94,.16);"
         "color:var(--ok);flex-shrink:0}"
         ".badge.crank{background:var(--chip2);color:var(--mut)}"
         ".sig{display:inline-flex;align-items:flex-end;gap:2px;height:14px;flex-shrink:0}"
         ".sig i{width:4px;background:var(--chip2);border-radius:1px}"
         ".sig i:nth-child(1){height:5px}.sig i:nth-child(2){height:8px}"
         ".sig i:nth-child(3){height:11px}.sig i:nth-child(4){height:14px}"
         ".sig i.on{background:var(--ok)}"
         "input[type=text]{width:100%;padding:11px;font-size:1rem;background:#0a0d16;color:var(--fg);"
         "border:1px solid var(--line);border-radius:9px;margin:5px 0 12px}"
         "label{font-weight:600;font-size:.9rem}"
         ".chk{display:flex;align-items:center;gap:9px;font-weight:400;font-size:.9rem;margin:8px 0 14px}"
         ".chk input{width:auto;margin:0}"
         "details{margin:4px 0 6px}summary{cursor:pointer;font-weight:600;margin:6px 0 10px;color:var(--fg)}"
         "button.go{width:100%;padding:13px;font-size:1rem;font-weight:600;color:#fff;"
         "background:var(--accent);border:0;border-radius:10px;cursor:pointer}"
         ".reset{width:100%;padding:11px;background:transparent;border:1px solid rgba(239,68,68,.5);"
         "color:#f87171;border-radius:10px;cursor:pointer}"
         "a{color:var(--accent)}"
         "</style></head><body><div class='wrap'>"
         "<div class='tb'><span>Pick your meter</span><a class='scan' href='/setup/scan'>";
    h += scanning ? "Scanning&hellip;" : "&#8635; Scan";
    h += "</a></div>"
         "<p class='hint'>Pick the meter (or surviving crank) the SB20 should read.</p>";
    // Live "currently reading X" / "searching" banner — lets a tester verify the source is
    // connected before riding (the pre-flight-verify principle), straight from this page.
    if (!currentStatus.empty()) h += "<p class='ok'>" + htmlEscape(currentStatus) + "</p>";
    if (!message.empty()) h += "<p class='msg'>" + htmlEscape(message) + "</p>";

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
         // --- Crank identity: what the SB20 sees as its crank (advanced) --------------------------
         "<details><summary style='cursor:pointer;font-weight:600;margin:6px 0 10px'>Crank identity "
         "(advanced)</summary>"
         "<p class='hint'>What the SB20 pairs to as its crank. Set this to <b>your</b> Stages crank's "
         "ID (shown in the Stages app) so the bike accepts it &mdash; or a different number to run "
         "alongside a still-working crank. Pull the matching crank's battery before riding.</p>"
         "<label for='spoof_name'>Crank name</label>"
         "<input type='text' id='spoof_name' name='spoof_name' autocomplete='off' "
         "autocapitalize='none' autocorrect='off' spellcheck='false' value='" +
         htmlEscape(cfg.spoofName) + "' placeholder='Stages 62144'>"
         "<label for='spoof_serial'>Serial <span class='hint'>(optional)</span></label>"
         "<input type='text' id='spoof_serial' name='spoof_serial' autocomplete='off' "
         "autocapitalize='none' autocorrect='off' spellcheck='false' value='" +
         htmlEscape(cfg.spoofSerial) + "' placeholder='11821518'>"
         "</details>"
         // --- Trainer (erg): which FTMS machine the workout engine drives ----------------------
         "<details";
    if (!cfg.trainerNameFilter.empty()) h += " open";
    h += "><summary style='cursor:pointer;font-weight:600;margin:6px 0 10px'>Trainer / erg "
         "(advanced)</summary>"
         "<p class='hint'>Workouts erg-drive this trainer (matched by name). Leave blank to turn "
         "erg off.</p>";
    // FTMS candidates from the same scan become a tap-list that fills the field.
    {
        std::string trows;
        for (const auto& d : ds) {
            if (!d.isFtms || d.name.empty()) continue;
            const bool sel = (!cfg.trainerNameFilter.empty() && d.name == cfg.trainerNameFilter);
            trows += "<button type='button' class='dev";
            if (sel) trows += " sel";
            trows += "' data-tname='" + htmlEscape(d.name) + "' onclick='pickTrainer(this)'>"
                     "<span class='nm'>" + htmlEscape(d.name) + "</span>"
                     "<span class='badge' title='Fitness Machine Service'>trainer</span>"
                     "<span class='sig' title='" + std::to_string(d.rssi) + " dBm'>";
            const int bars = rssiBars(d.rssi);
            for (int b = 1; b <= 4; ++b) trows += (b <= bars) ? "<i class='on'></i>" : "<i></i>";
            trows += "</span></button>";
        }
        if (!trows.empty()) h += "<div class='devs'>" + trows + "</div>";
    }
    h += "<label for='trainer'>Trainer name</label>"
         "<input type='text' id='trainer' name='trainer' autocomplete='off' autocapitalize='none' "
         "autocorrect='off' spellcheck='false' value='" + htmlEscape(cfg.trainerNameFilter) +
         "' placeholder='e.g. Stages Bike'>"
         "</details>"
         "<button class='go' type='submit'>Save</button></form>"
         // Recovery: clear the saved source + identity back to the shipped defaults (for a tester who
         // mis-picks). Separate form; confirm before it reboots.
         "<form method='POST' action='/setup/reset' style='margin-top:18px' "
         "onsubmit='return confirm(\"Reset source and crank identity to defaults?\")'>"
         "<button class='reset' type='submit'>Reset to defaults</button></form>"
         "<script>function pick(b){"
         "document.getElementById('addr').value=b.getAttribute('data-addr');"
         "var ds=document.querySelectorAll('.dev[data-addr]');"
         "for(var i=0;i<ds.length;i++)ds[i].classList.remove('sel');"
         "b.classList.add('sel');}"
         "function pickTrainer(b){"
         "document.getElementById('trainer').value=b.getAttribute('data-tname');"
         "var ds=document.querySelectorAll('.dev[data-tname]');"
         "for(var i=0;i<ds.length;i++)ds[i].classList.remove('sel');"
         "b.classList.add('sel');}</script>";

    h += renderLogToggleFooter(logState);
    h += "</div>"  // .wrap
         "<nav class='nav'><a href='/'>Ride</a><a class='on' href='/setup'>Setup</a>"
         "<a href='/more'>More</a></nav>"
         "</body></html>";
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
        "padding:16px;color:#e8ecf4;background:#0f1320}h1{font-size:1.3rem}"
        ".ok{background:rgba(34,197,94,.12);border:1px solid rgba(34,197,94,.4);color:#22c55e;"
        "padding:10px 14px;border-radius:10px}li{margin:6px 0}a{color:#3b82f6}</style></head><body>"
        "<h1>Saved &#10003;</h1>"
        "<p class='ok'>Source set to <b>" + src + "</b>" +
        (cfg.singleSidedDouble ? " (single-sided &times;2)" : "") +
        " &mdash; the device is restarting to apply it.</p>"
        "<p>When it's back, the dashboard at <a href='/'>/</a> should show your source connected. "
        "Spin it to confirm the power tracks.</p></body></html>";
}

}  // namespace sb20proxy
