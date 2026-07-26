<#
.SYNOPSIS
  Tests for tools/PioIni.ps1 -- the module both flash.ps1 scripts use to decide whether an env is
  safe to leave on a board someone intends to RIDE.

.DESCRIPTION
  This file exists because of a shipped regression. PR #302 added the ride-blocker guard but nothing
  covered it, and `Get-PioSectionBanner` treated a blank line as "keep accumulating". The REMOVED
  `esp32s3-touch` tombstone comment therefore bled across the blank separator into
  [env:esp32c3-wifi], and from there -- through `extends` lineage -- marked ALL 15 C3 envs
  SUPERSEDED. `flash.ps1` refused to flash the owner's actual ride build, while its own remediation
  text recommended that same env. A guard that cries wolf on the good build teaches you to always
  pass -Force, which is strictly worse than no guard at all.

  No Pester: `pwsh` alone is on every GitHub runner, so this stays a zero-dependency CI step.
  The banner/blocker unit tests use synthetic fixtures and need no PlatformIO. The integration
  tests read the repo's real platformio.ini files and DO need it -- that half is what would have
  caught the regression, so CI runs with -RequireReal to make skipping it impossible.

.EXAMPLE
  pwsh -File tools/tests/Test-PioIni.ps1 -RequireReal
#>
param([switch]$RequireReal)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\PioIni.ps1')

$script:Failures = @()
$script:Passed = 0

function Check {
    param([Parameter(Mandatory)][string]$Name, [Parameter(Mandatory)][scriptblock]$Body)
    try {
        & $Body
        $script:Passed++
        Write-Host "  PASS  $Name"
    } catch {
        $script:Failures += "$Name -- $($_.Exception.Message)"
        Write-Host "  FAIL  $Name" -ForegroundColor Red
        Write-Host "        $($_.Exception.Message)" -ForegroundColor Red
    }
}

function Assert-Equal {
    param($Expected, $Actual, [string]$Because = '')
    if ("$Expected" -ne "$Actual") { throw "expected '$Expected', got '$Actual'. $Because" }
}

# ---------------------------------------------------------------------------
# Get-PioSectionBanner -- pure text, synthetic fixture
# ---------------------------------------------------------------------------
$fixture = Join-Path ([System.IO.Path]::GetTempPath()) "pioini-test-$([guid]::NewGuid()).ini"
@'
; ---- tombstone for a DELETED family ----
; Superseded on 2026-07-03 by the envs above.

; ---- the good build ----
; This one is fine to ride.
[env:good]
board = seeed_xiao

; ---- genuinely retired ----
; SUPERSEDED: do not use.
[env:retired]
board = seeed_xiao

[env:no-banner]
extends = env:good
build_flags = -DFOO

; a comment, then a real option, then the header
; second comment line
some_option = 1
[env:option-breaks]
board = seeed_xiao
'@ | Set-Content -LiteralPath $fixture -Encoding UTF8

Write-Host "Get-PioSectionBanner"

Check 'a blank line ENDS a banner (the PR #302 regression)' {
    $b = Get-PioSectionBanner -IniPath $fixture -Section 'env:good'
    if ($b -match '(?i)superseded') {
        throw "the deleted family's tombstone bled across the blank line into [env:good]: $b"
    }
    if ($b -notmatch 'fine to ride') { throw "lost [env:good]'s own banner: '$b'" }
}

Check 'a superseded banner directly above a section IS still detected' {
    $b = Get-PioSectionBanner -IniPath $fixture -Section 'env:retired'
    if ($b -notmatch '(?i)superseded') { throw "missed a real SUPERSEDED banner: '$b'" }
}

Check 'a section with no comment block above it gets an empty banner' {
    Assert-Equal '' (Get-PioSectionBanner -IniPath $fixture -Section 'env:no-banner')
}

Check 'a real option line also ends a banner' {
    $b = Get-PioSectionBanner -IniPath $fixture -Section 'env:option-breaks'
    Assert-Equal '' $b 'comments above an option belong to that option, not the next header'
}

Check 'an unknown section yields an empty banner rather than throwing' {
    Assert-Equal '' (Get-PioSectionBanner -IniPath $fixture -Section 'env:nope')
}

# ---------------------------------------------------------------------------
# Get-PioEnvRideBlockers -- synthetic config, no PlatformIO needed
# ---------------------------------------------------------------------------
Write-Host "Get-PioEnvRideBlockers"

$cfg = @{
    'env:base'       = @{ board = 'seeed_xiao'; build_flags = '-DUSE_MOCK_METER=1' }
    'env:live'       = @{ board = 'seeed_xiao'; extends = 'env:base'; build_flags = '-DUSE_MOCK_METER=0' }
    'env:live-ota'   = @{ board = 'seeed_xiao'; extends = 'env:live' }
    'env:bench'      = @{ board = 'seeed_xiao'; extends = 'env:live'; build_flags = '-DMETER_MATCH_ANY_CPS' }
    'env:some-probe' = @{ board = 'seeed_xiao'; extends = 'env:live' }
    'env:native'     = @{ build_flags = '-DUSE_MOCK_METER=0' }
}

Check 'a live build three extends hops from the mock flag is SHIPPABLE' {
    $b = Get-PioEnvRideBlockers -Config $cfg -EnvName 'live-ota'
    if ($b.Count -ne 0) { throw "expected no blockers, got: $($b -join ' | ')" }
}

