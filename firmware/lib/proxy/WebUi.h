#pragma once
#include <string>

namespace sb20proxy {

// The shared web-UI design system — the palette + base layout + bottom-nav rules that every page
// (dashboard, /setup, /calibrate, /more) renders identically. Returned WITHOUT the <style> wrapper
// so a page concatenates it with its own page-specific rules inside a single <style> block:
//
//     "<style>" + webuiCss() + "<page-specific rules>" + "</style>"
//
// Keeping this in one place stops the palette/nav from drifting per page. Pure constant; the pages
// that use it are host-tested, so this is covered transitively.
inline const char* webuiCss() {
    return
        ":root{--bg:#0f1320;--card:#1a2030;--fg:#e8ecf4;--mut:#8b93a7;--ok:#22c55e;--accent:#3b82f6;"
        "--bad:#ef4444;--line:#1c2334;--chip2:#2a3142}"
        "*{box-sizing:border-box}"
        "body{margin:0;font-family:system-ui,-apple-system,sans-serif;background:var(--bg);"
        "color:var(--fg);overflow-x:hidden}"
        ".wrap{max-width:480px;margin:0 auto;padding:0 14px 84px}"
        ".tb{position:sticky;top:0;z-index:5;display:flex;align-items:center;justify-content:space-between;"
        "gap:8px;background:#151d2e;border-bottom:1px solid var(--line);padding:13px 14px;"
        "margin:0 -14px 12px;font-size:.98rem;font-weight:600}"
        ".hint{color:var(--mut);font-size:.85rem}"
        ".nav{position:fixed;left:0;right:0;bottom:0;display:flex;max-width:480px;margin:0 auto;"
        "background:#10141f;border-top:1px solid var(--line)}"
        ".nav a{flex:1;text-align:center;padding:12px 0;color:var(--mut);font-size:.85rem;text-decoration:none}"
        ".nav a.on{color:var(--accent)}";
}

}  // namespace sb20proxy
