"""The web layer: the live-JSON payload (pure) and the static dashboard page.

`render_live()` and `workout_json()` are pure transforms (LiveState snapshot +
RideDirector -> the dict the browser polls), so they're host-tested. APP_HTML is a
self-contained, dependency-free page that polls GET /api/live once a second and
renders everything in the browser — same philosophy as the ESP32 /ui: the device
(or the laptop) just serves small JSON; the phone does the work.
"""

from __future__ import annotations

from typing import Any

from .director import RideDirector, Workout


def workout_json(workout: Workout) -> dict[str, Any]:
    return {
        "name": workout.name,
        "total_s": workout.total_s,
        "segments": [
            {
                "label": s.label,
                "duration_s": s.duration_s,
                "power_w": s.power_w,
                "cadence_rpm": s.cadence_rpm,
                "note": s.note,
            }
            for s in workout.segments
        ],
    }


def render_live(snapshot: dict[str, Any], director: RideDirector) -> dict[str, Any]:
    """Combine a LiveState snapshot with the director's view of the workout."""
    started = bool(snapshot.get("ride_started"))
    elapsed = snapshot.get("ride_elapsed_s")
    ds = director.state_at(
        elapsed if (started and elapsed is not None) else 0.0, started=started
    )
    seg = ds.segment
    nxt = ds.next_segment
    out = dict(snapshot)
    out["director"] = {
        "workout": director.workout.name,
        "started": ds.started,
        "finished": ds.finished,
        "seg_index": ds.seg_index,
        "n_segments": len(director.workout.segments),
        "label": seg.label if seg else ("Done" if ds.finished else "Ready"),
        "note": seg.note if seg else "",
        "target_power_w": seg.power_w if seg else None,
        "target_cadence_rpm": seg.cadence_rpm if seg else None,
        "seg_elapsed_s": round(ds.seg_elapsed_s, 1),
        "seg_remaining_s": round(ds.seg_remaining_s, 1),
        "seg_duration_s": seg.duration_s if seg else 0,
        "total_elapsed_s": round(ds.total_elapsed_s, 1),
        "total_remaining_s": round(ds.total_remaining_s, 1),
        "total_s": director.workout.total_s,
        "next_label": nxt.label if nxt else None,
        "next_power_w": nxt.power_w if nxt else None,
        "next_cadence_rpm": nxt.cadence_rpm if nxt else None,
    }
    return out


