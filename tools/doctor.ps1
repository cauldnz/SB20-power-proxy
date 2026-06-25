<#
.SYNOPSIS
  Ride-readiness pre-flight for SB20-proxy: the build/flash toolchain AND the dual-radio capture rig.

.DESCRIPTION
  Run this at the DESK before relying on a session at the bike. It answers two questions on THIS machine:
    1. "can we actually build AND flash a firmware?" - the build/flash toolchain (session 8: a Py 3.14
       upgrade orphaned PlatformIO + no host compiler -> ~30 min lost mid-ride).
    2. "can we actually CAPTURE on both radios?" - the always-on dual-radio sniff that is a standing
       pre-flight rule (PLAYBOOK §pre-flight): the nRF BLE sniffer (-> pcap, via Npcap + the nRF Sniffer
       extcap) AND the ANT+ stick (-> JSONL, via usbipd -> a WSL libusb claim). Session 9 missed this across
       two "check everything" passes because doctor only covered the build toolchain - a green doctor gave
       false confidence and the gap (Npcap absent, nRF extcap unregistered, ANT node root-only) surfaced
       only at the bike. This script now GATES the rig so a green doctor means captures actually work.

  Exits non-zero if any required check fails. A WARN is an intentionally-absent radio or an optional piece.
  Re-provision a failing build env with tools/provision-dev-env.ps1; stand the capture rig up per
  tools/README.md > "Capture rig" and code/findings/wsl-capture-runbook.md.

.PARAMETER BoardIp
  Optional: also verify the board responds and print OTA host-IP candidates.
.PARAMETER NoCaptureRig
  Skip the capture-rig section entirely - build/flash toolchain only (does NOT certify ride-readiness).
.PARAMETER NoNrf
  The nRF BLE sniffer is intentionally absent this ride - report its checks as WARN, don't gate on them.
.PARAMETER NoAnt
  The ANT+ stick is intentionally absent this ride - report its checks as WARN, don't gate on them.
.PARAMETER WslDistro
  WSL distro that runs the ANT+ capture (holds the openant/pyusb venv). Default: Ubuntu-24.04.
.PARAMETER WslRepo
  Repo path under the WSL $HOME that holds the .venv (openant/pyusb). Default: local-repos/cauldnz/SB20-power-proxy.

.EXAMPLE
  .\tools\doctor.ps1
.EXAMPLE
  .\tools\doctor.ps1 -BoardIp 192.168.1.165    # also check the board is reachable + its OTA port
.EXAMPLE
  .\tools\doctor.ps1 -NoCaptureRig             # build/flash toolchain only (skip the capture rig)
.EXAMPLE
  .\tools\doctor.ps1 -NoNrf                     # ANT+-only ride; the nRF sniffer is intentionally absent
#>
param(
  [string]$BoardIp = "",                                  # optional: also verify the board responds and ArduinoOTA (UDP 3232) is reachable
  [switch]$NoCaptureRig,                                  # build/flash only - skip the capture-rig gate
  [switch]$NoNrf,                                         # nRF sniffer intentionally absent -> WARN, don't gate
  [switch]$NoAnt,                                         # ANT+ stick intentionally absent -> WARN, don't gate
  [string]$WslDistro = "Ubuntu-24.04",                   # distro that runs the ANT+ capture
  [string]$WslRepo   = "local-repos/cauldnz/SB20-power-proxy"   # repo path under WSL $HOME with the openant/pyusb .venv
)
$repo = Split-Path $PSScriptRoot -Parent
$fail = 0
$warn = 0
function Result($name, $state, $detail) {
  switch ($state) {
    'PASS' { Write-Host ("  [PASS] {0,-32} {1}" -f $name, $detail) -ForegroundColor Green }
    'WARN' { Write-Host ("  [WARN] {0,-32} {1}" -f $name, $detail) -ForegroundColor Yellow; $script:warn++ }
    default { Write-Host ("  [FAIL] {0,-32} {1}" -f $name, $detail) -ForegroundColor Red; $script:fail++ }
  }
}
function Check($name, $ok, $detail) { Result $name $(if ($ok) { 'PASS' } else { 'FAIL' }) $detail }

