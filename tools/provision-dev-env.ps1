<#
.SYNOPSIS
  Provision this machine's SB20-proxy dev toolchain to a known-good, PINNED state. Idempotent.

.DESCRIPTION
  Work moves between the desk and the bike laptop (different machines), and a Python upgrade can
  silently orphan the build toolchain (session 8, 2026-06-25: a Py 3.14 upgrade left no PlatformIO and
  no host compiler -> ~30 min of mid-session bring-up). This script makes the toolchain reproducible.

  It creates two repo-local venvs (both gitignored via `.venv/`):
    code/.venv      -> BLE capture tooling (bleak)        : 06_capture_ble.py, scans
    firmware/.venv  -> PlatformIO (firmware build/flash)  : pio run, espota

  Versions are pinned in tools/dev-env.lock. Re-running is safe (skips what's present, upgrades pins).
  Verify afterwards with tools/doctor.ps1.

  Windows-focused (the bike machine is native Windows). Linux/WSL desk setup is the editable install
  in code/ (see tools/README.md / the repo CLAUDE.md "Setup" section).

.EXAMPLE
  .\tools\provision-dev-env.ps1                  # create/refresh both venvs (fast)
.EXAMPLE
  .\tools\provision-dev-env.ps1 -WarmToolchain   # also build once to cache the ESP32 toolchain (slow 1st time)
.EXAMPLE
  .\tools\provision-dev-env.ps1 -SkipPio         # BLE tooling only (e.g. a capture-only machine)
#>
param(
  [switch]$WarmToolchain,   # also run a firmware build so the ESP32 toolchain is cached in ~/.platformio
  [switch]$SkipBle,
  [switch]$SkipPio
)
$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent

# --- pinned versions (KEEP IN SYNC with tools/dev-env.lock) — the known-good session-8 set ---
$BLEAK = "bleak==3.0.2"
$PIO   = "platformio==6.1.19"

function Say($m, $c = "Cyan") { Write-Host $m -ForegroundColor $c }

# Pick a base Python (3.10+). Prefer the py launcher (handles multiple installs); fall back to python.
$basePy = if (Get-Command py -ErrorAction SilentlyContinue) { "py" } `
          elseif (Get-Command python -ErrorAction SilentlyContinue) { "python" } `
          else { throw "No Python found on PATH (need 3.10+). Install Python, then re-run." }
Say "Base Python: $basePy ($(& $basePy -V 2>&1))"

function New-VenvWith($venvPath, $pkgSpec, $label) {
  $venvPy = Join-Path $venvPath "Scripts\python.exe"
  if (-not (Test-Path $venvPy)) {
    Say "Creating venv: $venvPath"
    & $basePy -m venv $venvPath
  } else {
    Say "venv exists: $venvPath (refreshing $label)"
  }
  & $venvPy -m pip install --quiet --upgrade pip | Out-Null
  & $venvPy -m pip install --quiet $pkgSpec
  if ($LASTEXITCODE -ne 0) { throw "pip install '$pkgSpec' failed in $venvPath" }
}

if (-not $SkipBle) {
  New-VenvWith (Join-Path $repo "code\.venv") $BLEAK "BLE tooling"
  Say "BLE venv ready (code\.venv): $BLEAK" Green
}

if (-not $SkipPio) {
  $pioVenv = Join-Path $repo "firmware\.venv"
  New-VenvWith $pioVenv $PIO "PlatformIO"
  Say "PlatformIO venv ready (firmware\.venv): $PIO" Green
  if ($WarmToolchain) {
    Say "Warming the ESP32 toolchain (first build downloads ~hundreds of MB into ~/.platformio)..."
    & (Join-Path $pioVenv "Scripts\platformio.exe") run -e esp32c3-oled-live -d (Join-Path $repo "firmware")
    if ($LASTEXITCODE -ne 0) { throw "warm build failed" }
    Say "ESP32 toolchain cached." Green
  } else {
    Say "  (run with -WarmToolchain to pre-download the ESP32 toolchain now; otherwise the first build does it)" Yellow
  }
}

# Host C++ compiler is needed for `pio test -e native` and is NOT pip-installable.
if (-not (Get-Command gcc -ErrorAction SilentlyContinue)) {
  Say "NOTE: no 'gcc' on PATH -> `pio test -e native` (host unit tests) won't run here." Yellow
  Say "      Install MinGW-w64 or VS Build Tools and put gcc/g++ on PATH if you want to run native tests locally (CI runs them regardless)." Yellow
}

Say "`nDone. Verify with:  .\tools\doctor.ps1" Green
