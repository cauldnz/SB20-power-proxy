#pragma once
#include <algorithm>
#include <string>
#include <vector>

namespace sb20proxy {

// WiFi credentials as provisioned by the captive portal (or seeded from wifi_secret.h).
struct WifiCredentials {
    std::string ssid;
    std::string pass;
};

// One access point from a WiFi scan, as offered by the portal's network picker. `rssi` is the
// signal in dBm (closer to 0 = stronger); `secured` is false only for an open network. Filled
// by WifiLink (Arduino WiFi.* on the device); the rendering/dedup/sort below is pure & host-tested.
struct ScannedNet {
    std::string ssid;
    int rssi = -100;
    bool secured = true;
};

// The pure (no-Arduino) half of WiFi provisioning: HTML rendering, form parsing, and
// credential validation. Host-tested exactly like Status.h / renderStatusJson — the radio,
// SoftAP, and DNS glue live in src/net/WifiLink.cpp and are exercised only on the bench.

// URL-decode one application/x-www-form-urlencoded token: '+' -> space, %XX -> byte. A '%'
// without two following hex digits is passed through literally (lenient, never throws).
inline std::string urlDecode(const std::string& in) {
    auto hex = [](char h) -> int {
        if (h >= '0' && h <= '9') return h - '0';
        if (h >= 'a' && h <= 'f') return h - 'a' + 10;
        if (h >= 'A' && h <= 'F') return h - 'A' + 10;
        return -1;
    };
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (c == '+') {
            out += ' ';
        } else if (c == '%' && i + 2 < in.size()) {
            int hi = hex(in[i + 1]);
            int lo = hex(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
            } else {
                out += c;  // malformed escape — keep the '%'
            }
        } else {
            out += c;
        }
    }
    return out;
}

// Percent-encode a string for an application/x-www-form-urlencoded body (RFC 3986 unreserved kept;
// everything else -> %XX, including space -> %20). The inverse of urlDecode for our purposes
// (urlDecode also accepts '+' as space, which we don't emit). Used to rebuild a form body from the
// WebServer's already-parsed named args when the raw body isn't available (see WifiLink formBody).
inline std::string urlEncode(const std::string& in) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(in.size() * 3);
    for (unsigned char c : in) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

// Strip the NVS line delimiter '|' from a free-text field (a crank / device / meter name). Those
// names are user-supplied and land in '|'-delimited fields of RuntimeConfig::toLine — a literal '|'
// would inject an extra delimiter and shift every later field (mode/curve/calibrating) on reload.
inline std::string stripConfigDelims(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        if (c != '|') o += c;
    }
    return o;
}

// Parse an application/x-www-form-urlencoded body into credentials. Recognises the keys
// `ssid` and `pass` (also accepts `password`); unknown keys are ignored. Missing keys yield
// empty strings — validation is the caller's job (credValidationError).
inline WifiCredentials parseFormUrlEncoded(const std::string& body) {
    WifiCredentials c;
    size_t pos = 0;
    while (pos <= body.size()) {
        size_t amp = body.find('&', pos);
        size_t end = (amp == std::string::npos) ? body.size() : amp;
        std::string pair = body.substr(pos, end - pos);
        size_t eq = pair.find('=');
        std::string key = (eq == std::string::npos) ? pair : pair.substr(0, eq);
        std::string val = (eq == std::string::npos) ? std::string() : pair.substr(eq + 1);
        key = urlDecode(key);
        val = urlDecode(val);
        if (key == "ssid") {
            c.ssid = val;
        } else if (key == "pass" || key == "password") {
            c.pass = val;
        }
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return c;
}

// Validate credentials for an 802.11 / WPA-PSK join. Returns a human-readable reason the
// creds are unusable, or nullptr if they are valid. An empty password is allowed (open
// network); a non-empty one must be 8..63 chars (the WPA-PSK passphrase range).
inline const char* credValidationError(const WifiCredentials& c) {
    if (c.ssid.empty()) return "Network name (SSID) is required";
    if (c.ssid.size() > 32) return "Network name too long (max 32 characters)";
    if (!c.pass.empty() && (c.pass.size() < 8 || c.pass.size() > 63))
        return "Password must be 8-63 characters (or blank for an open network)";
    return nullptr;
}

inline bool validateCreds(const WifiCredentials& c) { return credValidationError(c) == nullptr; }

// Escape text for safe interpolation into HTML attribute / element content.
inline std::string htmlEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            case '\'': o += "&#39;"; break;
            default: o += c;
        }
    }
    return o;
}

// Footer linking to the diagnostic /log endpoint with an on/off toggle. `logState`: 1 = on,
// 0 = off, -1 = hide entirely. Pure, host-tested.
inline std::string renderLogToggleFooter(int logState) {
    if (logState < 0) return std::string();
    if (logState > 0) {
        return "<hr><p>Diagnostic log: <a href='/log'>/log</a> (on) — "
               "<a href='/log/off'>turn off</a></p>";
    }
    return "<hr><p>Diagnostic log: off — <a href='/log/on'>turn on</a></p>";
}

