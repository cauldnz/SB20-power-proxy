<#
.SYNOPSIS
  Flash an nRF52840 board over its UF2 bootloader, refusing images whose SoftDevice layout
  disagrees with the SoftDevice actually on the board.

.DESCRIPTION
  An nRF52 application is linked to start immediately above the SoftDevice, so the app's base
  address is a property of the *stack on the board*, not of the source. `[env:xiao-sense]` inherits
  the board's default **S140 v7** layout (app at 0x27000 — measured from the Seeed core's
  `nrf52840_s140_v7.ld`; earlier docs in this repo said 0x26000, which is the *v6* value). The bench
  XIAO carries **S340 6.1.1**, whose region ends at 0x31000. Flashing the former onto the latter lands
  the application *inside* the SoftDevice: it does not fail loudly, it produces a board that no longer
  works and gives no clue why.

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
        throw "no UF2 bootloader drive found. Double-tap RST on the board, then re-run. (A 1200-baud touch enters the bootloader too, but measured 2026-07-27 on this XIAO it brings up only the CDC interface - Windows never enumerates the mass-storage one - so the drive never appears. Double-tap RST is the reliable way in.)"
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

# ---- 5b. get a .uf2 -------------------------------------------------------------------------------
# The Adafruit builder emits firmware.uf2 for the stock envs, but not for one carrying a custom
# board_build.ldscript (xiao-sense-s340 produces only .hex/.zip). Converting the .hex ourselves is
# deterministic, so do it rather than dead-ending the flash.
$uf2 = Join-Path $fw ".pio\build\$Env\firmware.uf2"
$hex = Join-Path $fw ".pio\build\$Env\firmware.hex"
if (-not (Test-Path $uf2)) {
    if (-not (Test-Path $hex)) {
        throw "'$Env' produced neither firmware.uf2 nor firmware.hex at $(Split-Path $uf2). Build it first (drop -NoBuild)."
    }
    $conv = Get-ChildItem (Join-Path $env:USERPROFILE ".platformio\packages") -Recurse -Filter "uf2conv.py" -ErrorAction SilentlyContinue |
        Sort-Object { $_.FullName -notmatch 'seeed' } | Select-Object -First 1
    if (-not $conv) { throw "no firmware.uf2 and no uf2conv.py in ~/.platformio/packages to convert firmware.hex with." }
    Say "No .uf2 for '$Env' (custom ldscript); converting firmware.hex ..." "DarkGray"
    & python $conv.FullName -f 0xADA52840 -c -o $uf2 $hex 2>&1 | Where-Object { $_ -notmatch 'SyntaxWarning|re\.split' } | ForEach-Object { Say "  $_" "DarkGray" }
    if (-not (Test-Path $uf2)) { throw "uf2conv.py did not produce $uf2." }
}

# ---- 5c. assert the *image* was linked where the ldscript says --------------------------------------
# Step 4 compares the ldscript's *filename* to the board. It cannot tell whether that ldscript was
# actually honoured. If board_build.ldscript is silently ignored (typo, path, builder change) the app
# links at the core default instead, and you flash a wrongly-based image onto an S340 board believing
# the name-check cleared it - #298's exact failure, undetected. A UF2 block header carries its target
# address at offset 12, so the artefact states its own base and cannot lie. Compare the two.
$hdr = [byte[]]::new(16)
$fs = [System.IO.File]::OpenRead($uf2)
try { $null = $fs.Read($hdr, 0, 16) } finally { $fs.Dispose() }
if ([BitConverter]::ToUInt32($hdr, 0) -ne 0x0A324655) { throw "$uf2 is not a UF2 file (bad magic)." }
$imageBase = [BitConverter]::ToUInt32($hdr, 12)

# Expected base is read from the ldscript in use - never hardcoded, so it cannot go stale. (Measured
# 2026-07-27: S340 v6 = 0x31000, but the Seeed core's S140 *v7* default = 0x27000, not the 0x26000
# this repo's docs long claimed - a hardcoded table would have falsely refused every stock build.)
$ldPath = if ($ld) { if ([System.IO.Path]::IsPathRooted($ld)) { $ld } else { Join-Path $fw $ld } } else { $null }
if ($ldPath -and (Test-Path $ldPath) -and ((Get-Content $ldPath -Raw) -match '(?m)^\s*FLASH\s*\(\w+\)\s*:\s*ORIGIN\s*=\s*(0x[0-9A-Fa-f]+)')) {
    $want = [Convert]::ToUInt32($Matches[1], 16)
    Say ("Image base      : 0x{0:X} (ldscript ORIGIN 0x{1:X})" -f $imageBase, $want)
    if ($imageBase -ne $want) {
        Say ""
        Say ("MISMATCH: '$Env' set board_build.ldscript to $(Split-Path $ldPath -Leaf) (ORIGIN 0x{0:X})," -f $want) "Red"
        Say ("  but the built image is based at 0x{0:X} - the ldscript did NOT take effect." -f $imageBase) "Red"
        Say "  The SoftDevice name-check above passed on the ldscript's *name*, so it cannot save you" "Yellow"
        Say "  here. Flashing this puts the application at the wrong address. See issue #298." "Yellow"
        if (-not $Force) { throw "refusing to flash: image base 0x$('{0:X}' -f $imageBase) != ldscript ORIGIN 0x$('{0:X}' -f $want)." }
        Say "-Force given: proceeding with a wrongly-based image." "Red"
    }
} else {
    Say ("Image base      : 0x{0:X} (no explicit ldscript; core default)" -f $imageBase) "DarkGray"
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
