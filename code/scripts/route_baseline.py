"""Capture a golden baseline of every WifiLink HTTP route from a running board, and diff two.

The oracle for firmware refactors that must not change HTTP behaviour: capture against the board
before the change, flash, capture again, diff. A clean diff is the evidence that 57 route
behaviours -- including all 17 CSRF rejections -- survived.

    python route_baseline.py capture 192.168.1.165 -o before.json
    # ... flash ...
    python route_baseline.py capture 192.168.1.165 -o after.json
    python route_baseline.py diff before.json after.json

SAFETY -- read before extending:

* Never POST /forget or /wifi/off; both strand the board. GET /wifi/off is the confirm page and
  is safe. The CSRF guard is proven on harmless routes instead: every mutating route runs the same
  check, so probing 17 of them establishes the mechanism.
* Never POST a curve you did not just read back. web/HTTP-API.md: "empty body clears it". The
  first version of this script read the wrong JSON key, built an empty body, and wiped a board's
  calibration; the values were only recoverable because HTTP-API.md documents the same profile.
  The guard below refuses instead of clearing, and the run ends by asserting the board is
  unchanged.
* POST /curve needs Content-Type: text/plain and POST /obc/buttons.json needs application/json.
  The Arduino WebServer parses a urlencoded body into named args and leaves arg("plain") empty, so
  the wrong content-type silently becomes an empty body -- which reads as a device 400/wipe but is
  a harness bug. decisions.md recorded this for /curve long before this script re-discovered it.

DETERMINISM -- established by capturing twice against one unchanged firmware and requiring a zero
diff before trusting the oracle at all:

* Route ORDER matters. /log answers 403 unless the ring is enabled, so /log/on must precede it and
  /log/off must follow, or the route reads 200 once and 403 for ever after. /stats/reset precedes
  /stats for the same reason.
* /log, /setup, /stats and /diag have legitimately non-deterministic bodies (a rolling ring, a live
  WiFi/BLE scan, loop counters, RSSI in non-JSON text). They are compared by STRUCTURE -- the field
  and section names a route refactor would break. Even so, /log and /setup still differ between a
  cold and a warm boot on IDENTICAL firmware: Provisioning.h and ConfigPage.h emit `<i class='on'>`
  signal bars and a `<br><small>address</small>` row PER DISCOVERED DEVICE, so an empty scan list
  and a populated one have different tag sets. Treat a diff confined to those routes as unproven
  rather than as a regression, and re-capture with both boards in the same state.
* `len` counts un-normalised volatile fields (build_sha, uptime), so diff ignores it. It is kept in
  the record because it is the only thing that catches a body change past the 1200-char truncation
  -- that is how the regenerated SPA showing up at /app was spotted.
"""
import argparse
import json
import re
import sys
import urllib.error
import urllib.parse
import urllib.request

# Fields that legitimately differ between two boots -- blanked before diffing.
VOLATILE = re.compile(
    r'"(ms|heap|rssi|uptime|forwarded|power_w|cadence_rpm|balance_pct|'
    r'src_power_w|src_cadence_rpm|src_balance_pct|build_sha|build_time|source|src_name|'
    r'stalls_50ms|stalls_200ms|window_s|loop_count)"'
    r'\s*:\s*(-?\d+\.?\d*|"[^"]*")'
)


def norm(body: str) -> str:
    body = VOLATILE.sub(lambda m: f'"{m.group(1)}":*', body)
    # Serial-over-HTTP log and uptime-bearing text vary per boot.
    body = re.sub(r"\d{4,}", "*", body)
    return body


# Routes whose BODY is legitimately non-deterministic run to run, measured by capturing twice
# against one unchanged firmware: /log is a rolling ring buffer, /setup embeds a live WiFi scan,
# /stats and /diag carry loop counters and RSSI in non-JSON text. Diffing their bytes would drown
# a real regression in noise, so they are compared by STRUCTURE instead -- the field and section
# names a route refactor would actually break, which measured identical across both runs.
UNSTABLE_BODY = {"GET /log", "GET /setup", "GET /stats", "GET /diag"}


def shape(body: str) -> str:
    """A structural fingerprint: the names a route emits, not the values."""
    names = set()
    names |= {f"json:{k}" for k in re.findall(r'"([A-Za-z_][\w]*)"\s*:', body)}
    names |= {f"attr:{v}" for v in re.findall(r"\b(?:name|id)=['\"]([^'\"]+)['\"]", body)}
    names |= {f"tag:{t.lower()}" for t in re.findall(r"<([a-zA-Z][a-zA-Z0-9]*)", body)}
    names |= {f"sect:{s}" for s in re.findall(r"^\s*\[([^\]]+)\]", body, re.M)}
    names |= {f"field:{s}" for s in re.findall(r"^\s*([a-z_]+)=", body, re.M)}
    return "\n".join(sorted(names))