# Prove openant can actually claim the ANT stick over libusb inside WSL (not just that it's attached).
# Heredoc-free on purpose: write the probe to a .py temp (python tolerates CRLF) and run it directly via
# `wsl -- <python> <file>`. A bash heredoc written from PowerShell breaks on CRLF (runbook's "quoting hell").
# Returns the probe's raw stdout (tokens: NODE=, PERMS=, RESULT=OK|NODEV|IMPORTFAIL|ERR errno=N).
function Invoke-WslAntClaim($distro, $repoRel) {
  $wslHome = (wsl.exe -d $distro -- bash -lc 'printf %s "$HOME"' 2>$null | Out-String).Trim()
  $venv = "$wslHome/$repoRel/.venv/bin/python"
  wsl.exe -d $distro -- test -x $venv 2>$null
  $py = if ($LASTEXITCODE -eq 0) { $venv } else { 'python3' }   # fall back to system python3 (may lack pyusb)
  $probe = @'
import sys, os
try:
    import usb.core, usb.util
except Exception as e:
    print("RESULT=IMPORTFAIL " + str(e)); sys.exit(0)
d = usb.core.find(idVendor=0x0fcf, idProduct=0x1008)
if d is None:
    print("RESULT=NODEV"); sys.exit(0)
node = "/dev/bus/usb/%03d/%03d" % (d.bus, d.address)
print("NODE=" + node)
try:
    print("PERMS=%o" % (os.stat(node).st_mode & 0o777))
except Exception:
    pass
try:
    d.set_configuration(); usb.util.dispose_resources(d); print("RESULT=OK")
except usb.core.USBError as e:
    print("RESULT=ERR errno=%s" % e.errno)
'@
  $pyWin = Join-Path $env:TEMP 'sb20_ant_claim_probe.py'
  [System.IO.File]::WriteAllText($pyWin, $probe)
  $pyWsl = (wsl.exe -d $distro -- wslpath "$($pyWin -replace '\\','/')" 2>$null | Out-String).Trim()
  $out = (wsl.exe -d $distro -- $py $pyWsl 2>&1 | Out-String)
  # Best-effort auto-fix if the node is root-only AND passwordless sudo happens to be configured; then re-probe.
  # (Not configured here - doctor falls through to the chmod remediation message, which is the desk-time win.)
  $node = ([regex]'NODE=(\S+)').Match($out).Groups[1].Value
  if ($out -match 'errno=13' -and $node) {
    wsl.exe -d $distro -- sudo -n chmod 666 $node 2>$null
    if ($LASTEXITCODE -eq 0) { $out = (wsl.exe -d $distro -- $py $pyWsl 2>&1 | Out-String) }
  }
  return $out
}

Write-Host "SB20-proxy ride-readiness doctor" -ForegroundColor Cyan
Write-Host "--------------------------------"

# 1. BLE venv + bleak (BLE captures)
$blePy = Join-Path $repo "code\.venv\Scripts\python.exe"
$bleakV = $null
if (Test-Path $blePy) {
  $bleakV = & $blePy -c "import bleak; from importlib.metadata import version; print(version('bleak'))" 2>$null
}
Check "BLE venv + bleak" ([bool]$bleakV) $(if ($bleakV) { "-> bleak $bleakV" } else { "- missing; run provision-dev-env.ps1" })

# 2. PlatformIO venv + version (firmware build/flash)
$pio = Join-Path $repo "firmware\.venv\Scripts\platformio.exe"
$pioV = $null
if (Test-Path $pio) { $pioV = (& $pio --version 2>$null) }
Check "PlatformIO" ([bool]$pioV) $(if ($pioV) { "-> $pioV" } else { "- missing; run provision-dev-env.ps1" })

# 3. ESP32 platform cached (so a build doesn't trigger a fresh hundreds-of-MB download mid-session)
$espCached = $false
if ($pioV) {
  $platforms = & $pio platform list --json-output 2>$null | Out-String
  $espCached = $platforms -match "espressif32"
}
Check "ESP32 toolchain cached" $espCached $(if ($espCached) { "-> espressif32 installed" } else { "- run provision-dev-env.ps1 -WarmToolchain" })

# 4. Host C++ compiler (only needed for `pio test -e native` locally; CI runs them regardless)
$gcc = Get-Command gcc -ErrorAction SilentlyContinue
if ($gcc) { Result "host C++ compiler (native tests)" 'PASS' "-> $($gcc.Source)" }
else      { Result "host C++ compiler (native tests)" 'WARN' "- no gcc; 'pio test -e native' won't run here (CI still does)" }

