# WSL ANT+ Capture Runbook & Troubleshooting

**Written 2026-06-14 after the first live guided ride.** That session got the
critical data (the calibration handshake — see `decisions.md`), but burned ~25
minutes of bike time on avoidable WSL/USB/process problems. This document exists
so that never happens again. Every problem below was hit for real; every fix
below was verified on the day.

---

## 0. The operating model that worked: drive captures from *outside* the terminal

The interactive `ride_wizard.py` **could not be driven** on ride day: the WSL
terminal's stdin threw `read failed 5: I/O error`, so the wizard's `input()`
prompts never received the Enter key, and focus kept bouncing between the
terminal and the chat window. We lost ~10 minutes to "I press Enter and nothing
happens".

**What worked instead, and is now the default for assisted rides:** the agent
(Claude) drives captures from the Windows side via WSL interop, and guides the
rider through chat. No reliance on the rider typing into a terminal at all.

- Run commands *inside* WSL from the Windows/agent side:
  `wsl.exe -d Ubuntu-24.04 -e bash -c "<script>"`
  (write the script to a temp file and `bash -c "$(cat /tmp/x.sh)"` to avoid
  quoting hell).
- Launch the capture **detached** so the wsl.exe call returns but the capture
  keeps running:
  `nohup setsid python code/scripts/01_capture_stages.py ... </dev/null >/tmp/cap.log 2>&1 &`
- Poll the JSONL for live power/cadence and announce ride cues in chat.
- `ride_wizard.py` is still useful for a rider working solo at a normal
  keyboard, but for an assisted ride, **agent-driven is the reliable path.**

Reading the files back from the Windows side:
- ✅ PowerShell: `Get-ChildItem \\wsl.localhost\Ubuntu-24.04\home\...`
- ✅ Inside WSL: `wsl.exe -d <distro> -e bash -c "tail -f ..."`
- ❌ Git-bash style `//wsl.localhost/...` paths do **not** resolve — don't use them.

---

## 1. `usb.core.USBError: [Errno 13] Access denied` — stick perms are root-only

**Cause.** After `usbipd attach`, the device node (`/dev/bus/usb/BBB/DDD`) comes
up `crw-rw-r-- root root` (no write for your user). The udev rule that should
make it `0666` does **not** fire, because usbipd attaches via another distro
(`podman-machine-default`) and *shares* the device — the hotplug uevent doesn't
reach Ubuntu's udevd.

