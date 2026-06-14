#!/usr/bin/env bash
#
# Robust launcher for the Phase-0 ANT+ capture in WSL.
#
# Encodes the operational lessons from the 2026-06-14 ride (full write-up in
# code/findings/wsl-capture-runbook.md):
#   - release the stick if a zombie capture is holding it (Resource busy);
#   - launch detached so it survives this shell exiting;
#   - retry the transient ANT "CHANNEL_IN_WRONG_STATE" that hits at session
#     boundaries;
#   - never pattern-kill in a way that could match this script itself.
#
# Run it FROM WSL (the venv must be active or will be sourced):
#   ./code/scripts/run_capture.sh <device_id> <duration_s> <output.jsonl> [extra capture args...]
#
# Examples:
#   ./code/scripts/run_capture.sh 62144 180 code/findings/captures/C0-$(date +%H%M%S).jsonl --log-channel-events
#   ./code/scripts/run_capture.sh 62144 900 code/findings/captures/A-$(date +%H%M%S).jsonl
#
# NOTE: this launcher was authored after ride day and exercised against the
# same hardware path, but treat the first use of each session like a pre-flight
# (watch the printed "data flowing" check).

set -u

DEV="${1:?usage: run_capture.sh <device_id> <duration_s> <output.jsonl> [extra args]}"
DUR="${2:?missing duration_s}"
OUT="${3:?missing output path}"
shift 3
EXTRA=("$@")

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO" || exit 1
CAP="code/scripts/01_capture_stages.py"

# Activate the venv if openant isn't importable yet.
if ! python -c 'import openant' >/dev/null 2>&1; then
  # shellcheck disable=SC1091
  source .venv/bin/activate 2>/dev/null || true
fi

echo "== run_capture: device=$DEV duration=${DUR}s out=$OUT =="

# --- 1. Release the stick if a previous capture is still holding it. ---------
# fuser prints the PIDs holding any 0fcf USB node. We kill those EXACT pids
# (never a name pattern — that can match this very script; see runbook §5).
holders="$(fuser /dev/bus/usb/*/* 2>/dev/null | tr -s ' ' || true)"
if [ -n "${holders// /}" ]; then
  echo "  releasing stick held by PID(s):$holders"
  for p in $holders; do kill -9 "$p" 2>/dev/null || true; done
  sleep 2
fi

# --- 2. Launch detached, with retries for transient wrong-state. -------------
launched=0
for attempt in 1 2 3; do
  echo "  launch attempt $attempt..."
  nohup setsid python "$CAP" \
      --device-id "$DEV" --duration "$DUR" --output "$OUT" "${EXTRA[@]}" \
      </dev/null >/tmp/run_capture.log 2>&1 &
  sleep 9
  if grep -qiE 'traceback|error 21|usberror|resource busy|access denied' /tmp/run_capture.log; then
    echo "    failed: $(tail -1 /tmp/run_capture.log | cut -c1-90)"
    rm -f "$OUT"
    sleep 5
    continue
  fi
  launched=1
  break
done

# --- 3. Report. --------------------------------------------------------------
if [ "$launched" -ne 1 ]; then
  echo "RESULT: FAILED to launch after 3 attempts. Last log:"
  tail -5 /tmp/run_capture.log
  echo "Hint: check 'fuser /dev/bus/usb/*/*' for a zombie holder, and"
  echo "      'ls -l' the device node for 0666 perms (runbook §1, §3)."
  exit 1
fi

recs="$(wc -l < "$OUT" 2>/dev/null || echo 0)"
echo "RESULT: RUNNING -> $OUT ($recs records and counting)"
echo "  last record:"; tail -1 "$OUT"
echo "  (capture self-stops after ${DUR}s; tail the file to watch it live)"