# ----------------------------------------------------------------------------------------------------
# Capture rig - the always-on dual-radio sniff (standing pre-flight rule; PLAYBOOK §pre-flight).
#   nRF BLE sniffer -> pcap   (Npcap + the nRF Sniffer for Bluetooth LE extcap + the dongle on a COM port)
#   ANT+ stick      -> JSONL  (usbipd shares the 0fcf:1008 stick into WSL; openant claims it over libusb)
# A green rig here means BOTH radios can actually capture. This is the gate session 9's retro asked for.
# ----------------------------------------------------------------------------------------------------
if (-not $NoCaptureRig) {
  Write-Host "`nCapture rig (dual-radio sniff)" -ForegroundColor Cyan
  Write-Host "------------------------------"

  # ---------- nRF BLE sniffer path (Windows-native) ----------
  if ($NoNrf) {
    Result "nRF BLE sniffer" 'WARN' "- skipped (-NoNrf: intentionally absent this ride)"
  } else {
    $tshark = (Get-Command tshark -ErrorAction SilentlyContinue).Source
    if (-not $tshark -and (Test-Path 'C:\Program Files\Wireshark\tshark.exe')) { $tshark = 'C:\Program Files\Wireshark\tshark.exe' }

    if (-not $tshark) {
      Result "Wireshark / tshark" 'FAIL' "- not found; install Wireshark - tools/README.md > Capture rig"
    } else {
      # `tshark -D` exits non-zero (13) when Npcap can't load, so capture stdout+stderr regardless of exit code.
      $td = (& $tshark -D 2>&1 | Out-String)
      # (a) Npcap installed + tshark can load wpcap
      if ($td -match 'Unable to load Npcap' -or $td -match 'wpcap\.dll') {
        Result "Npcap (wpcap.dll loads)" 'FAIL' "- tshark can't load Npcap; install it (admin installer + reboot) - tools/README.md > Capture rig"
      } else {
        Result "Npcap (wpcap.dll loads)" 'PASS' "-> tshark loads wpcap"
      }
      # (b) nRF Sniffer for Bluetooth LE extcap registered (tshark -D lists it)
      if ($td -match 'nRF Sniffer') {
        Result "nRF Sniffer extcap" 'PASS' "-> tshark -D lists the nRF Sniffer interface"
      } else {
        Result "nRF Sniffer extcap" 'FAIL' "- not registered (tshark -D shows no nRF Sniffer); install+register the extcap - tools/README.md > Capture rig"
      }
    }
    # (c) the nRF dongle is on a COM port (Nordic VID_1915, sniffer firmware enumerates a CDC-ACM serial port)
    $nordic = Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
      Where-Object { $_.DeviceID -like '*VID_1915*' -and $_.Name -match '\(COM\d+\)' } | Select-Object -First 1
    if ($nordic) {
      $com    = ([regex]'COM\d+').Match($nordic.Name).Value
      $nrfPid = ([regex]'PID_([0-9A-Fa-f]{4})').Match($nordic.DeviceID).Groups[1].Value
      Result "nRF dongle (COM port)" 'PASS' "-> Nordic VID_1915 PID_$nrfPid on $com"
    } else {
      Result "nRF dongle (COM port)" 'FAIL' "- no Nordic VID_1915 serial port; plug the dongle in / flash the Sniffer firmware - tools/README.md > Capture rig"
    }
  }

  # ---------- ANT+ path (usbipd -> WSL libusb claim) ----------
  if ($NoAnt) {
    Result "ANT+ capture" 'WARN' "- skipped (-NoAnt: intentionally absent this ride)"
  } elseif (-not (Get-Command usbipd -ErrorAction SilentlyContinue)) {
    Result "ANT+ usbipd" 'FAIL' "- usbipd not installed; needed to share the ANT stick into WSL - wsl-capture-runbook.md"
  } else {
    $ul = (usbipd list 2>&1 | Out-String)
    $line = ($ul -split "`r?`n") | Where-Object { $_ -match '0fcf:1008' } | Select-Object -First 1
    if (-not $line) {
      Result "ANT+ stick (0fcf:1008)" 'FAIL' "- ANTUSB2 not present in 'usbipd list'; plug it in"
    } else {
      $busid = ($line.Trim() -split '\s+')[0]
      $stState = if ($line -match 'Attached') { 'Attached' } elseif ($line -match 'Shared') { 'Shared' } else { 'Not shared' }
      Result "ANT+ stick (0fcf:1008)" 'PASS' "-> busid $busid, state '$stState'"

      # Ensure it's attached into WSL (attach needs no admin once the stick is Shared).
      $attached = $stState -eq 'Attached'
      if (-not $attached -and $stState -eq 'Shared') {
        Write-Host ("  [..]   {0,-32} attaching busid {1} into WSL {2}..." -f "ANT+ attach to WSL", $busid, $WslDistro) -ForegroundColor DarkGray
        usbipd attach --wsl --busid $busid 2>&1 | Out-Null
        Start-Sleep -Milliseconds 800
        $attached = (((usbipd list 2>&1 | Out-String) -split "`r?`n") | Where-Object { $_ -match '0fcf:1008' }) -match 'Attached'
      }
      if (-not $attached) {
        $hint = if ($stState -eq 'Not shared') { "run as admin: usbipd bind --busid $busid" } else { "usbipd attach --wsl --busid $busid" }
        Result "ANT+ attached to WSL" 'FAIL' "- not attached into WSL; $hint"
      } else {
        # WSL libusb claim test - prove openant can actually open the stick (not just that it's attached).
        wsl.exe -d $WslDistro -- true 2>$null
        if ($LASTEXITCODE -ne 0) {
          Result "ANT+ WSL libusb claim" 'FAIL' "- WSL distro '$WslDistro' not available (wsl -d $WslDistro)"
        } else {
          $claim = Invoke-WslAntClaim $WslDistro $WslRepo
          $node  = ([regex]'NODE=(\S+)').Match($claim).Groups[1].Value
          $perms = ([regex]'PERMS=(\S+)').Match($claim).Groups[1].Value
          if ($claim -match 'RESULT=OK') {
            Result "ANT+ WSL libusb claim" 'PASS' "-> openant can claim 0fcf:1008 in $WslDistro ($node)"
          } elseif ($claim -match 'errno=13') {
            Result "ANT+ WSL libusb claim" 'FAIL' "- node $node is root-only (perms $perms); fix: wsl -d $WslDistro -- sudo chmod 666 $node  (runbook S1), or enable systemd"
          } elseif ($claim -match 'errno=16') {
            Result "ANT+ WSL libusb claim" 'FAIL' "- stick busy: a zombie capture holds it (runbook S3); free it: wsl -d $WslDistro -- fuser -k $node"
          } elseif ($claim -match 'RESULT=NODEV') {
            Result "ANT+ WSL libusb claim" 'FAIL' "- attached on Windows but not visible in WSL lsusb; detach + re-attach the busid"
          } elseif ($claim -match 'RESULT=IMPORTFAIL') {
            Result "ANT+ WSL libusb claim" 'FAIL' "- pyusb/openant not importable in the WSL venv; pip install -e 'code[ble]' in $WslRepo"
          } else {
            Result "ANT+ WSL libusb claim" 'FAIL' "- claim failed: $(($claim -split "`r?`n" | Where-Object { $_ }) -join ' ')"
          }
        }
      }
    }
  }
}

