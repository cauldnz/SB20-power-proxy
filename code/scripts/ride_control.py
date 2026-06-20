#!/usr/bin/env python3
"""ride_control.py — steer a running Ride Director from the command line.

The ergonomic front end to the agent control API (server.py / control.py): a Claude
Code session (or you) drives the ride the rider sees on their phone, instead of raw
curl. Stdlib only — works anywhere Python does.

  # what's happening right now (meters, segment, hold, erg setpoint, plan)
  python code/scripts/ride_control.py state

  # talk to the rider; set an ad-hoc hold; steer the plan
  python code/scripts/ride_control.py message "Settle into a steady 220 W" --level info
  python code/scripts/ride_control.py target 250 --cadence 90 --for 120
  python code/scripts/ride_control.py target --pct 0.9          # 90% FTP
  python code/scripts/ride_control.py target --clear
  python code/scripts/ride_control.py skip
  python code/scripts/ride_control.py extend 60                 # +60 s on this block
  python code/scripts/ride_control.py goto 2
  python code/scripts/ride_control.py profile --ftp 365
  python code/scripts/ride_control.py segment append --dur 300 --label "VO2" --zone Z5
  python code/scripts/ride_control.py plan my_workout.json      # replace the whole plan

  --base http://sb20proxy.local:8080   (default http://localhost:8080)
  --token <t>                          if the server was started with --control-token
"""

from __future__ import annotations

import argparse
import json
import sys
import urllib.error
import urllib.request


def _call(args: argparse.Namespace, method: str, path: str, body: dict | None = None):
    url = args.base.rstrip("/") + path
    headers = {"Content-Type": "application/json"}
    if args.token:
        headers["X-Control-Token"] = args.token
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            return r.status, json.loads(r.read() or b"{}")
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read() or b"{}")
    except urllib.error.URLError as e:
        print(f"cannot reach {url}: {e.reason}", file=sys.stderr)
        raise SystemExit(2) from None


def _print_state(st: dict) -> None:
    d = st.get("director", {})
    seg = f'{d.get("label", "?")}'
    if d.get("target_power_w") is not None:
        seg += f' {d["target_power_w"]} W'
    if d.get("zone"):
        seg += f' [{d["zone"]} {d.get("zone_name", "")}]'
    print(f'workout : {d.get("workout")}  (v{st.get("plan_version")}, '
          f'{d.get("seg_index")}/{d.get("n_segments")})')
    print(f'segment : {seg}')
    if st.get("hold"):
        print(f'HOLD    : {st["hold"]}')
    print(f'erg set : {st.get("erg_setpoint_w")} W   profile FTP {st.get("profile", {}).get("ftp_w")} W')
    for name, m in (st.get("meters") or {}).items():
        print(f'meter   : {name:10} {m.get("power_w")} W  {m.get("cadence_rpm")} rpm')
    if st.get("message"):
        print(f'message : {st["message"]["text"]}')


def _seg_body(a: argparse.Namespace) -> dict:
    seg: dict = {"duration_s": a.dur, "label": a.label or ""}
    if a.watts is not None:
        seg["power_w"] = a.watts
    if a.pct is not None:
        seg["pct_ftp"] = a.pct
    if a.zone is not None:
        seg["zone"] = a.zone
    if a.cadence is not None:
        seg["cadence_rpm"] = a.cadence
    if a.note:
        seg["note"] = a.note
    return seg


def main() -> int:
    # --base / --token are shared so they work either before OR after the subcommand
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--base", default="http://localhost:8080", help="ride server base URL")
    common.add_argument("--token", default=None, help="control token, if the server requires one")

    p = argparse.ArgumentParser(description="Steer a running Ride Director", parents=[common])
    sub = p.add_subparsers(dest="cmd", required=True)

    def cmd(name: str, desc: str):
        return sub.add_parser(name, help=desc, parents=[common])

    cmd("state", "print the live control state")
    cmd("start", "start the ride clock")
    cmd("stop", "stop / reset the ride clock")
    cmd("skip", "advance to the next block now")

    m = cmd("message", "push a coaching message to the phone banner")
    m.add_argument("text")
    m.add_argument("--level", default="info", choices=["info", "ok", "warn", "bad"])
    m.add_argument("--ttl", type=float, default=None, help="seconds before it fades")

    t = cmd("target", "set/clear an ad-hoc hold target")
    t.add_argument("watts", nargs="?", type=int, default=None)
    t.add_argument("--pct", type=float, default=None, help="fraction of FTP (e.g. 0.9)")
    t.add_argument("--cadence", type=int, default=None)
    t.add_argument("--for", dest="duration", type=float, default=None, help="auto-clear after N s")
    t.add_argument("--clear", action="store_true")

    g = cmd("goto", "jump to a block index and restart it now")
    g.add_argument("index", type=int)

    e = cmd("extend", "add (or subtract) seconds on the active block")
    e.add_argument("seconds", type=float)

    pr = cmd("profile", "set rider FTP / scale")
    pr.add_argument("--ftp", type=int, default=None)
    pr.add_argument("--scale", default=None)

    sg = cmd("segment", "edit a single segment")
    sg.add_argument("op", choices=["append", "insert", "replace", "delete"])
    sg.add_argument("--index", type=int, default=None)
    sg.add_argument("--dur", type=float, default=0.0)
    sg.add_argument("--label", default="")
    sg.add_argument("--watts", type=int, default=None)
    sg.add_argument("--pct", type=float, default=None)
    sg.add_argument("--zone", default=None)
    sg.add_argument("--cadence", type=int, default=None)
    sg.add_argument("--note", default="")

    pl = cmd("plan", "replace the whole plan from a JSON file")
    pl.add_argument("file", help="{name, segments:[...]} JSON")

    a = p.parse_args()

    if a.cmd == "state":
        status, st = _call(a, "GET", "/api/control/state")
        _print_state(st) if status == 200 else print(st)
        return 0 if status == 200 else 1
    if a.cmd in ("start", "stop"):
        status, r = _call(a, "POST", f"/api/{a.cmd}")
    elif a.cmd == "skip":
        status, r = _call(a, "POST", "/api/control/skip")
    elif a.cmd == "message":
        status, r = _call(a, "POST", "/api/control/message",
                          {"text": a.text, "level": a.level, "ttl_s": a.ttl})
    elif a.cmd == "target":
        if a.clear:
            body = {"clear": True}
        else:
            body = {"power_w": a.watts, "pct_ftp": a.pct,
                    "cadence_rpm": a.cadence, "duration_s": a.duration}
        status, r = _call(a, "POST", "/api/control/target", body)
    elif a.cmd == "goto":
        status, r = _call(a, "POST", "/api/control/goto", {"index": a.index})
    elif a.cmd == "extend":
        status, r = _call(a, "POST", "/api/control/extend", {"seconds": a.seconds})
    elif a.cmd == "profile":
        status, r = _call(a, "POST", "/api/control/profile",
                          {"ftp_w": a.ftp, "scale": a.scale})
    elif a.cmd == "segment":
        body = {"op": a.op, "index": a.index}
        if a.op != "delete":
            body["segment"] = _seg_body(a)
        status, r = _call(a, "POST", "/api/control/segments", body)
    elif a.cmd == "plan":
        with open(a.file, encoding="utf-8") as fh:
            status, r = _call(a, "POST", "/api/control/plan", json.load(fh))
    else:  # unreachable (argparse requires a known cmd)
        return 2

    print(json.dumps(r))
    return 0 if status == 200 and r.get("ok", True) else 1


if __name__ == "__main__":
    raise SystemExit(main())