Check 'a child -DMACRO=0 OVERRIDES the parent, it does not union with it' {
    Assert-Equal '0' (Get-PioEnvDefine -Config $cfg -EnvName 'live' -Macro 'USE_MOCK_METER')
}

Check 'a mock build is blocked' {
    $b = Get-PioEnvRideBlockers -Config $cfg -EnvName 'base'
    if ($b -notmatch 'MOCK') { throw "expected a MOCK blocker, got: $($b -join ' | ')" }
}

Check 'a bench build (pairs with any stranger CPS meter) is blocked' {
    $b = Get-PioEnvRideBlockers -Config $cfg -EnvName 'bench'
    if ($b -notmatch 'BENCH') { throw "expected a BENCH blocker, got: $($b -join ' | ')" }
}

Check 'a probe build is blocked by name' {
    $b = Get-PioEnvRideBlockers -Config $cfg -EnvName 'some-probe'
    if ($b -notmatch 'PROBE') { throw "expected a PROBE blocker, got: $($b -join ' | ')" }
}

Check 'a host-test env (no board key) is blocked -- there is nothing to flash' {
    $b = Get-PioEnvRideBlockers -Config $cfg -EnvName 'native'
    if ($b -notmatch 'host-test') { throw "expected a host-test blocker, got: $($b -join ' | ')" }
}

Check 'a SUPERSEDED banner on an ANCESTOR propagates down the lineage' {
    $b = Get-PioEnvRideBlockers -Config $cfg -EnvName 'live-ota' -IniPath $fixture
    # 'env:base'/'env:live' are absent from the fixture ini, so nothing should be flagged...
    if ($b -match 'SUPERSEDED') { throw "flagged an env whose lineage has no banner: $($b -join ' | ')" }

    $lineageCfg = @{
        'env:retired' = @{ board = 'seeed_xiao' }
        'env:child'   = @{ board = 'seeed_xiao'; extends = 'env:retired' }
    }
    $b2 = Get-PioEnvRideBlockers -Config $lineageCfg -EnvName 'child' -IniPath $fixture
    if ($b2 -notmatch 'SUPERSEDED') { throw "a child of a superseded env must inherit the blocker" }
}

Remove-Item -LiteralPath $fixture -Force -ErrorAction SilentlyContinue

# ---------------------------------------------------------------------------
# Integration -- the REAL platformio.ini files. Needs PlatformIO.
# This is the half that would have caught the shipped regression: the synthetic tests all passed
# against the buggy code because the bug only appears in a file where one env's banner sits a
# blank line above the next env's.
# ---------------------------------------------------------------------------
Write-Host "Real platformio.ini (integration)"

$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..')

# env -> must it be flashable onto a bike?  Named in flash.ps1's own remediation text and BENCH-FLASH.md.
$rideBuilds = @{
    'firmware'     = @('esp32c3-oled-live-ota', 'esp32c3-oled-live', 'esp32cyd-live')
    'firmware-nrf' = @('xiao-sense')
}
$mustBlock = @{
    'firmware'     = @('esp32c3-wifi', 'esp32c3-wifi-live-bench', 'native')
    'firmware-nrf' = @('native')
}

$pioAvailable = $true
foreach ($proj in @('firmware', 'firmware-nrf')) {
    $dir = Join-Path $repo $proj
    $ini = Join-Path $dir 'platformio.ini'
    if (-not (Test-Path $ini)) { continue }

    $real = $null
    $why = ''
    try { $real = Get-PioConfig -ProjectDir $dir } catch { $pioAvailable = $false; $why = $_.Exception.Message }
    if (-not $real) {
        if ($RequireReal) {
            $script:Failures += "$proj -- PlatformIO unavailable but -RequireReal was given: $why"
            Write-Host "  FAIL  $proj : PlatformIO unavailable but -RequireReal was given" -ForegroundColor Red
            Write-Host "        $why" -ForegroundColor Red
        } else {
            Write-Host "  SKIP  $proj : PlatformIO not available on this machine ($why)"
        }
        continue
    }

    foreach ($e in $rideBuilds[$proj]) {
        Check "$proj : '$e' is SHIPPABLE (a rider must not need -Force)" {
            if ((Get-PioEnvNames -Config $real) -notcontains $e) {
                throw "no [env:$e] in $proj/platformio.ini -- rename it here and in BENCH-FLASH.md together"
            }
            $b = Get-PioEnvRideBlockers -Config $real -EnvName $e -IniPath $ini
            if ($b.Count -ne 0) { throw "blocked: $($b -join ' | ')" }
        }
    }
    foreach ($e in $mustBlock[$proj]) {
        Check "$proj : '$e' is REFUSED without -Force" {
            if ((Get-PioEnvNames -Config $real) -notcontains $e) { return }   # env retired: nothing to guard
            $b = Get-PioEnvRideBlockers -Config $real -EnvName $e -IniPath $ini
            if ($b.Count -eq 0) { throw "the guard let a non-shippable build through" }
        }
    }
}

Write-Host ""
if ($script:Failures.Count -gt 0) {
    Write-Host "FAILED: $($script:Failures.Count) of $($script:Passed + $script:Failures.Count)" -ForegroundColor Red
    exit 1
}
Write-Host "OK: $($script:Passed) checks passed$(if (-not $pioAvailable) { ' (PlatformIO integration skipped)' })" -ForegroundColor Green
exit 0