APP_HTML = r"""<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SB20 Ride Director</title>
<style>
:root{--bg:#0f1320;--card:#1a2030;--fg:#e8ecf4;--mut:#8b93a7;--accent:#3b82f6;
--ok:#22c55e;--warn:#f59e0b;--bad:#ef4444}
*{box-sizing:border-box}
body{margin:0 auto;font-family:system-ui,-apple-system,sans-serif;background:var(--bg);
color:var(--fg);max-width:680px;padding:14px}
header{display:flex;align-items:center;justify-content:space-between;margin-bottom:10px}
h1{font-size:1.05rem;margin:0;font-weight:600}
.dot{width:10px;height:10px;border-radius:50%;background:var(--bad);display:inline-block;
margin-left:6px;vertical-align:middle}
.dot.on{background:var(--ok)}
.sub{color:var(--mut);font-size:.78rem}
.target{background:var(--card);border-radius:16px;padding:18px;margin-bottom:12px;text-align:center}
.target .seg{color:var(--accent);font-size:1.1rem;font-weight:700;letter-spacing:.04em;
text-transform:uppercase}
.target .big{font-size:3.6rem;font-weight:800;line-height:1.05;margin:4px 0}
.target .big small{font-size:1.3rem;color:var(--mut);font-weight:600}
.target .cad{font-size:1.2rem;color:var(--mut)}
.target .count{font-size:1.5rem;font-weight:700;margin-top:6px}
.target .note{color:var(--mut);font-size:.9rem;margin-top:6px;min-height:1.1em}
.target .next{color:var(--mut);font-size:.85rem;margin-top:8px}
.bar{height:8px;background:#0b0e18;border-radius:5px;overflow:hidden;margin-top:10px}
.bar>div{height:100%;background:var(--accent);width:0%;transition:width .4s linear}
.btn{display:inline-block;border:0;border-radius:10px;padding:12px 22px;font-size:1rem;
font-weight:700;cursor:pointer;margin-top:12px;color:#fff;background:var(--accent);font-family:inherit}
.btn.stop{background:#374151}
.meters{display:flex;gap:10px;margin-bottom:12px;flex-wrap:wrap}
.meter{background:var(--card);border-radius:12px;padding:12px 14px;flex:1;min-width:120px}
.meter .nm{color:var(--mut);font-size:.72rem;text-transform:uppercase;letter-spacing:.05em}
.meter .pw{font-size:2rem;font-weight:700}
.meter .pw small{font-size:.9rem;color:var(--mut)}
.meter .cd{color:var(--mut);font-size:.9rem}
.meter.delta .pw{color:var(--warn)}
.hold{font-weight:700}.hold.ok{color:var(--ok)}.hold.off{color:var(--warn)}
canvas{width:100%;height:130px;background:var(--card);border-radius:12px;display:block;margin-bottom:12px}
.tl{display:flex;gap:2px;height:22px;border-radius:6px;overflow:hidden;margin-bottom:10px}
.tl>div{display:flex;align-items:center;justify-content:center;font-size:.6rem;color:#0b0e18;
font-weight:700;overflow:hidden;white-space:nowrap}
.foot{color:var(--mut);font-size:.75rem;display:flex;justify-content:space-between;gap:8px;flex-wrap:wrap}
</style></head><body>
<header><h1>SB20 Ride Director <span id="dot" class="dot"></span></h1>
<span id="mode" class="sub"></span></header>

<div class="target">
  <div class="seg" id="seg">READY</div>
  <div class="big"><span id="tp">--</span> <small>W target</small></div>
  <div class="cad" id="tc"></div>
  <div class="count" id="count"></div>
  <div class="bar"><div id="segbar"></div></div>
  <div class="note" id="note">Press Start when you're spinning.</div>
  <div class="next" id="next"></div>
  <button class="btn" id="btn" onclick="toggle()">Start ride</button>
</div>

<div class="tl" id="tl"></div>
<div class="meters" id="meters"></div>
<canvas id="chart"></canvas>
<div class="foot">
  <span id="status"></span><span id="total"></span>
</div>

<script>
var $=function(i){return document.getElementById(i)};
var COLORS=['#3b82f6','#22c55e','#f59e0b','#a855f7'];
var hist={}, MAX=120, started=false, targetPow=null, total=0, wb=null;

function mmss(s){s=Math.max(0,Math.round(s));var m=Math.floor(s/60);return m+':'+String(s%60).padStart(2,'0');}

function loadWorkout(){fetch('/api/workout',{cache:'no-store'}).then(function(r){return r.json();}).then(function(w){
  total=w.total_s; wb=w; var tl=$('tl'); tl.innerHTML='';
  w.segments.forEach(function(s,i){var d=document.createElement('div');
    d.style.flex=s.duration_s; d.style.background=COLORS[i%COLORS.length];
    d.title=s.label+' — '+(s.power_w==null?'free':s.power_w+'W')+(s.cadence_rpm?' @'+s.cadence_rpm+'rpm':'');
    d.textContent=s.power_w==null?s.label:s.power_w; tl.appendChild(d);});
});}

function drawChart(){var c=$('chart'),r=window.devicePixelRatio||1,w=c.clientWidth,h=c.clientHeight;
  c.width=w*r;c.height=h*r;var x=c.getContext('2d');x.scale(r,r);x.clearRect(0,0,w,h);
  var names=Object.keys(hist); var mx=100;
  names.forEach(function(n){hist[n].forEach(function(v){if(v>mx)mx=v;});});
  if(targetPow!=null&&targetPow>mx)mx=targetPow; mx=mx*1.1;
  if(targetPow!=null){var ty=h-6-(targetPow/mx)*(h-12);
    x.strokeStyle='rgba(255,255,255,.35)';x.setLineDash([5,4]);x.beginPath();
    x.moveTo(0,ty);x.lineTo(w,ty);x.stroke();x.setLineDash([]);}
  names.forEach(function(n,ci){var arr=hist[n];if(arr.length<2)return;
    x.strokeStyle=COLORS[ci%COLORS.length];x.lineWidth=2;x.beginPath();
    arr.forEach(function(v,i){var px=i/(MAX-1)*w,py=h-6-(v/mx)*(h-12);if(i)x.lineTo(px,py);else x.moveTo(px,py);});
    x.stroke();});}

function toggle(){fetch(started?'/api/stop':'/api/start',{method:'POST'}).then(tick);}

function tick(){fetch('/api/live',{cache:'no-store'}).then(function(r){return r.json();}).then(function(d){
  $('dot').classList.add('on');
  $('mode').textContent=d.mode+(d.capture_running?'':' (capture stopped)');
  var dir=d.director; started=dir.started; targetPow=dir.target_power_w;
  $('btn').textContent=started?'Stop':(dir.finished?'Restart':'Start ride');
  $('btn').className='btn'+(started?' stop':'');
  $('seg').textContent=dir.finished?'COMPLETE':dir.label;
  $('tp').textContent=dir.target_power_w==null?'--':dir.target_power_w;
  $('tc').textContent=dir.target_cadence_rpm?('@ '+dir.target_cadence_rpm+' rpm'):(dir.target_power_w!=null?'any cadence':'');
  $('note').textContent=dir.note||(dir.finished?'Workout complete — nice ride!':(started?'':'Press Start when you\'re spinning.'));
  $('count').textContent=started&&!dir.finished?(mmss(dir.seg_remaining_s)+' left in block'):'';
  $('segbar').style.width=(dir.seg_duration_s?Math.min(100,100*dir.seg_elapsed_s/dir.seg_duration_s):0)+'%';
  $('next').textContent=dir.next_label?('Next: '+dir.next_label+(dir.next_power_w!=null?' — '+dir.next_power_w+' W':'')):'';
  $('total').textContent=started?('elapsed '+mmss(dir.total_elapsed_s)+' / '+mmss(dir.total_s)):('workout '+mmss(dir.total_s));
  $('status').textContent=d.messages+' msgs · '+(d.output||'');
  // meters
  var mEl=$('meters'); var names=Object.keys(d.meters); mEl.innerHTML='';
  names.forEach(function(n){var m=d.meters[n];
    if(!hist[n])hist[n]=[]; if(m.power_w!=null){hist[n].push(m.power_w);if(hist[n].length>MAX)hist[n].shift();}
    var hold=''; if(started&&targetPow!=null&&m.power_w!=null&&targetPow>0){
      var off=Math.abs(m.power_w-targetPow); hold=' <span class="hold '+(off<=Math.max(8,targetPow*0.05)?'ok':'off')+'">'+(m.power_w>=targetPow?'+':'')+(m.power_w-targetPow)+'</span>';}
    var d2=document.createElement('div'); d2.className='meter';
    d2.innerHTML='<div class="nm">'+n+'</div><div class="pw">'+(m.power_w==null?'--':m.power_w)+' <small>W</small>'+hold+'</div><div class="cd">'+(m.cadence_rpm==null?'-- rpm':m.cadence_rpm+' rpm')+(m.age_s!=null&&m.age_s>3?' · stale '+m.age_s+'s':'')+'</div>';
    mEl.appendChild(d2);});
  if(names.length>=2){var a=d.meters[names[0]].power_w,b=d.meters[names[1]].power_w;
    if(a!=null&&b!=null){var dv=document.createElement('div'); dv.className='meter delta';
      dv.innerHTML='<div class="nm">'+names[0]+' − '+names[1]+'</div><div class="pw">'+(a-b>=0?'+':'')+(a-b)+' <small>W</small></div><div class="cd">'+(b?Math.round(100*a/b)+'%':'')+'</div>';
      mEl.appendChild(dv);}}
  drawChart();
}).catch(function(){$('dot').classList.remove('on');});}

loadWorkout(); setInterval(tick,1000); tick(); window.addEventListener('resize',drawChart);
</script></body></html>"""
