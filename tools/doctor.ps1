<#
.SYNOPSIS
  Build/flash toolchain pre-flight for SB20-proxy. Verifies each piece session 8 found missing.

.DESCRIPTION
  Run this at the DESK before relying on a firmware build/flash at the bike. It answers
  "can we actually build AND flash a firmware on THIS machine?" — the check whose absence cost a
  live session ~30 min (Py 3.14 had orphaned PlatformIO; no host compiler). Exits non-zero if any
  required check fails. Re-provision a failing env with tools/provision-dev-env.ps1.

.EXAMPLE
  .\tools\doctor.ps1
.EXAMPLE
  .\tools\doctor.ps1 -BoardIp 192.168.1.165    # also check the board is reachable + its OTA port
#>
param(
  [string]$BoardIp = ""    # optional: also verify the board responds and ArduinoOTA (UDP 3232) is reachable
)
$repo = Split-Path $PSScriptRoot -Parent
$fail = 0
function Check($name, $ok, $detail) {
  if ($ok) { Write-Host ("  [PASS] {0,-32} {1}" -f $name, $detail) -ForegroundColor Green }
  else     { Write-Host ("  [FAIL] {0,-32} {1}" -f $name, $detail) -ForegroundColor Red; $script:fail++ }
}
Write-Host "SB20-proxy dev-env doctor" -ForegroundColor Cyan
Write-Host "-------------------------"

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
if ($gcc) { Write-Host ("  [PASS] {0,-32} -> {1}" -f "host C++ compiler (native tests)", $gcc.Source) -ForegroundColor Green }
else      { Write-Host ("  [WARN] {0,-32} - no gcc; 'pio test -e native' won't run here (CI still does)" -f "host C++ compiler (native tests)") -ForegroundColor Yellow }

# 5. (optional) board reachable + ArduinoOTA port
if ($BoardIp) {
  $alive = $false
  try { Invoke-WebRequest "http://$BoardIp/status" -TimeoutSec 5 -UseBasicParsing | Out-Null; $alive = $true } catch {}
  Check "board /status reachable" $alive "-> http://$BoardIp/status"
  # ArduinoOTA listens on UDP 3232; we can't TCP-probe it, but note the multi-NIC espota gotcha.
  $lanIps = (Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue | Where-Object { $_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.254.*' }).IPAddress
  Write-Host ("  [INFO] OTA host-IP candidates (use espota -I <one on the board's subnet>): {0}" -f ($lanIps -join ', ')) -ForegroundColor Cyan
}

if ($fail) {
  Write-Host "`n$fail required check(s) FAILED — fix at the desk before relying on a build/flash at the bike." -ForegroundColor Red
  Write-Host "Fix: .\tools\provision-dev-env.ps1 [-WarmToolchain]" -ForegroundColor Yellow
  exit 1
}
Write-Host "`nBuild + flash toolchain ready." -ForegroundColor Green