# 5. (optional) board reachable + ArduinoOTA port
if ($BoardIp) {
  $alive = $false
  try { Invoke-WebRequest "http://$BoardIp/status" -TimeoutSec 5 -UseBasicParsing | Out-Null; $alive = $true } catch {}
  Check "board /status reachable" $alive "-> http://$BoardIp/status"
  if (-not $alive) {
    Write-Host "  [INFO] a freshly-rebooted C3 needs ~25 s before its HTTP server rebinds (ping/BLE come up first) - re-check before calling it dead." -ForegroundColor Cyan
  }
  # ArduinoOTA listens on UDP 3232; we can't TCP-probe it, but note the multi-NIC espota gotcha.
  $lanIps = (Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue | Where-Object { $_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.254.*' }).IPAddress
  Write-Host ("  [INFO] OTA host-IP candidates (use espota -I <one on the board's subnet>): {0}" -f ($lanIps -join ', ')) -ForegroundColor Cyan
}

if ($NoCaptureRig) {
  Write-Host "`n[NOTE] Capture rig NOT checked (-NoCaptureRig) - this run verified the build/flash toolchain only." -ForegroundColor Yellow
}
if ($fail) {
  Write-Host "`n$fail required check(s) FAILED - fix at the desk before relying on a session at the bike." -ForegroundColor Red
  Write-Host "Fix: build/flash -> .\tools\provision-dev-env.ps1 [-WarmToolchain];  capture rig -> tools/README.md > Capture rig + wsl-capture-runbook.md" -ForegroundColor Yellow
  exit 1
}
if ($warn) {
  Write-Host "`n$warn warning(s) above - each is an intentionally-absent radio or an optional piece; confirm that's expected." -ForegroundColor Yellow
}
if ($NoCaptureRig) {
  Write-Host "`nBuild + flash toolchain ready (capture rig NOT checked this run)." -ForegroundColor Green
} elseif ($NoNrf -and $NoAnt) {
  Write-Host "`nBuild + flash ready, but NEITHER radio was verified (-NoNrf -NoAnt) - capture rig NOT certified." -ForegroundColor Yellow
} elseif ($NoNrf) {
  Write-Host "`nReady: build, flash, AND ANT+ capture verified (nRF sniffer skipped - confirm that's intended)." -ForegroundColor Green
} elseif ($NoAnt) {
  Write-Host "`nReady: build, flash, AND nRF BLE capture verified (ANT+ skipped - confirm that's intended)." -ForegroundColor Green
} else {
  Write-Host "`nRide-ready: can build, flash, AND capture on BOTH radios (nRF + ANT+) on this machine." -ForegroundColor Green
}