def req(method: str, path: str, body=None, headers=None):
    url = BASE + path
    data = body.encode() if body is not None else None
    h = {"Content-Type": "application/x-www-form-urlencoded"}
    h.update(headers or {})
    r = urllib.request.Request(url, data=data, headers=h, method=method)
    try:
        with urllib.request.urlopen(r, timeout=15) as resp:
            body = resp.read().decode("utf-8", "replace")
            return resp.status, resp.headers.get("Content-Type", ""), body
    except urllib.error.HTTPError as e:
        return e.code, e.headers.get("Content-Type", ""), e.read().decode("utf-8", "replace")
    except Exception as e:  # noqa: BLE001 - the record should carry the failure, not raise
        return -1, "", f"ERROR {type(e).__name__}: {e}"
GETS = [
    "/", "/ui", "/more", "/status", "/app", "/stats/reset", "/stats", "/compare",
    # ORDER MATTERS and must leave the board as it found it. /log answers 403 unless the ring
    # is enabled, so enabling first makes this run-to-run deterministic instead of "200 the
    # first time, 403 for ever after" (which is what the first baseline capture recorded).
    "/log/on", "/log", "/log/off",
    "/obc", "/obc/buttons.json", "/obc/press", "/obc/press?id=0x30",
    "/obc/press?id=999", "/obc/press?id=0x01&state=0",
    "/setup", "/setup/scan", "/scan", "/config", "/curve", "/diag", "/report",
    "/workout", "/workout/state",
    "/calibrate", "/calibrate/scan",
    "/wifi/off",              # GET is the confirm PAGE - safe. POST is not, and is never sent.
    "/definitely-not-a-route",  # onNotFound behaviour
]

# (path, body) -- chosen to be no-ops or explicit rejections against the live config.
POSTS = [
    ("/curve", None),                  # filled in at runtime with the board's own curve
    ("/obc/buttons.json", None),       # filled in at runtime with the board's own bindings
    ("/workout/load", "not a workout"),
    ("/workout/preset", "key=__nope__"),
    ("/workout/start", ""),
    ("/workout/pause", ""),
    ("/workout/resume", ""),
    ("/workout/skip", ""),
    ("/workout/stop", ""),
    ("/calibrate/finish", ""),         # rejects: not calibrating
]

# Mutating routes probed with a hostile Origin: expect 403 and NO side effect.
CSRF = [
    "/curve", "/obc/buttons.json", "/workout/load", "/workout/preset",
    "/workout/start", "/workout/stop", "/config", "/setup/save", "/setup/reset",
    "/calibrate/start", "/calibrate/finish", "/calibrate/save", "/calibrate/cancel",
    "/obc/devmode/on", "/obc/devmode/off", "/obc/shifter/on", "/obc/shifter/off",
]

