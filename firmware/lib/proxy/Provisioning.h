#pragma once
#include <string>
#include <vector>

namespace sb20proxy {

// WiFi credentials as provisioned by the captive portal (or seeded from wifi_secret.h).
struct WifiCredentials {
    std::string ssid;
    std::string pass;
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

// Render the captive-portal setup page: an SSID field (with a datalist of any scanned
// networks for convenience) and a password field, posting to /save. `message` surfaces a
// validation error back to the user. Pure string-building, so it is host-tested.
inline std::string renderProvisioningPage(const std::vector<std::string>& networks = {},
                                          const std::string& message = std::string()) {
    std::string h =
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>SB20 Proxy WiFi Setup</title></head><body>"
        "<h1>SB20 Proxy WiFi Setup</h1>";
    if (!message.empty()) h += "<p class='msg'>" + htmlEscape(message) + "</p>";
    h += "<form method='POST' action='/save'>"
         "<label>Network<br><input name='ssid' list='nets' autofocus required></label>"
         "<datalist id='nets'>";
    for (const auto& n : networks) {
        if (n.empty()) continue;
        h += "<option value='" + htmlEscape(n) + "'>";
    }
    h += "</datalist><br>"
         "<label>Password<br><input name='pass' type='password'></label><br>"
         "<button type='submit'>Save &amp; Connect</button></form>"
         "</body></html>";
    return h;
}

}  // namespace sb20proxy
