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
:root{--bg:#0f1320;--card:#1a2030;--fg:#e8ecf4;--mut:#8b93a7;--ok:#22c55e;--bad:#ef4444}
*{box-sizing:border-box}
body{margin:0 auto;font-family:system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--fg);max-width:560px;padding:16px}
header{display:flex;align-items:center;justify-content:space-between;margin-bottom:12px}
h1{font-size:1.1rem;margin:0;font-weight:600}
.dot{width:10px;height:10px;border-radius:50%;background:var(--bad);display:inline-block;margin-left:6px;vertical-align:middle}
.dot.on{background:var(--ok)}
.big{display:flex;gap:12px;margin-bottom:12px}
.card{background:var(--card);border-radius:14px;padding:16px;flex:1}
.lbl{color:var(--mut);font-size:.75rem;text-transform:uppercase;letter-spacing:.05em}
.val{font-size:2.4rem;font-weight:700;line-height:1.1;margin-top:2px}
.val small{font-size:1rem;color:var(--mut);font-weight:500}
canvas{width:100%;height:120px;background:var(--card);border-radius:14px;margin-bottom:12px;display:block}
.stats{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.stat{background:var(--card);border-radius:10px;padding:10px 12px;font-size:.9rem}
.stat b{display:block;color:var(--mut);font-size:.7rem;text-transform:uppercase;letter-spacing:.04em;margin-bottom:2px}
.flow{display:flex;align-items:center;justify-content:center;gap:14px;background:var(--card);border-radius:14px;padding:10px;margin-bottom:12px;font-size:1rem}
.flow .io{color:var(--mut)}.flow .io b{color:var(--fg);font-size:1.3rem}.flow .arrow{color:#3b82f6;font-size:1.3rem}
a.set{color:#3b82f6;text-decoration:none;font-size:.85rem;border:1px solid #2b3650;padding:5px 10px;border-radius:8px}
</style></head><body>
<header><h1>SB20 Proxy <span id='dot' class='dot'></span></h1><a class='set' href='/setup'>&#9881; Source</a></header>
<div class='big'>
<div class='card'><div class='lbl'>Power</div><div class='val'><span id='pw'>--</span> <small>W</small></div></div>
<div class='card'><div class='lbl'>Cadence</div><div class='val'><span id='cad'>--</span> <small>rpm</small></div></div>
</div>
<div class='flow'><span class='io'>METER IN <b id='in'>--</b> W</span><span class='arrow'>&#8594;</span><span class='io'>CRANK OUT <b id='out'>--</b> W</span></div>
<canvas id='chart'></canvas>
<div class='stats'>
<div class='stat'><b>Source</b><span id='src'>--</span></div>
<div class='stat'><b>Balance L/R</b><span id='bal'>--</span></div>
<div class='stat'><b>WiFi</b><span id='rssi'>--</span></div>
<div class='stat'><b>Uptime</b><span id='up'>--</span></div>
<div class='stat'><b>Forwarded</b><span id='fwd'>--</span></div>
<div class='stat'><b>Firmware</b><span id='fw'>--</span></div>
</div>
<script>
var $=function(i){return document.getElementById(i)};
var hist=[],MAX=90;
function fmtUp(ms){var s=Math.floor(ms/1000),h=Math.floor(s/3600);s%=3600;var m=Math.floor(s/60);s%=60;return (h?h+'h ':'')+(m||h?m+'m ':'')+s+'s';}
function draw(){var c=$('chart'),r=window.devicePixelRatio||1,w=c.clientWidth,h=c.clientHeight;c.width=w*r;c.height=h*r;var x=c.getContext('2d');x.scale(r,r);x.clearRect(0,0,w,h);if(hist.length<2)return;var mx=100;for(var i=0;i<hist.length;i++){if(hist[i]>mx)mx=hist[i];}x.beginPath();for(var i=0;i<hist.length;i++){var px=i/(MAX-1)*w,py=h-8-(hist[i]/mx)*(h-16);if(i){x.lineTo(px,py);}else{x.moveTo(px,py);}}x.strokeStyle='#3b82f6';x.lineWidth=2;x.stroke();x.lineTo((hist.length-1)/(MAX-1)*w,h);x.lineTo(0,h);x.closePath();x.fillStyle='rgba(59,130,246,.15)';x.fill();}
function tick(){fetch('/status',{cache:'no-store'}).then(function(r){return r.json();}).then(function(d){
$('dot').classList.add('on');
$('pw').textContent=d.power_w;$('cad').textContent=(d.cadence_rpm<0?'--':d.cadence_rpm);
$('in').textContent=(d.src_power_w===undefined?'--':d.src_power_w);$('out').textContent=d.power_w;
$('src').textContent=(d.src_name?d.src_name+' ('+d.source+')':d.source);$('rssi').textContent=d.rssi+' dBm';
$('bal').textContent=(d.balance_pct===undefined||d.balance_pct<0?'--':('L'+d.balance_pct+' / R'+(100-d.balance_pct)));
$('up').textContent=fmtUp(d.ms);$('fwd').textContent=d.forwarded;$('fw').textContent=d.fw;
hist.push(d.power_w);if(hist.length>MAX){hist.shift();}draw();
}).catch(function(){$('dot').classList.remove('on');});}
setInterval(tick,1000);tick();window.addEventListener('resize',draw);
</script></body></html>)HTML";
}

}  // namespace sb20proxy