def capture() -> dict:
    out = {}

    for p in GETS:
        st, ct, b = req("GET", p)
        key = f"GET {p}"
        rec = {"status": st, "ct": ct}
        if key in UNSTABLE_BODY:
            rec["shape"] = shape(b)
        else:
            rec["len"] = len(b)
            rec["body"] = norm(b)[:1200]
        out[key] = rec

    # Round-trip the board's own state so these POSTs are true no-ops.
    _, _, cur_curve = req("GET", "/curve")
    _, _, cur_btns = req("GET", "/obc/buttons.json")

    # DESTRUCTIVE-BUG GUARD. The first version of this read `.get("points")` -- the key is "curve"
    # -- so it built an EMPTY body, and web/HTTP-API.md says plainly of POST /curve: "empty body
    # clears it". It also posted urlencoded, which decisions.md had already recorded as mangled by
    # the Arduino form parser into an empty curve. Either fault alone wipes the board's
    # calibration; the capture run did exactly that to the C3 and the values were only recoverable
    # because HTTP-API.md happened to document the same profile.
    try:
        pts = json.loads(cur_curve).get("curve", [])
        curve_body = ",".join(f"{w}:{f}" for w, f in pts)
    except Exception:  # noqa: BLE001 - any parse failure must fall through to the refusal below
        curve_body = ""

    for p, body in POSTS:
        hdrs = None
        if p == "/curve":
            if not curve_body:
                # Refuse rather than clear. A skipped vector is a gap in the oracle; a cleared
                # curve is lost rider calibration.
                out["POST /curve"] = {
                    "status": "SKIPPED",
                    "why": "refusing to POST an empty curve: it would CLEAR the board's"
                           f" calibration. GET /curve returned: {cur_curve[:200]}",
                }
                continue
            body = curve_body
            hdrs = {"Content-Type": "text/plain"}   # urlencoded is mangled -> empty curve
        elif p == "/obc/buttons.json":
            body = cur_btns
            # Same trap as /curve: a urlencoded JSON body arrives as named args with arg("plain")
            # empty, so the board answers 400 and it looks like a device defect. It is the harness.
            hdrs = {"Content-Type": "application/json"}
        st, ct, b = req("POST", p, body, hdrs)
        out[f"POST {p}"] = {"status": st, "ct": ct, "len": len(b), "body": norm(b)[:1200]}

    for p in CSRF:
        st, ct, b = req("POST", p, "", {"Origin": "http://evil.example"})
        out[f"CSRF {p}"] = {"status": st, "body": norm(b)[:200]}

    # The harness must leave the board exactly as it found it. This check exists because an earlier
    # version silently cleared the correction curve and nothing noticed until a much later diff.
    _, _, end_curve = req("GET", "/curve")
    _, _, end_btns = req("GET", "/obc/buttons.json")
    if end_curve != cur_curve or end_btns != cur_btns:
        print("HARNESS MUTATED THE BOARD -- this capture is not a valid oracle:", file=sys.stderr)
        print(f"  /curve before: {cur_curve}\n  /curve after : {end_curve}", file=sys.stderr)
        print(f"  /obc  before: {cur_btns}\n  /obc  after : {end_btns}", file=sys.stderr)
        sys.exit(2)

    return out


def diff(before: dict, after: dict) -> int:
    """Report behavioural differences. Returns the number of differing vectors."""
    def cmp(rec):
        # `len` counts un-normalised volatile fields, so it is reported but never compared.
        return {k: v for k, v in rec.items() if k != "len"}

    kb, ka = set(before), set(after)
    for k in sorted(kb - ka):
        print(f"MISSING  {k}  (present before, gone after)")
    for k in sorted(ka - kb):
        print(f"NEW      {k}")

    n = 0
    for k in sorted(kb & ka):
        if cmp(before[k]) == cmp(after[k]):
            if before[k].get("len") != after[k].get("len"):
                print(f"note     {k}: body length {before[k].get('len')} -> {after[k].get('len')}"
                      " (normalised body identical; volatile fields or past the 1200-char cut)")
            continue
        n += 1
        print(f"DIFF     {k}: {before[k].get('status')} -> {after[k].get('status')}")
        sb, sa = before[k].get("shape"), after[k].get("shape")
        if sb is not None or sa is not None:
            B, A = set((sb or "").split("\n")), set((sa or "").split("\n"))
            print(f"           lost : {sorted(B - A)[:15]}")
            print(f"           added: {sorted(A - B)[:15]}")
        else:
            print(f"           before: {before[k].get('body', '')[:300]!r}")
            print(f"           after : {after[k].get('body', '')[:300]!r}")

    total = len(kb & ka)
    print(f"\nRESULT: {n} differing of {total} vectors"
          f"{' -- BEHAVIOUR PRESERVED' if n == 0 and kb == ka else ''}")
    if n and UNSTABLE_BODY & {k for k in kb & ka if cmp(before[k]) != cmp(after[k])}:
        print("NOTE: some differing routes are in UNSTABLE_BODY. Those differ between a cold and a"
              "\n      warm boot on IDENTICAL firmware -- re-capture with both boards warm before"
              "\n      calling it a regression.")
    return n


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("capture", help="capture a baseline from a running board")
    c.add_argument("host", nargs="?", default="192.168.1.165")
    c.add_argument("-o", "--out", help="write here instead of stdout")
    d = sub.add_parser("diff", help="compare two captures")
    d.add_argument("before")
    d.add_argument("after")
    args = ap.parse_args()

    if args.cmd == "capture":
        BASE = f"http://{args.host}"
        result = capture()
        text = json.dumps(result, indent=1, sort_keys=True)
        if args.out:
            with open(args.out, "w", encoding="utf-8") as fh:
                fh.write(text)
            print(f"{len(result)} vectors -> {args.out}", file=sys.stderr)
        else:
            print(text)
    else:
        with open(args.before, encoding="utf-8") as fh:
            b = json.load(fh)
        with open(args.after, encoding="utf-8") as fh:
            a = json.load(fh)
        sys.exit(1 if diff(b, a) else 0)

