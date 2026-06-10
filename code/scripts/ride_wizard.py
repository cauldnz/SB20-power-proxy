#!/usr/bin/env python3
"""Guided Phase-0 morning ride — runs the capture sessions and talks you through them.

One command, no flags needed:

    cd ~/local-repos/cauldnz/SB20-power-proxy
    source .venv/bin/activate
    python code/scripts/ride_wizard.py

What it does, in order:
  0. Preflight  — ANT+ stick present, output dir, device IDs, optional
                  auto-launch of the Windows-side BLE advertisement survey.
  1. Session C-0 (3 min) — light pedalling + one zero-reset; the wizard tells
                  you exactly when to stop pedalling and tap zero-reset, then
                  checks the capture for the calibration page (0x01) itself.
  2. Session A  (15 min) — guided power blocks (targets in STAGES watts — the
                  number the bike/app shows). The wizard announces each block
                  and runs the validator automatically afterwards.
  3. Session B  (10 min, optional) — the other crank, steady riding.

During every session it prints a heartbeat (messages received, last power
seen) so you know data is flowing. Ctrl-C inside a session ends that session
cleanly and the wizard carries on — partial data is kept, never lost.

Preview without hardware (cue flow at 20x speed, no captures written):

    python code/scripts/ride_wizard.py --preview
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Any

HERE = Path(__file__).resolve().parent
CAPTURES = HERE.parent / "findings" / "captures"

# Windows-side paths for the optional BLE survey auto-launch (via WSL interop)
WIN_PY = r"C:\repos\cauldnz\SB20-power-proxy\code\.venv-win\Scripts\python.exe"
WIN_BLE = r"C:\repos\cauldnz\SB20-power-proxy\code\scripts\06_capture_ble.py"
WIN_OUT_DIR = r"C:\repos\cauldnz\SB20-power-proxy\code\findings\captures"


def _load(fname: str, modname: str):
    spec = importlib.util.spec_from_file_location(modname, HERE / fname)
    assert spec and spec.loader
    mod = importlib.util.module_from_spec(spec)
    # Must register in sys.modules BEFORE exec: @dataclass (in the validator)
    # resolves its module via sys.modules on Python 3.12 and crashes otherwise.
    sys.modules[modname] = mod
    spec.loader.exec_module(mod)
    return mod


def say(msg: str = "", bell: bool = False) -> None:
    print(("\a" if bell else "") + msg, flush=True)


def banner(title: str) -> None:
    say()
    say("=" * 62)
    say(f"  {title}")
    say("=" * 62)


def ask(prompt: str, default: str = "") -> str:
    suffix = f" [{default}]" if default else ""
    val = input(f"{prompt}{suffix}: ").strip()
    return val or default


# --- cue engine -------------------------------------------------------------

class CueThread(threading.Thread):
    """Announces ride cues at scheduled offsets; prints a data heartbeat.

    Runs alongside the (blocking) capture. `speed` > 1 compresses time for
    --preview. Fired cues are recorded with their actual wall-clock time so
    the session notes double as the timestamp annotations the Phase-0 docs
    ask for.
    """

    def __init__(self, schedule: list[tuple[float, str]], duration_s: float,
                 runner: Any | None = None, speed: float = 1.0,
                 heartbeat_s: float = 60.0):
        super().__init__(daemon=True)
        self.schedule = sorted(schedule)
        self.duration_s = duration_s
        self.runner = runner
        self.speed = speed
        self.heartbeat_s = heartbeat_s
        self.fired: list[dict[str, Any]] = []
        # NB: must NOT be named _stop — threading.Thread has an internal
        # _stop() method that join() calls; shadowing it with an Event
        # crashes with "'Event' object is not callable".
        self._stop_evt = threading.Event()

    def stop(self) -> None:
        self._stop_evt.set()

    def run(self) -> None:
        t0 = time.monotonic()
        pending = list(self.schedule)
        next_beat = self.heartbeat_s
        while not self._stop_evt.is_set():
            now = (time.monotonic() - t0) * self.speed
            while pending and pending[0][0] <= now:
                offset, msg = pending.pop(0)
                stamp = time.strftime("%H:%M:%S")
                say(f"\n>>> [{stamp}, t={offset:.0f}s]  {msg}", bell=True)
                self.fired.append({"planned_offset_s": offset, "message": msg,
                                   "actual_iso": time.strftime("%Y-%m-%dT%H:%M:%S")})
            if now >= next_beat:
                next_beat += self.heartbeat_s
                if self.runner is not None:
                    p = getattr(self.runner, "last_power", None)
                    c = getattr(self.runner, "last_cadence", None)
                    n = getattr(self.runner, "_messages_logged", 0)
                    extra = ""
                    if p is not None:
                        extra = f" | last power {p} W"
                    if c is not None:
                        extra += f", cadence {c} rpm"
                    say(f"    ... data check: {n} messages captured{extra}")
                    if n <= 2:
                        say("    !!! Almost no data — is the crank awake? "
                            "Rotate the cranks. If this persists, Ctrl-C and "
                            "check with Claude.", bell=True)
            if now >= self.duration_s and not pending:
                break
            time.sleep(0.25)


# --- guided capture runner ----------------------------------------------------

def make_guided_runner_class(cap_mod):
    """Subclass CaptureRunner to remember the last power/cadence for heartbeats."""

    class GuidedRunner(cap_mod.CaptureRunner):
        def __init__(self, **kw):
            super().__init__(**kw)
            self.last_power: int | None = None
            self.last_cadence: int | None = None

        def _on_data(self, data: bytes) -> None:
            # Same as the parent (decode + log) but keeps the latest power/
            # cadence so the cue thread can show a live heartbeat.
            decoded = cap_mod.decode_page(bytes(data))
            self._log("broadcast", data=decoded)
            if decoded.get("page_no_toggle") == 0x10:
                p = decoded.get("instantaneous_power_w")
                c = decoded.get("instantaneous_cadence_rpm")
                if p is not None:
                    self.last_power = p
                if c is not None:
                    self.last_cadence = c

    return GuidedRunner


def run_session(cap_mod, *, label: str, device_id: int, duration_s: float,
                output: Path, schedule: list[tuple[float, str]],
                log_channel_events: bool = False) -> tuple[Any, list[dict]]:
    GuidedRunner = make_guided_runner_class(cap_mod)
    runner = GuidedRunner(device_id=device_id, output_path=output,
                          log_channel_events=log_channel_events)
    runner.setup()
    say(f"\nCapture running: {label} -> {output.name}")
    say("(Ctrl-C ends this session early but keeps its data.)\n")
    cues = CueThread(schedule, duration_s, runner=runner)
    cues.start()
    runner.run(duration_s)  # blocks; handles Ctrl-C + duration internally
    cues.stop()
    return runner, cues.fired


def write_notes(output: Path, *, label: str, device_id: int,
                which_crank: str, fired: list[dict],
                extra: list[str]) -> Path:
    notes = output.with_name(output.stem + "-notes.md")
    lines = [f"# Session notes — {output.name}", "",
             f"- session: {label}",
             f"- device_id: {device_id}",
             f"- which crank (owner-reported): {which_crank}",
             f"- generated by ride_wizard.py at {time.strftime('%Y-%m-%dT%H:%M:%S')}",
             "", "## Cue timeline (planned offset -> actual time)", ""]
    for f in fired:
        lines.append(f"- t={f['planned_offset_s']:.0f}s  {f['actual_iso']}  — {f['message']}")
    if extra:
        lines += ["", "## Wizard observations", ""] + [f"- {x}" for x in extra]
    notes.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return notes


# --- session definitions -----------------------------------------------------

C0_DURATION = 180.0
C0_SCHEDULE = [
    (0,   "Pedal LIGHT and easy (~100 W). We're confirming data flows."),
    (40,  "Get your phone ready: Stages app -> the zero-reset screen."),
    (60,  "STOP pedalling. Set the crank arms VERTICAL and keep them still."),
    (75,  "Tap ZERO-RESET in the Stages app NOW."),
    (115, "Done? Resume LIGHT pedalling until the end."),
    (170, "Almost done — keep spinning."),
]

A_DURATION = 900.0
# Targets are in STAGES watts (what the bike/app displays). Owner FTPs:
# ~367 W Stages / ~330 W Assioma — the discrepancy this project exists to fix.
A_SCHEDULE = [
    (0,   "WARMUP — easy spin ~130-150 W, comfortable cadence (~85 rpm)."),
    (180, "ENDURANCE — lift to ~200 W, cadence ~90 rpm."),
    (360, "TEMPO — lift to ~260 W."),
    (480, "THRESHOLD — hard: hold ~330 W for 2 minutes (about 90% of your Stages FTP)."),
    (600, "SURGE — 30 seconds HARD, 400+ W. Push!"),
    (630, "COAST — STOP PEDALLING completely for 30 s (we need zero-power samples)."),
    (660, "LOW CADENCE — ~200 W but GRIND: cadence ~60 rpm, 2 minutes."),
    (780, "HIGH CADENCE — easy ~150 W but SPIN: 95-100 rpm, 2 minutes."),
    (880, "COOL — easy spin to finish. Nearly there."),
]

B_DURATION = 600.0
B_SCHEDULE = [
    (0,   "Steady riding ~180-220 W, normal cadence. Nothing fancy for 10 min."),
    (300, "Halfway. Same steady effort."),
    (540, "Last minute — keep it steady."),
]


# --- C-0 verdict ---------------------------------------------------------------

def scan_calibration_pages(path: Path) -> list[dict[str, Any]]:
    hits = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            try:
                r = json.loads(line)
            except json.JSONDecodeError:
                continue
            d = r.get("data") or {}
            if d.get("page_no_toggle") == 0x01 or d.get("page") == 0x01:
                hits.append({"t": r.get("monotonic_s"), "kind": r.get("kind"),
                             "calibration_id_hex": d.get("calibration_id_hex"),
                             "calibration_data": d.get("calibration_data"),
                             "raw_hex": d.get("raw_hex")})
    return hits


# --- BLE survey launch ---------------------------------------------------------

def maybe_launch_ble_survey(stamp: str) -> str:
    ans = ask("Auto-launch the BLE advertisement survey in a Windows window? (y/n)", "y")
    if ans.lower() != "y":
        say("Skipped. You can start it manually in PowerShell — see RIDE-CARD.md.")
        return "skipped"
    out = rf"{WIN_OUT_DIR}\ble-adv-survey-{stamp}.jsonl"
    try:
        subprocess.Popen(
            ["cmd.exe", "/c", "start", "BLE survey (leave open)",
             WIN_PY, WIN_BLE, "--adv-only", "--duration", "2700",
             "--output", out],
            cwd="/mnt/c/")
        say("BLE survey launched in a new Windows window (45 min, passive, "
            "advertisements only). Leave it open; close it when you're done riding.")
        return out
    except Exception as e:
        say(f"Could not auto-launch ({e}).")
        say("Start it manually in PowerShell — exact command is in RIDE-CARD.md.")
        return "launch-failed"


# --- preflight -----------------------------------------------------------------

def preflight() -> None:
    banner("PREFLIGHT")
    try:
        out = subprocess.run(["lsusb"], capture_output=True, text=True).stdout
    except FileNotFoundError:
        out = ""
    if "0fcf" in out.lower():
        say("[ok] ANT+ stick visible in WSL.")
    else:
        say("[!!] No ANT+ stick (USB vendor 0fcf) visible in WSL.")
        say("     In an *Administrator PowerShell* on Windows run:")
        say("         usbipd list                      (find the 0fcf busid)")
        say("         usbipd attach --wsl --busid <BUSID>")
        say("     then re-run this wizard.")
        sys.exit(1)
    CAPTURES.mkdir(parents=True, exist_ok=True)
    say(f"[ok] Output dir: {CAPTURES}")


# --- main flow -------------------------------------------------------------------

def main() -> int:
    p = argparse.ArgumentParser(description="Guided Phase-0 ride")
    p.add_argument("--preview", action="store_true",
                   help="Show the cue flow at 20x speed; no hardware, no files.")
    args = p.parse_args()

    if args.preview:
        banner("PREVIEW — Session C-0 cues (20x speed)")
        c = CueThread(C0_SCHEDULE, C0_DURATION, speed=20.0); c.start(); c.join()
        banner("PREVIEW — Session A cues (20x speed)")
        a = CueThread(A_SCHEDULE, A_DURATION, speed=20.0); a.start(); a.join()
        say("\nPreview done. Run without --preview on the bike.")
        return 0

    cap = _load("01_capture_stages.py", "capture_stages_w")
    val = _load("00_validate_capture.py", "validate_capture_w")

    banner("PHASE 0 — GUIDED MORNING RIDE")
    say("Plan: C-0 zero-reset dry run (3 min) -> Session A (15 min) -> "
        "optional Session B (10 min).")
    say("All power targets are STAGES watts (the number the bike/app shows).")
    say("Chat with Claude any time — he can read the capture files live while you ride.")

    preflight()

    stamp = time.strftime("%Y%m%d-%H%M")
    device_id = int(ask("\nDevice ID to capture (combined/L stream)", "62144"))
    which = ask("Per the sticker/app, is this the LEFT crank? (L/R/unsure)", "unsure")
    ble_out = maybe_launch_ble_survey(stamp)

    # ---- Session C-0 ----
    banner("SESSION C-0 — zero-reset dry run (3 min)")
    say("You'll pedal lightly, then at the prompt: stop, cranks vertical,")
    say("tap zero-reset in the Stages app, then pedal again. That's it.")
    input("\nOn the bike, phone in hand? Press ENTER to start C-0... ")
    c0_out = CAPTURES / f"C0-ack-dryrun-{stamp}.jsonl"
    runner, fired = run_session(cap, label="C-0", device_id=device_id,
                                duration_s=C0_DURATION, output=c0_out,
                                schedule=C0_SCHEDULE, log_channel_events=True)
    hits = scan_calibration_pages(c0_out)
    extra = []
    if hits:
        say(f"\n[PASS] C-0: {len(hits)} calibration page(s) (0x01) captured!", bell=True)
        for h in hits[:5]:
            say(f"   t={h['t']}s kind={h['kind']} cal_id={h['calibration_id_hex']} "
                f"offset={h['calibration_data']} raw={h['raw_hex']}")
        extra.append(f"C-0 verdict: PASS — {len(hits)} page-0x01 record(s) captured.")
        say("Session C (the real pairing capture) is GO for a future ride.")
    else:
        say("\n[INVESTIGATE] C-0: no calibration page (0x01) seen.", bell=True)
        say("Not fatal for today — carry on with Session A, but tell Claude; "
            "he'll look at the file and work out why before Session C.")
        extra.append("C-0 verdict: no page-0x01 records — needs investigation.")
    write_notes(c0_out, label="C-0 zero-reset dry run", device_id=device_id,
                which_crank=which, fired=fired, extra=extra)

    # ---- Session A ----
    banner("SESSION A — 15 min guided ride")
    say("Block plan (Stages watts):")
    for t, msg in A_SCHEDULE:
        say(f"   {int(t//60):2d}:{int(t%60):02d}  {msg}")
    input("\nReady to ride? Press ENTER to start Session A... ")
    a_out = CAPTURES / f"A-stagesL-steady-{stamp}.jsonl"
    runner, fired = run_session(cap, label="Session A", device_id=device_id,
                                duration_s=A_DURATION, output=a_out,
                                schedule=A_SCHEDULE)
    say("\nRunning the validator on Session A...")
    rep = val.run_checks(a_out)
    say(val.render_plain(rep))
    verdict = {0: "PASS", 1: "REVIEW", 2: "FAIL"}[rep.verdict]
    write_notes(a_out, label="Session A steady-state", device_id=device_id,
                which_crank=which, fired=fired,
                extra=[f"Validator verdict: {verdict}"])
    if rep.verdict == 2:
        say("\nValidator says FAIL — stop here and paste the output above to Claude.")
        return 2

    # ---- Session B (optional) ----
    banner("SESSION B — other crank, 10 min steady (optional)")
    if ask("Run Session B now? (y/n)", "y").lower() == "y":
        b_id = int(ask("Device ID for the OTHER crank", "17039"))
        input("Press ENTER to start Session B... ")
        b_out = CAPTURES / f"B-stagesR-steady-{stamp}.jsonl"
        runner, fired = run_session(cap, label="Session B", device_id=b_id,
                                    duration_s=B_DURATION, output=b_out,
                                    schedule=B_SCHEDULE)
        write_notes(b_out, label="Session B steady-state (other crank)",
                    device_id=b_id, which_crank="other-than-" + which,
                    fired=fired, extra=[])
        say(f"\nSession B done: {b_out.name}")

    # ---- wrap up ----
    banner("ALL DONE — nice ride!")
    say("Files created this morning:")
    for f in sorted(CAPTURES.glob(f"*{stamp}*")):
        say(f"   {f.name}")
    if ble_out not in ("skipped", "launch-failed"):
        say(f"   (Windows) {ble_out}")
        say("   You can close the BLE survey window now.")
    say("\nNext: tell Claude 'sessions done' in chat — he reads these files")
    say("directly off your machine and will analyse them with you.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
