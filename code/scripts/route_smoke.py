#!/usr/bin/env python3
"""route_smoke.py — smoke-test a live board's HTTP routes (catches the form-POST class of bug).

Green CI didn't catch that the form-POST routes silently ignored browser urlencoded bodies — host
tests fed the pure parser a string; the LIVE route was never hit. This exercises the routes on a real
board and, crucially, POSTs a real ``application/x-www-form-urlencoded`` form and confirms the device
persisted it (the regression guard). Pure pass/fail logic is in sb20proxy.qa.route_check; this is the
HTTP seam. Stdlib only (no bleak/venv needed).

    python code/scripts/route_smoke.py --ip 192.168.1.165          # GET checks + the POST-persist guard
    python code/scripts/route_smoke.py --ip sb20proxy.local --no-post   # read-only (no config write/reboot)

Exit 0 = all routes healthy. The --post check writes a sentinel via /setup/save (reboots), verifies
/diag reflects it, then restores the original config (reboots again). Point it at a spare/dev board.
"""

from __future__ import annotations

import argparse
import os
import sys
import time
from urllib.error import URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))
from sb20proxy.analysis.diag import parse_diag_report  # noqa: E402
from sb20proxy.qa.route_check import (  # noqa: E402
    check_form_persisted,
    check_get,
    check_status_json,
    overall_pass,
    render,
)


def _get(ip: str, path: str, timeout: float = 6.0) -> tuple[int, str]:
    try:
        with urlopen(f"http://{ip}{path}", timeout=timeout) as r:  # noqa: S310 — LAN device
            return r.status, r.read().decode("utf-8", "replace")
    except URLError as exc:
        return 0, f"{exc}"
    except OSError as exc:
        return 0, f"{exc}"


def _post_form(ip: str, path: str, fields: dict, timeout: float = 6.0) -> int:
    """POST application/x-www-form-urlencoded — exactly what a browser <form> sends (the bug's trigger)."""
    data = urlencode(fields).encode()
    req = Request(f"http://{ip}{path}", data=data,
                  headers={"Content-Type": "application/x-www-form-urlencoded"})
    try:
        with urlopen(req, timeout=timeout) as r:  # noqa: S310
            return r.status
    except (URLError, OSError):
        return 0


def _wait_up(ip: str, timeout: float = 45.0) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if _get(ip, "/status", timeout=3.0)[0] == 200:
            return True
        time.sleep(2)
    return False


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ip", required=True, help="board IP or mDNS name (e.g. sb20proxy.local)")
    ap.add_argument("--no-post", action="store_true",
                    help="skip the config-write persistence check (read-only; no reboot)")
    a = ap.parse_args()

    checks = []
    # --- GET routes ---
    checks.append(check_status_json(*_get(a.ip, "/status")))
    s, b = _get(a.ip, "/")
    checks.append(check_get("/ (dashboard)", s, b, ["SB20 Proxy", "/status"]))
    s, b = _get(a.ip, "/calibrate")
    checks.append(check_get("/calibrate", s, b, ["Meter calibration", "/calibrate/start"]))
    s, b = _get(a.ip, "/setup")
    checks.append(check_get("/setup", s, b, ["power source", "/setup/save"]))
    s, b = _get(a.ip, "/diag")
    checks.append(check_get("/diag", s, b, ["[config]", "[status]"]))

    # --- the form-POST persistence guard (the regression that bit us) ---
    if not a.no_post:
        _, diag0 = _get(a.ip, "/diag")
        orig = parse_diag_report(diag0).config
        sentinel = "RouteSmoke9"
        if _post_form(a.ip, "/setup/save",
                      {"name": orig.get("source_name_filter", "ASSIOMA"), "addr": "",
                       "spoof_name": sentinel, "spoof_serial": orig.get("spoof_serial", ""),
                       "single": ""}) and _wait_up(a.ip):
            _, diag1 = _get(a.ip, "/diag")
            checks.append(check_form_persisted("spoof_name", sentinel, diag1))
            # restore the original spoof identity (reboots again)
            _post_form(a.ip, "/setup/save",
                       {"name": orig.get("source_name_filter", "ASSIOMA"), "addr": "",
                        "spoof_name": orig.get("spoof_name", "Stages 62144"),
                        "spoof_serial": orig.get("spoof_serial", ""), "single": ""})
            _wait_up(a.ip)
        else:
            from sb20proxy.qa.route_check import RouteCheck
            checks.append(RouteCheck("POST persisted (spoof_name)", False,
                                     "POST/reboot failed — board didn't come back"))

    print(render(checks, title=f"Board route smoke ({a.ip})"))
    return 0 if overall_pass(checks) else 1


if __name__ == "__main__":
    raise SystemExit(main())
