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
        // TOKENS-GEN:START — generated from design/tokens.json (run design/gen_tokens.py); do not edit by hand
        ":root{--bg:#0f1320;--card:#1a2030;--fg:#e8ecf4;--mut:#8b93a7;--ok:#22c55e;--accent:#3b82f6;--bad:#ef4444;--line:#1c2334;--chip2:#2a3142;--title:#151d2e;}"
        // TOKENS-GEN:END
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

// Reloads the page every 2 s while a scan runs, unless the rider is typing or has picked something
// that is not saved yet: a reload would throw that away. Pickers set window.sbDirty.
inline const char* webuiScanReloadJs() {
    return "<script>(function(){document.addEventListener('input',function(){window.sbDirty=1;});"
           "setInterval(function(){var a=document.activeElement;"
           "if(window.sbDirty||(a&&a.tagName==='INPUT'))return;location.reload();},2000);})();"
           "</script>";
}

// The board's web server is single-threaded, so a page can take a second or two. This shows at once
// that a tap registered: a bar across the top, and the tapped control dimmed. It covers links, form
// posts and POSTs made with fetch. GETs made with fetch are the 1 Hz status poll, so they are left
// out. Cleared on pageshow for when Safari restores a page from its back/forward cache.
inline const char* webuiBusyHtml() {
    return "<style>#sbBusy{display:none;position:fixed;top:0;left:0;right:0;height:3px;z-index:99;"
           "background:#3b82f6;animation:sbP .7s ease-in-out infinite alternate}"
           "@keyframes sbP{from{opacity:.25}to{opacity:1}}"
           ".sbW{opacity:.55!important;pointer-events:none}</style><div id='sbBusy'></div>"
           "<script>(function(){var b=document.getElementById('sbBusy'),t,w=[];"
           "function off(){b.style.display='none';clearTimeout(t);"
           "w.forEach(function(e){e.classList.remove('sbW');});w=[];}"
           "function on(e){b.style.display='block';clearTimeout(t);t=setTimeout(off,15000);"
           "if(e){e.classList.add('sbW');w.push(e);}}"
           "document.addEventListener('click',function(e){if(e.defaultPrevented)return;"
           "var a=e.target.closest&&e.target.closest('a[href]');if(!a)return;"
           "var h=a.getAttribute('href');"
           "if(h.charAt(0)==='#'||a.target||a.hasAttribute('download'))return;on(a);});"
           "document.addEventListener('submit',function(e){if(e.defaultPrevented)return;"
           "on(e.target.querySelector('button:not([type=button])'));});"
           "var f=window.fetch;if(f)window.fetch=function(u,o){"
           "if(!o||!o.method||o.method==='GET')return f.apply(this,arguments);on();"
           "var p=f.apply(this,arguments);p.then(off,off);return p;};"
           "window.addEventListener('pageshow',off);})();</script>";
}

}  // namespace sb20proxy