// Collapse a raw scan into the list the picker shows: drop blank/hidden SSIDs, merge duplicate
// names (mesh / multi-AP networks broadcast the same SSID from several radios — keep the
// strongest), then sort strongest signal first. Pure; the renderer calls it so the page is
// correct regardless of the order the radio returned. Stable: equal-RSSI ties keep scan order.
inline std::vector<ScannedNet> dedupeAndSortNetworks(const std::vector<ScannedNet>& in) {
    std::vector<ScannedNet> out;
    for (const auto& n : in) {
        if (n.ssid.empty()) continue;  // hidden network — not selectable from a list
        bool merged = false;
        for (auto& e : out) {
            if (e.ssid == n.ssid) {
                if (n.rssi > e.rssi) { e.rssi = n.rssi; e.secured = n.secured; }
                merged = true;
                break;
            }
        }
        if (!merged) out.push_back(n);
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const ScannedNet& a, const ScannedNet& b) { return a.rssi > b.rssi; });
    return out;
}

// Map a WiFi RSSI (dBm) to a 0..4 signal bucket for the picker's bar glyph; thresholds follow
// the usual phone-icon breakpoints (>= -55 excellent ... <= -85 unusable).
inline int rssiBars(int rssi) {
    if (rssi >= -55) return 4;
    if (rssi >= -65) return 3;
    if (rssi >= -75) return 2;
    if (rssi >= -85) return 1;
    return 0;
}