**Fix (needs the rider's password, once per attach):**
```bash
sudo chmod 666 /dev/bus/usb/001/002      # use the bus/dev from `lsusb`
```
Put it on the rider's clipboard (`Set-Clipboard`) so they only have to paste +
Enter + password. After one `sudo`, the password is cached ~15 min.

**Why not the udev helper?** `python -m openant.udev_rules` is broken for pip
installs (it copies a rules file pip doesn't ship). The rule itself is fine and
already written to `/etc/udev/rules.d/42-ant-usb-sticks.rules`; it just didn't
*apply* on this attach. A re-enumeration *within* Ubuntu (see §4) does make
udev apply it and the node comes back `0666` automatically — but re-enumeration
has its own downside (§4), so prefer the one-shot chmod.

---

## 2. `Exception: Responded with error 21: CHANNEL_IN_WRONG_STATE`

**Cause.** Raised at `node.new_channel(...)` / channel assign. The ANT chip was
left with a channel still assigned because the *previous* capture didn't tear
down cleanly (see §6). A fresh `Node()` runs `reset_system()`, but in WSL that
reset can be swallowed by stale buffered broadcast data, so the chip stays
"dirty" and rejects the new channel.

**Fix.** It usually clears on a **retry** with a fresh process a few seconds
later (we saw a fail at 16:42 succeed at 16:44). The robust launcher
(`run_capture.sh`, §8) retries automatically. The thing that makes retries
actually work is fixing §3 first — otherwise the failed attempt hangs and
blocks everything.

**Do NOT** "fix" this with `pyusb dev.reset()` — see §4.

---

## 3. `usb.core.USBError: [Errno 16] Resource busy` — a zombie capture holds the stick  ⚠️ the big one

**Cause.** This was the worst time-sink. When a capture fails *after*
openant's `Node()` has started (e.g. it then hits CHANNEL_IN_WRONG_STATE at
channel assign), the exception propagates — but openant's worker thread is
**not a daemon**, so the process does **not exit**. It hangs forever, still
holding the ANT stick's USB handle open. Every subsequent launch then fails
with `Resource busy` because the device is already claimed.

**Diagnose — find exactly who holds the device:**
```bash
fuser -v /dev/bus/usb/001/002          # shows PID with access
lsof /dev/bus/usb/001/002              # confirms FD + command
ps -eo pid,lstart,cmd | grep capture_stages | grep -v grep
```

**Fix — kill the specific PID (NOT a pattern, see §5):**
```bash
kill -9 <PID>      # e.g. the hung 01_capture_stages.py
```
Then the device is free and the next launch works.

**Permanent fix (now in the code).** `01_capture_stages.py` `main()` now wraps
`setup()` and calls `os._exit(2)` on failure, so a setup error force-kills the
process and the kernel releases the USB device immediately — no more zombies.
This is the single most important fix from the session.

---

## 4. Do not `pyusb dev.reset()` the ANTUSB stick to "clear" state

It looks tempting for §2/§3, but on ride day it made things worse: the reset
**re-enumerates** the device, which (a) can let a kernel driver grab the
FTDI-based ANTUSB2 and (b) leaves it transiently `Resource busy` while it
settles — which we then mistook for ftdi_sio. Re-enumeration *does*
re-trigger udev (perms come back `0666`), but it's not worth the churn.

**Prefer:** kill the zombie holder (§3) and retry (§2). If the chip genuinely
needs a hard reset, do a **usbipd detach + re-attach from Windows** (clean,
predictable) followed by the §1 chmod — not a pyusb reset.

---

## 5. Never `pkill -f` / `kill $(pgrep -f ...)` with a pattern that can match your own shell  ⚠️

Bit us twice (exit code 15 — our own controlling shell got SIGTERM'd).
`pgrep -f '01_capture_stages.py'` and `pgrep -f 'ride_wizard.py'` **also match
the bash `-c` script we're running**, because the pattern string appears in
that script's own command line.

**Rules:**
- Prefer killing by **exact PID** you obtained from `fuser`/`ps`.
- If you must pattern-match, **anchor it** so it can't match the launcher:
  `pkill -f 'ride_wizard\.py$'` (the python proc ends in `...ride_wizard.py`;
  your `bash -c` line does not).
- Or exclude self: `pgrep -f '...' | grep -v "^$$\$" | xargs -r kill`.

---

## 6. `node_stop_error: [Errno 2] Entity not found` on teardown — cosmetic, but it leaves the chip dirty

Logged at the end of every capture. It's openant's `driver.close()` calling
`attach_kernel_driver()`, which fails in WSL because there was no kernel driver
to re-attach. **Harmless to the data** (the JSONL is already written and
`session_end` follows). But the unclean close is *why* the chip is left in the
state that causes §2 on the next launch. Treated as: ignore the message, expect
a possible retry at the next session boundary.

---

## 7. Console encoding: `UnicodeEncodeError: 'charmap' codec ...`

Python printing `→`, `✅`, etc. crashes on the Windows cp1252 console.
- Set `PYTHONIOENCODING=utf-8` when running scripts that print Unicode, **or**
- keep operational scripts ASCII-only (the BLE script was fixed this way).

---

## 8. The golden path: robust launch procedure

Use the launcher `code/scripts/run_capture.sh` (added 2026-06-14), which encodes
all of the above. Manual equivalent, per session:

```bash
cd ~/local-repos/cauldnz/SB20-power-proxy && source .venv/bin/activate

# (a) make sure nothing is holding the stick (kill exact PIDs only)
for p in $(fuser /dev/bus/usb/001/* 2>/dev/null); do kill -9 "$p"; done

# (b) launch detached, with a couple of retries for wrong-state
STAMP=$(date +%Y%m%d-%H%M%S)
OUT="code/findings/captures/<SESSION>-$STAMP.jsonl"
for i in 1 2 3; do
  nohup setsid python code/scripts/01_capture_stages.py \
      --device-id <ID> --duration <SECS> [--log-channel-events] \
      --output "$OUT" </dev/null >/tmp/cap.log 2>&1 &
  sleep 9
  grep -qi 'error\|traceback' /tmp/cap.log && { rm -f "$OUT"; sleep 5; continue; }
  break
done

# (c) confirm data is flowing
tail -2 "$OUT"
```

Session boundaries (C-0 → A → B) are where §2/§3 bite. Between sessions: confirm
the previous capture's process has exited (`ps ... | grep capture_stages`) and
nothing holds the stick (`fuser /dev/bus/usb/001/*`) **before** launching the
next one.

---

## 9. Pre-ride checklist (do this *before* the rider clips in)

1. `usbipd list` → stick shows **Connected** (not just Persisted). If only
   Persisted, the physical stick is unplugged.
2. `usbipd attach --wsl --busid <B>`; verify `lsusb | grep -i 0fcf` inside the
   **target** distro (Ubuntu-24.04), not just "all distros".
3. `ls -l /dev/bus/usb/.../...` → if not `crw-rw-rw-`, do the §1 chmod **now**
   (get the password out of the way before the rider is mid-effort).
4. Launch one throwaway 15 s capture and confirm broadcasts flow. Shake out
   §1/§2/§3 against the clock, not against the rider.
5. Start the BLE survey **once** (don't double-launch — the wizard also offers
   to, which created a duplicate window on the day).

A 60-second pre-flight here would have saved the entire mess.
