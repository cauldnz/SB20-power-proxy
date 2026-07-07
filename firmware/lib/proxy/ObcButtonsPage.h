#pragma once
#include <string>

#include "Provisioning.h"    // urlDecode / htmlEscape (captive-portal helpers)
#include "Sb20ButtonMap.h"   // the per-button action binding + the selectable options
#include "WebUi.h"           // shared palette + base layout (webuiCss)

namespace sb20proxy {

// The pure (no-Arduino) half of the "SB20 button actions" web page: bind each of the 6 handlebar
// buttons to an action (an OBC re-broadcast, a local erg nudge, or none). Mirrors ConfigPage.h —
// HTML render + form parse here (host-tested); the HTTP route + NVS save + live-apply are the seam in
// src/WifiLink + main. Form keys: `btn0`..`btn5` (one per button, in sb20Buttons() order), value = an
// action token from sb20ActionOptions().

// Accept only KNOWN tokens (an unknown/garbage value keeps the slot's current binding — never persists
// junk to NVS). "none" is a known token.
inline bool sb20IsKnownToken(const std::string& token) {
    size_t n = 0;
    const Sb20ActionOption* o = sb20ActionOptions(n);
    for (size_t i = 0; i < n; ++i)
        if (token == o[i].token) return true;
    return false;
}

inline Sb20ButtonMap parseObcButtonsForm(const std::string& body) {
    Sb20ButtonMap m = Sb20ButtonMap::defaults();
    size_t pos = 0;
    while (pos <= body.size()) {
        const size_t amp = body.find('&', pos);
        const size_t end = (amp == std::string::npos) ? body.size() : amp;
        const std::string pair = body.substr(pos, end - pos);
        const size_t eq = pair.find('=');
        const std::string key = urlDecode(eq == std::string::npos ? pair : pair.substr(0, eq));
        const std::string val = urlDecode(eq == std::string::npos ? std::string() : pair.substr(eq + 1));
        if (key.size() == 4 && key.compare(0, 3, "btn") == 0) {
            const int i = key[3] - '0';
            if (i >= 0 && i < 6 && sb20IsKnownToken(val)) m.token[i] = val;
        }
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return m;
}

inline std::string renderObcButtonsPage(const Sb20ButtonMap& m, bool saved = false) {
    size_t nOpt = 0;
    const Sb20ActionOption* opts = sb20ActionOptions(nOpt);
    size_t nBtn = 0;
    const ShifterButton* btns = sb20Buttons(nBtn);
    std::string h =
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>SB20 Proxy &mdash; Button actions</title><style>";
    h += webuiCss();
    h += "select{width:100%;padding:10px;font-size:1rem;background:#0a0d16;color:var(--fg);"
         "border:1px solid var(--line);border-radius:9px;margin:4px 0 12px}"
         "label{font-weight:600;font-size:.9rem}"
         ".ok{background:rgba(34,197,94,.12);border:1px solid rgba(34,197,94,.4);color:var(--ok);"
         "padding:9px 12px;border-radius:9px;margin:8px 0}"
         "button.go{width:100%;padding:13px;font-size:1rem;font-weight:600;color:#fff;"
         "background:var(--accent);border:0;border-radius:10px;cursor:pointer}a{color:var(--accent)}"
         "</style></head><body><div class='wrap'>";
    h += "<h1>SB20 button actions</h1>";
    h += "<p style='color:var(--mut);font-size:.9rem'>Bind each handlebar button to an OpenBikeControl "
         "action (re-broadcast to a training app), a local erg-target nudge, or none. Applies live.</p>";
    if (saved) h += "<div class='ok'>Saved &#10003;</div>";
    h += "<form method='POST' action='/obc/buttons/save'>";
    for (size_t b = 0; b < nBtn; ++b) {
        h += "<label>";
        h += htmlEscape(shifterButtonName(btns[b]));
        h += "</label><select name='btn";
        h += (char)('0' + (int)b);
        h += "'>";
        for (size_t o = 0; o < nOpt; ++o) {
            h += "<option value='";
            h += opts[o].token;
            h += "'";
            if (m.token[b] == opts[o].token) h += " selected";
            h += ">";
            h += htmlEscape(opts[o].label);
            h += "</option>";
        }
        h += "</select>";
    }
    h += "<button class='go' type='submit'>Save</button></form>";
    h += "<p style='margin-top:14px'><a href='/obc'>&larr; OBC status</a> &middot; "
         "<a href='/'>Dashboard</a></p>";
    h += "</div></body></html>";
    return h;
}

}  // namespace sb20proxy