// Render the captive-portal setup page. The scanned networks become a *tap-list* — each row is a
// button that JS drops into the SSID field and jumps to the password. We chose this over a bare
// <datalist> (free-text whose dropdown is unreliable on iOS Safari, so it reads as "type it
// blind") and over <select> (native but can't show signal/lock and offers no inline manual
// entry). Each row shows signal strength + an open/secured lock and the list is sorted
// strongest-first. A plain SSID text field stays visible as the no-JS / hidden-network fallback,
// and a "Rescan" link (GET /rescan) re-runs the scan. `message` surfaces a validation error;
// `logState` adds the /log footer (-1 hides it); `scanning` marks an async rescan in flight (the
// page then auto-refreshes until results land). Pure string-building, so it is host-tested.
inline std::string renderProvisioningPage(const std::vector<ScannedNet>& networks = {},
                                          const std::string& message = std::string(),
                                          int logState = -1, bool scanning = false) {
    const std::vector<ScannedNet> nets = dedupeAndSortNetworks(networks);
    std::string h =
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>SB20 Proxy WiFi Setup</title>";
    if (scanning) h += "<meta http-equiv='refresh' content='2'>";  // poll until the scan lands
    h += "<style>"
         "body{font-family:system-ui,-apple-system,sans-serif;max-width:480px;margin:0 auto;"
         "padding:16px;color:#111;background:#fafafa}"
         "h1{font-size:1.3rem}"
         ".msg{background:#fdecea;border:1px solid #f5c2c7;color:#842029;padding:8px 12px;"
         "border-radius:6px}"
         ".row{display:flex;align-items:center;justify-content:space-between;margin:10px 0 4px}"
         ".nets{display:flex;flex-direction:column;gap:6px;margin:8px 0}"
         "button{font-family:inherit}"
         ".net{display:flex;align-items:center;gap:10px;width:100%;padding:12px;font-size:1rem;"
         "text-align:left;background:#fff;border:1px solid #ccc;border-radius:8px;cursor:pointer}"
         ".net:active{background:#eef}"
         ".net .nm{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
         ".lk{color:#888;font-size:.9rem}"
         ".sig{display:inline-flex;align-items:flex-end;gap:2px;height:14px}"
         ".sig i{width:4px;background:#ccc;border-radius:1px}"
         ".sig i:nth-child(1){height:5px}.sig i:nth-child(2){height:8px}"
         ".sig i:nth-child(3){height:11px}.sig i:nth-child(4){height:14px}"
         ".sig i.on{background:#2a8a3e}"
         "input{width:100%;box-sizing:border-box;padding:10px;font-size:1rem;"
         "border:1px solid #ccc;border-radius:8px;margin:4px 0 12px}"
         // The password field is a masked TEXT input (see the form below for why): WebKit/Blink
         // mask it with -webkit-text-security; .show clears the mask for the Show/Hide toggle.
         "#pass{-webkit-text-security:disc}"
         "#pass.show{-webkit-text-security:none}"
         ".reveal{padding:4px 10px;font-size:.85rem;background:#fff;border:1px solid #ccc;"
         "border-radius:6px;cursor:pointer;color:#2a6df4}"
         "label{font-weight:600;font-size:.9rem}"
         "button.go{width:100%;padding:12px;font-size:1rem;font-weight:600;color:#fff;"
         "background:#2a6df4;border:0;border-radius:8px;cursor:pointer}"
         "a{color:#2a6df4}"
         "</style></head><body>"
         "<h1>SB20 Proxy WiFi Setup</h1>";
    if (!message.empty()) h += "<p class='msg'>" + htmlEscape(message) + "</p>";

    h += "<div class='row'><label>Choose your WiFi network</label>"
         "<a href='/rescan'>";
    h += scanning ? "Scanning&hellip;" : "Rescan";
    h += "</a></div>";

    if (nets.empty()) {
        h += "<p>";
        h += scanning ? "Scanning for networks&hellip;"
                      : "No networks found &mdash; enter yours below or tap Rescan.";
        h += "</p>";
    } else {
        h += "<div class='nets'>";
        for (const auto& n : nets) {
            const int bars = rssiBars(n.rssi);
            // The SSID rides in a data- attribute (HTML-escaped); pick() reads it back as a plain
            // string and assigns it to the field's .value, so there is no JS-string injection.
            h += "<button type='button' class='net' data-ssid='" + htmlEscape(n.ssid) +
                 "' onclick='pick(this)'>";
            h += "<span class='nm'>" + htmlEscape(n.ssid) + "</span>";
            h += "<span class='lk' title='";
            h += n.secured ? "secured" : "open";
            h += "'>";
            h += n.secured ? "&#128274;" : "&#128275;";  // closed / open padlock
            h += "</span>";
            h += "<span class='sig' title='" + std::to_string(n.rssi) + " dBm'>";
            for (int b = 1; b <= 4; ++b) h += (b <= bars) ? "<i class='on'></i>" : "<i></i>";
            h += "</span></button>";
        }
        h += "</div>";
    }

    h += "<form method='POST' action='/save'>"
         "<label for='ssid'>Network name</label>"
         "<input id='ssid' name='ssid' autocomplete='off' autocapitalize='none' "
         "autocorrect='off' spellcheck='false' required>"
         "<div class='row'><label for='pass'>Password</label>"
         "<button type='button' class='reveal' onclick='revealPass(this)'>Show</button></div>"
         // The WiFi key is an EXISTING credential the rider already knows, not a new login. A
         // native type=password field makes iOS Safari (and the Captive Network Assistant
         // webview) pop "Use Strong Password" + a save prompt, and autocomplete='off' is ignored
         // for password fields. So this is a MASKED TEXT field: never classified as a credential,
         // so no overlay/save prompt fires; -webkit-text-security (WebKit + Blink, i.e. every
         // captive-portal browser) draws the dots, and the Show/Hide button above replaces the
         // native reveal. autocomplete='off' IS honoured for text fields. See forward-plan §8.
         "<input id='pass' name='pass' type='text' autocomplete='off' "
         "autocapitalize='off' autocorrect='off' spellcheck='false' "
         "placeholder='Leave blank if the network is open'>"
         "<button class='go' type='submit'>Save &amp; Connect</button></form>"
         "<script>function pick(b){var f=document.forms[0];"
         "f.ssid.value=b.getAttribute('data-ssid');f.pass.focus();}"
         "function revealPass(b){var p=document.getElementById('pass');"
         "b.textContent=p.classList.toggle('show')?'Hide':'Show';}</script>";

    h += renderLogToggleFooter(logState);
    h += "</body></html>";
    return h;
}

// The page shown right after the user saves credentials. Same look as the setup page; confirms the
// save and tells the user how to know it worked (the status-LED pulse and the IP on the OLED) —
// the device reboots immediately, so this page can't poll for the result itself. Pure/host-tested.
inline std::string renderSavedPage(const std::string& ssid) {
    return
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>SB20 Proxy &mdash; Saved</title>"
        "<style>"
        "body{font-family:system-ui,-apple-system,sans-serif;max-width:480px;margin:0 auto;"
        "padding:16px;color:#111;background:#fafafa}"
        "h1{font-size:1.3rem}"
        ".ok{background:#e7f5ec;border:1px solid #b7e0c4;color:#1d6b34;padding:10px 14px;"
        "border-radius:8px}"
        "li{margin:6px 0}"
        "</style></head><body>"
        "<h1>Saved &#10003;</h1>"
        "<p class='ok'>Connecting to <b>" + htmlEscape(ssid) +
        "</b> &mdash; the device is restarting now.</p>"
        "<p>You can close this page. To confirm it joined your network:</p>"
        "<ul>"
        "<li>the onboard <b>status LED</b> settles into a slow, steady pulse, and</li>"
        "<li>the <b>OLED</b> shows the device&rsquo;s IP address.</li>"
        "</ul>"
        "<p><b>Next:</b> open the dashboard at that IP (or <code>http://sb20proxy.local/</code>) and tap "
        "<b>&#9881; Source</b> to choose the power meter or crank it should read.</p>"
        "</body></html>";
}

}  // namespace sb20proxy
