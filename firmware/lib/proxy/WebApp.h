#pragma once

namespace sb20proxy {

// The streaming dashboard served at GET / (and /ui). A self-contained, dependency-free page that
// POLLS the status JSON at GET /status once a second and renders power/cadence/balance + a live
// chart entirely in the browser — so the ESP32 only serves the small JSON it already serves and all
// the UI cost lives on the phone. A header link goes to /setup (pick the source). Static (no
// device-side templating), so it is just a constant; a host test guards the essentials survive edits.
inline const char* appPageHtml() {
    return R"HTML(<!DOCTYPE html><html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>SB20 Proxy</title>
<style>
:root{--bg:#0f1320;--card:#1a2030;--fg:#e8ecf4;--mut:#8b93a7;--ok:#22c55e;--bad:#ef4444;--accent:#3b82f6;--line:#1c2334;--chip2:#2a3142}
*{box-sizing:border-box}
body{margin:0;font-family:system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--fg);overflow-x:hidden}
.wrap{max-width:480px;margin:0 auto;padding:0 14px 84px}
.ttl{position:sticky;top:0;z-index:5;width:100%;display:flex;align-items:center;justify-content:space-between;gap:8px;
 background:#151d2e;border:0;border-bottom:1px solid var(--line);color:var(--fg);
 padding:12px 4px;margin:0 -14px 0;width:calc(100% + 28px);font-size:.95rem;cursor:pointer;font-family:inherit}
.ids{display:flex;align-items:center;gap:7px;padding-left:14px;min-width:0;overflow:hidden;white-space:nowrap;text-overflow:ellipsis}
.gd{width:9px;height:9px;border-radius:50%;background:var(--bad);flex-shrink:0}
.gd.on{background:var(--ok)}
.arr{color:var(--accent)}
.chev{color:var(--mut);font-size:1rem;padding-right:18px;transition:transform .15s}
.ttl.open .chev{transform:rotate(180deg)}
.details{display:none;margin-top:12px}
.details.open{display:block}
.cols{display:flex;gap:10px}
.col{flex:1;min-width:0;background:var(--card);border-radius:12px;padding:12px}
.badge{display:inline-block;font-size:.7rem;font-weight:600;padding:2px 9px;border-radius:7px;letter-spacing:.04em}
.bin{background:rgba(34,197,94,.16);color:var(--ok)}
.bout{background:rgba(59,130,246,.18);color:var(--accent)}
.cn{font-size:.85rem;font-weight:600;margin:8px 0 6px;word-break:break-word}
.cp{font-size:1.5rem;font-weight:700;line-height:1}.cp small{font-size:.8rem;color:var(--mut);font-weight:500}
.cs{font-size:.8rem;color:var(--mut);margin-top:4px}
.dev{margin-top:10px;font-size:.78rem;color:var(--mut);line-height:1.7;word-break:break-word}
.dev b{color:var(--fg);font-weight:500}
.power{text-align:center;margin-top:18px}
.plabel{font-size:.78rem;color:var(--mut);letter-spacing:.14em}
.pbig{font-size:4.6rem;font-weight:700;line-height:1;margin-top:2px}.pbig small{font-size:1.2rem;color:var(--mut);font-weight:500}
canvas{width:100%;height:96px;display:block;margin-top:6px}
.chips{display:flex;gap:10px;margin-top:14px}
.chip{flex:1;min-width:0;background:var(--card);border-radius:12px;padding:12px 14px}
.ck{font-size:.78rem;color:var(--mut)}
.cv{font-size:1.7rem;font-weight:700;margin-top:2px}.cv small{font-size:.85rem;color:var(--mut);font-weight:500}
.nav{position:fixed;left:0;right:0;bottom:0;display:flex;max-width:480px;margin:0 auto;background:#10141f;border-top:1px solid var(--line)}
.nav a{flex:1;text-align:center;padding:12px 0;color:var(--mut);font-size:.85rem;text-decoration:none}
.nav a.on{color:var(--accent)}
</style></head><body>
<div class='wrap'>
<button class='ttl' id='ttl' onclick='toggle()'>
<span class='ids'><span class='gd' id='dIn'></span> <span id='nIn'>&hellip;</span>
<span class='arr'>&rarr;</span> <span class='gd' id='dOut'></span> <span id='nOut'>&hellip;</span></span>
<span class='chev'>&#8964;</span></button>
<div class='details' id='details'>
<div class='cols'>
<div class='col'><span class='badge bin'>IN</span><div class='cn' id='dnIn'>&mdash;</div>
<div class='cp'><span id='dpIn'>--</span><small> W</small></div><div class='cs'><span id='dcIn'>--</span> rpm</div></div>
<div class='col'><span class='badge bout'>OUT</span><div class='cn' id='dnOut'>&mdash;</div>
<div class='cp'><span id='dpOut'>--</span><small> W</small></div><div class='cs'><span id='dcOut'>--</span> rpm</div></div>
</div>
<div class='dev'>WiFi <b id='rssi'>--</b> &middot; up <b id='up'>--</b> &middot; heap <b id='heap'>--</b> &middot; fwd <b id='fwd'>--</b> &middot; <b id='fw'>--</b></div>
</div>
<div class='power'><div class='plabel'>POWER</div><div class='pbig'><span id='pw'>--</span><small> W</small></div></div>
<canvas id='chart'></canvas>
<div class='chips'>
<div class='chip'><div class='ck'>Cadence</div><div class='cv'><span id='cad'>--</span><small> rpm</small></div></div>
<div class='chip'><div class='ck'>Balance</div><div class='cv' id='bal'>--</div></div>
</div>
</div>
<nav class='nav'><a class='on' href='/'>Ride</a><a href='/setup'>Setup</a><a href='/more'>More</a></nav>
<script>
var $=function(i){return document.getElementById(i)};
var hist=[],MAX=90;
function toggle(){$('ttl').classList.toggle('open');$('details').classList.toggle('open');}
function fmtUp(ms){var s=Math.floor(ms/1000),h=Math.floor(s/3600);s%=3600;var m=Math.floor(s/60);s%=60;return (h?h+'h ':'')+(m||h?m+'m ':'')+s+'s';}
function draw(){var c=$('chart'),r=window.devicePixelRatio||1,w=c.clientWidth,h=c.clientHeight;c.width=w*r;c.height=h*r;var x=c.getContext('2d');x.scale(r,r);x.clearRect(0,0,w,h);if(hist.length<2)return;var mx=100;for(var i=0;i<hist.length;i++){if(hist[i]>mx)mx=hist[i];}x.beginPath();for(var i=0;i<hist.length;i++){var px=i/(MAX-1)*w,py=h-8-(hist[i]/mx)*(h-16);if(i){x.lineTo(px,py);}else{x.moveTo(px,py);}}x.strokeStyle='#3b82f6';x.lineWidth=2;x.stroke();x.lineTo((hist.length-1)/(MAX-1)*w,h);x.lineTo(0,h);x.closePath();x.fillStyle='rgba(59,130,246,.16)';x.fill();}
function tick(){fetch('/status',{cache:'no-store'}).then(function(r){return r.json();}).then(function(d){
var inOn=(d.source==='connected'||d.source==='mock');
$('dIn').classList.toggle('on',inOn);$('dOut').classList.add('on');
$('nIn').textContent=(d.src_name||(d.source==='mock'?'mock':d.source||'searching'));
$('nOut').textContent=(d.identity||'crank');
$('pw').textContent=d.power_w;$('cad').textContent=(d.cadence_rpm<0?'--':d.cadence_rpm);
$('bal').textContent=(d.balance_pct===undefined||d.balance_pct<0?'--':(d.balance_pct+' / '+(100-d.balance_pct)));
$('dnIn').textContent=(d.src_name||d.source||'--');$('dpIn').textContent=(d.src_power_w===undefined?'--':d.src_power_w);$('dcIn').textContent=(d.src_cadence_rpm<0?'--':d.src_cadence_rpm);
$('dnOut').textContent=(d.identity||'--');$('dpOut').textContent=d.power_w;$('dcOut').textContent=(d.cadence_rpm<0?'--':d.cadence_rpm);
$('rssi').textContent=d.rssi+' dBm';$('up').textContent=fmtUp(d.ms);$('heap').textContent=Math.round(d.heap/1024)+'k';$('fwd').textContent=d.forwarded;$('fw').textContent=(d.identity?d.version:d.fw);
hist.push(d.power_w);if(hist.length>MAX){hist.shift();}draw();
}).catch(function(){$('dIn').classList.remove('on');$('dOut').classList.remove('on');});}
setInterval(tick,1000);tick();window.addEventListener('resize',draw);
</script></body></html>)HTML";
}

// The Settings / "More" page (GET /more) — the third bottom-nav tab. A status summary + a nav hub:
// it reads live mode / identity / source / firmware / uptime from the same /status JSON the dashboard
// polls, and each row links to the page that edits it (source + identity + mode on /setup, ride mode
// on /wifi/off, the meter corrector on /calibrate, the diagnostic on /report). Unlike the LCD's
// QR hand-off, "Send a report" just opens /report — on the phone you're already there. Pure constant,
// host-tested like appPageHtml.
inline const char* settingsPageHtml() {
    return R"HTML(<!DOCTYPE html><html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>SB20 Proxy &mdash; Settings</title>
<style>
:root{--bg:#0f1320;--card:#1a2030;--fg:#e8ecf4;--mut:#8b93a7;--ok:#22c55e;--accent:#3b82f6;--line:#1c2334}
*{box-sizing:border-box}
body{margin:0;font-family:system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--fg);overflow-x:hidden}
.wrap{max-width:480px;margin:0 auto;padding:0 14px 84px}
.tb{position:sticky;top:0;z-index:5;background:#151d2e;border-bottom:1px solid var(--line);
 padding:13px 14px;margin:0 -14px 6px;font-size:.98rem;font-weight:600;
 display:flex;align-items:center;justify-content:space-between}
.tb small{font-weight:400;color:var(--mut);font-size:.8rem}
.row{display:flex;align-items:center;justify-content:space-between;gap:10px;padding:14px 2px;
 border-bottom:1px solid var(--line);color:var(--fg);text-decoration:none}
.lbl{font-size:.98rem}
.right{display:flex;align-items:center;gap:8px;min-width:0}
.val{font-size:.9rem;color:var(--mut);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:190px}
.val.ok{color:var(--ok)}
.chev{color:var(--mut);font-size:1.1rem;flex-shrink:0}
.foot{text-align:center;font-size:.8rem;color:var(--mut);padding:14px 0}
.nav{position:fixed;left:0;right:0;bottom:0;display:flex;max-width:480px;margin:0 auto;background:#10141f;border-top:1px solid var(--line)}
.nav a{flex:1;text-align:center;padding:12px 0;color:var(--mut);font-size:.85rem;text-decoration:none}
.nav a.on{color:var(--accent)}
</style></head><body><div class='wrap'>
<div class='tb'><span>Settings</span><small id='host'>sb20proxy</small></div>
<a class='row' href='/setup'><span class='lbl'>Mode</span><span class='right'><span class='val' id='mode'>&hellip;</span><span class='chev'>&#8250;</span></span></a>
<a class='row' href='/setup'><span class='lbl'>Identity</span><span class='right'><span class='val' id='ident'>&hellip;</span><span class='chev'>&#8250;</span></span></a>
<a class='row' href='/setup'><span class='lbl'>Source</span><span class='right'><span class='val ok' id='src'>&hellip;</span><span class='chev'>&#8250;</span></span></a>
<a class='row' href='/calibrate'><span class='lbl'>Calibrate a meter</span><span class='chev'>&#8250;</span></a>
<a class='row' href='/wifi/off'><span class='lbl'>Ride mode &mdash; WiFi off</span><span class='chev'>&#8250;</span></a>
<a class='row' href='/report'><span class='lbl'>Send a report</span><span class='chev'>&#8250;</span></a>
<div class='row' style='border-bottom:none'><span class='lbl'>Firmware</span><span class='val' id='ver'>&hellip;</span></div>
<div class='foot'>sb20proxy.local &middot; up <span id='up'>&mdash;</span></div>
</div>
<nav class='nav'><a href='/'>Ride</a><a href='/setup'>Setup</a><a class='on' href='/more'>More</a></nav>
<script>
var $=function(i){return document.getElementById(i)};
function fmtUp(ms){var s=Math.floor(ms/1000),h=Math.floor(s/3600);s%=3600;var m=Math.floor(s/60);s%=60;return (h?h+'h ':'')+(m||h?m+'m ':'')+s+'s';}
fetch('/status',{cache:'no-store'}).then(function(r){return r.json();}).then(function(d){
$('mode').textContent=(d.mode==='corrector'?'Corrector':'Crank spoof');
$('ident').textContent=(d.identity||'--');
$('src').textContent=(d.src_name||(d.source==='mock'?'mock':d.source||'searching'));
$('ver').textContent='v'+(d.version||'?');
$('up').textContent=fmtUp(d.ms);
}).catch(function(){});
</script></body></html>)HTML";
}

// "Ride mode" — turn WiFi off so the C3's radio is BLE-only for the ride (WiFi+BLE+OLED coex is
// what intermittently hangs it under load). Opt-in + reversible: a power-cycle brings WiFi back.
// The confirm page POSTs to /wifi/off; the seam (WifiLink) shuts the radio down after replying.
// Pure constants — host-tested like appPageHtml.
inline const char* rideModeConfirmHtml() {
    return R"HTML(<!DOCTYPE html><html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>SB20 Proxy &mdash; Ride mode</title>
<style>body{font-family:system-ui,-apple-system,sans-serif;max-width:480px;margin:0 auto;padding:16px;color:#111;background:#fafafa}h1{font-size:1.3rem}
button.go{width:100%;padding:12px;font-size:1rem;font-weight:600;color:#fff;background:#2a6df4;border:0;border-radius:8px;cursor:pointer}a{color:#2a6df4}.hint{color:#666}</style></head><body>
<h1>Ride mode</h1>
<p>This turns <b>WiFi off</b> so the board runs Bluetooth-only for your ride &mdash; it frees the radio and
avoids the rare mid-ride freeze under WiFi+BLE load. The power proxy keeps working the whole time.</p>
<p class='hint'>Do this <b>after</b> you've checked the dashboard shows your source connected. To bring WiFi
back (for the dashboard or updates), just <b>power-cycle</b> the board.</p>
<form method='POST' action='/wifi/off'><button class='go' type='submit'>Turn WiFi off &amp; ride</button></form>
<p><a href='/'>Cancel &mdash; back to the dashboard</a></p>
</body></html>)HTML";
}

inline const char* rideModeDoneHtml() {
    return R"HTML(<!DOCTYPE html><html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>SB20 Proxy &mdash; WiFi off</title>
<style>body{font-family:system-ui,-apple-system,sans-serif;max-width:480px;margin:0 auto;padding:16px;color:#111;background:#fafafa}h1{font-size:1.3rem}
.ok{background:#e7f5ec;border:1px solid #b7e0c4;color:#1d6b34;padding:10px 14px;border-radius:8px}</style></head><body>
<h1>WiFi off &mdash; ride mode &#10003;</h1>
<p class='ok'>The board is now Bluetooth-only. Your power source keeps relaying to the SB20 the whole ride.</p>
<p>This page won't refresh (WiFi is off). <b>Power-cycle</b> the board when you want WiFi back for the
dashboard or an update.</p>
</body></html>)HTML";
}

// The tester report "review &amp; send" page (GET /report). CONSENT-FIRST: it fetches the raw /diag
// report (config + status + the meter's raw CPS frames), shows it so the tester can READ exactly what
// they'd send, then lets them Download it / Copy it / open an email &mdash; nothing leaves the device
// until they choose to. The raw /diag stays plain text (parse_diag's input); this only wraps it for the
// tester. Pure constant &mdash; host-tested like appPageHtml.
inline const char* diagReportPageHtml() {
    return R"HTML(<!DOCTYPE html><html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>SB20 Proxy &mdash; Send a report</title>
<style>
:root{--bg:#0f1320;--card:#1a2030;--fg:#e8ecf4;--mut:#8b93a7;--accent:#3b82f6}
*{box-sizing:border-box}
body{margin:0 auto;font-family:system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--fg);max-width:560px;padding:16px}
h1{font-size:1.2rem;margin:0 0 8px}
.note{background:var(--card);border-radius:12px;padding:12px 14px;font-size:.85rem;color:var(--mut);margin-bottom:12px}
.note b{color:var(--fg)}
pre{background:#0a0d16;border:1px solid #2b3650;border-radius:10px;padding:12px;font-size:.72rem;line-height:1.35;white-space:pre-wrap;word-break:break-word;max-height:46vh;overflow:auto;margin:0 0 12px}
.row{display:flex;gap:8px;flex-wrap:wrap}
button,a.btn{flex:1;min-width:120px;text-align:center;padding:12px;font-size:.95rem;font-weight:600;border-radius:10px;border:0;cursor:pointer;text-decoration:none}
.primary{background:var(--accent);color:#fff}
.ghost{background:var(--card);color:var(--fg);border:1px solid #2b3650}
.steps{font-size:.85rem;color:var(--mut);margin:12px 0}
.steps b{color:var(--fg)}
a.back{color:var(--mut);font-size:.8rem}
#msg{font-size:.8rem;color:#22c55e;min-height:1.1em;margin-top:6px}
</style></head><body>
<h1>Send us a report</h1>
<div class='note'>This is <b>exactly what you'd send us</b> &mdash; your meter's Bluetooth signal plus the
board's config and status. <b>No location or personal data.</b> Nothing leaves the board until you tap
Download or Email below &mdash; review it first.</div>
<div class='steps'><b>1.</b> Read it below &nbsp; <b>2.</b> tap <b>Download</b> &nbsp; <b>3.</b> email us
the file (attach it) at the address we gave you.</div>
<pre id='rpt'>loading&hellip;</pre>
<div class='row'>
<button class='primary' onclick='dl()'>&#11015; Download .txt</button>
<button class='ghost' onclick='cp()'>Copy</button>
<a class='btn ghost' id='mail' href='#'>&#9993; Email us</a>
</div>
<div id='msg'></div>
<p style='margin-top:14px'><a class='back' href='/'>&larr; Back to dashboard</a> &nbsp;&middot;&nbsp;
<a class='back' href='/diag'>view raw</a></p>
<script>
var rpt='';
function fname(){return 'sb20-report-'+new Date().toISOString().replace(/[:.]/g,'-').slice(0,19)+'.txt';}
fetch('/diag',{cache:'no-store'}).then(function(r){return r.text();}).then(function(t){
rpt=t;document.getElementById('rpt').textContent=t;
document.getElementById('mail').href='mailto:?subject='+encodeURIComponent('SB20 proxy report')
+'&body='+encodeURIComponent('My SB20 proxy diagnostic is attached (download it first, then attach the .txt).');
}).catch(function(){document.getElementById('rpt').textContent='(could not load the report — reload this page)';});
function dl(){var b=new Blob([rpt],{type:'text/plain'});var a=document.createElement('a');
a.href=URL.createObjectURL(b);a.download=fname();document.body.appendChild(a);a.click();a.remove();
document.getElementById('msg').textContent='Downloaded — now email us the file (attach it).';}
function cp(){if(navigator.clipboard){navigator.clipboard.writeText(rpt).then(function(){
document.getElementById('msg').textContent='Copied — paste it into a message to us.';});}}
</script></body></html>)HTML";
}

}  // namespace sb20proxy
