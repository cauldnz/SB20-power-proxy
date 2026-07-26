<#
.SYNOPSIS
  Flash an nRF52840 board over its UF2 bootloader, refusing images whose SoftDevice layout
  disagrees with the SoftDevice actually on the board.

.DESCRIPTION
  An nRF52 application is linked to start immediately above the SoftDevice, so the app's base
  address is a property of the *stack on the board*, not of the source. `[env:xiao-sense]` inherits
  the board's default **S140** layout (app at 0x26000). The bench XIAO carries **S340 6.1.1**, whose
  region ends at 0x31000. Flashing the former onto the latter lands the application *inside* the
  SoftDevice: it does not fail loudly, it produces a board that no longer works and gives no clue why.

  Nothing in the toolchain checks this. `pio run -t upload` will happily do it, and on 2026-07-26 an
  attempt to do exactly that erased the bench XIAO's application before failing for an unrelated
  reason (issue #297/#298). The bootloader publishes the answer in plain text — `INFO_UF2.TXT` —
  so this script reads it and refuses on mismatch.

  It also refuses to flash while the board still has no app, unless you say so, and never guesses:
  if it cannot identify the SoftDevice it stops rather than assuming.

.PARAMETER Env
  PlatformIO env in firmware-nrf/ (e.g. xiao-sense-s340).

.PARAMETER Drive
  The UF2 bootloader drive (e.g. D:). Auto-detected when omitted.

.PARAMETER Force
  Flash anyway despite a refusal. Prints in red what you are overriding.

.PARAMETER NoBuild
  Use the already-built .uf2/.hex instead of rebuilding.

.EXAMPLE
  .\flash.ps1 -Env xiao-sense-s340
  .\flash.ps1 -Env xiao-sense -Drive D: -Force     # you have read the warning and mean it
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Env,
    [string]$Drive,
    [switch]$Force,
    [switch]$NoBuild
)
$ErrorActionPreference = "Stop"
$fw = $PSScriptRoot
function Say($msg, $color = "Cyan") { Write-Host $msg -ForegroundColor $color }

. (Join-Path (Split-Path $fw -Parent) "tools\PioIni.ps1")

# ---- 1. the env must exist -----------------------------------------------------------------------
$cfg = Get-PioConfig -ProjectDir $fw
$envNames = Get-PioEnvNames -Config $cfg
if ($envNames -notcontains $Env) {
    throw "no [env:$Env] in firmware-nrf/platformio.ini. Available: $($envNames -join ', ')"
}
if (-not $cfg["env:$Env"].ContainsKey('board')) {
    throw "'$Env' is a host-test env (no 'board'); there is nothing to flash. Did you mean xiao-sense-s340?"
}

# ---- 2. find the bootloader ----------------------------------------------------------------------
if (-not $Drive) {
    $candidates = @(Get-CimInstance Win32_LogicalDisk -Filter "DriveType=2" |
        Where-Object { Test-Path (Join-Path $_.DeviceID "INFO_UF2.TXT") })
    if ($candidates.Count -eq 0) {
        throw "no UF2 bootloader drive found. Double-tap RST on the board, then re-run. (A 1200-baud touch also enters the bootloader.)"
    }
    if ($candidates.Count -gt 1) {
        throw "more than one UF2 drive present ($($candidates.DeviceID -join ', ')). Pass -Drive to say which."
    }
    $Drive = $candidates[0].DeviceID
}
$info = Join-Path $Drive "INFO_UF2.TXT"
if (-not (Test-Path $info)) { throw "$Drive is not a UF2 bootloader (no INFO_UF2.TXT)." }
$infoText = Get-Content $info -Raw
Say "Bootloader on ${Drive}:"
$infoText.Trim() -split "`r?`n" | ForEach-Object { Say "  $_" "DarkGray" }

# ---- 3. what SoftDevice is actually on the board? -------------------------------------------------
# Never guess. An unreadable INFO_UF2.TXT means we do not know the app base address, and a wrong
# base address is precisely the failure this script exists to prevent.
if ($infoText -notmatch '(?m)^\s*SoftDevice:\s*(\S+)') {
    throw "INFO_UF2.TXT has no 'SoftDevice:' line, so the board's stack cannot be identified. Refusing to guess. Use -Force only if you know the layout is right."
}
$boardSd = $Matches[1].ToUpper()      # e.g. S340
Say "Board SoftDevice : $boardSd"

# ---- 4. what SoftDevice does this env's link layout assume? ---------------------------------------
# The explicit ldscript wins; otherwise the board default (S140 on both nRF52840 boards here).
$ld = $cfg["env:$Env"]['board_build.ldscript']
if ($ld) {
    if ($ld -match '(?i)s(\d{3})') { $envSd = "S$($Matches[1])" } else { $envSd = $null }
    $layoutSource = "ldscript $(Split-Path $ld -Leaf)"
} else {
    $envSd = "S140"
    $layoutSource = "board default (no board_build.ldscript)"
}
Say "Env  SoftDevice : $(if ($envSd) { $envSd } else { 'UNKNOWN' })  [$layoutSource]"

if (-not $envSd) {
    throw "cannot tell which SoftDevice '$Env' is linked against (ldscript '$ld' has no sNNN in its name). Refusing to guess."
}
if ($envSd -ne $boardSd) {
    Say "" 
    Say "MISMATCH: '$Env' is linked for $envSd but this board runs $boardSd." "Red"
    Say "  An nRF52 app starts immediately above the SoftDevice, so a $envSd-linked image lands at the" "Yellow"
    Say "  wrong address on a $boardSd board - inside the SoftDevice. It will not fail loudly; it will" "Yellow"
    Say "  just stop working. See issue #298." "Yellow"
    $alt = $envNames | Where-Object { $_ -match "(?i)$boardSd" }
    if ($alt) { Say "  Try instead: $($alt -join ', ')" "Yellow" }
    if (-not $Force) { throw "refusing to flash a $envSd image onto a $boardSd board." }
    Say "-Force given: proceeding into a known-bad layout." "Red"
}

# ---- 5. build ------------------------------------------------------------------------------------
if (-not $NoBuild) {
    Say "Building $Env ..."
    $pio = Get-PioCommand
    & $pio.File @($pio.PreArgs + @("run", "-e", $Env, "-d", $fw))
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
}

$uf2 = Join-Path $fw ".pio\build\$Env\firmware.uf2"
if (-not (Test-Path $uf2)) {
    throw "no firmware.uf2 for '$Env' at $uf2. (The nRF envs emit .uf2 via the Adafruit builder; if only a .hex exists, this env is not set up for UF2 flashing.)"
}

# ---- 6. flash ------------------------------------------------------------------------------------
Say "Copying $(Split-Path $uf2 -Leaf) ($([math]::Round((Get-Item $uf2).Length/1KB)) KB) to $Drive ..."
Copy-Item $uf2 (Join-Path $Drive "firmware.uf2") -Force

Say "Waiting for the board to leave the bootloader ..."
$left = $false
foreach ($i in 1..30) {
    Start-Sleep -Seconds 1
    if (-not (Test-Path $info)) { $left = $true; break }
}
if ($left) {
    Say "Flashed. The bootloader drive is gone, so the board reset into the application." "Green"
} else {
    Say "The bootloader drive is still mounted after 30s." "Yellow"
    Say "That usually means the image contains no valid application for this layout - the bootloader" "Yellow"
    Say "has nothing to jump to and stays in DFU. Re-check the SoftDevice/linker pairing above." "Yellow"
    exit 1
}
