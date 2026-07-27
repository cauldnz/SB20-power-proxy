<#
.SYNOPSIS
  Answer questions about a PlatformIO environment's EFFECTIVE configuration.

.DESCRIPTION
  PlatformIO envs inherit. `esp32c3-oled-live-ota` is three `extends` hops from the flags that decide
  whether it reads a real meter or synthesises one, and `build_flags` in a child *replaces* the
  parent's rather than appending to it. Anything reasoning about "what is this env actually built
  with" — the flash guards, the doctor's pre-flight — has to resolve that, or it reasons about the
  wrong text.

  This module does NOT re-implement those semantics. It asks PlatformIO, via
  `pio project config --json-output`, which returns every option fully resolved (extends walked,
  `${section.key}` interpolated). A second model of someone else's inheritance rules is a model that
  drifts, and a guard built on a drifting model is worse than no guard: the first hand-rolled version
  of this file marked *every* env — including the recommended ride build — as a mock build, because a
  union-of-inherited-text scan cannot express an override.

  Dot-source it:  . "$PSScriptRoot\..\tools\PioIni.ps1"

.NOTES
  Requires PlatformIO on the machine (both callers build or flash, so this is not a new dependency).
#>

$script:PioConfigCache = @{}

function Get-PioCommand {
    # A hashtable, not an array: PowerShell unrolls a single-element array on return, so a one-element
    # @($exe) comes back as a bare string and $exe[0] silently becomes its first *character*.
    # $env:USERPROFILE is unset off Windows and Join-Path throws on a null -Path, so this probe has to
    # be guarded or the whole function dies before reaching the PATH fallback (which is how the CI
    # runner reported "PlatformIO unavailable" while pio was installed and on PATH).
    if ($env:USERPROFILE) {
        $penv = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\pio.exe"
        if (Test-Path $penv) { return @{ File = $penv; PreArgs = @() } }
    }
    $onPath = Get-Command pio -ErrorAction SilentlyContinue
    if ($onPath) { return @{ File = $onPath.Source; PreArgs = @() } }
    return @{ File = "python"; PreArgs = @("-m", "platformio") }   # the module in the active interpreter
}

<#
.SYNOPSIS
  The fully-resolved config for a PlatformIO project, as @{ 'env:name' = @{ key = value } }.
#>
function Get-PioConfig {
    param([Parameter(Mandatory)][string]$ProjectDir)

    $key = (Resolve-Path -LiteralPath $ProjectDir).Path
    if ($script:PioConfigCache.ContainsKey($key)) { return $script:PioConfigCache[$key] }

    $pio = Get-PioCommand
    $out = & $pio.File @($pio.PreArgs + @("project", "config", "--json-output", "-d", $key)) 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $out) {
        throw "could not read the PlatformIO config for '$key' (is PlatformIO installed? run tools\doctor.ps1)"
    }

    $cfg = @{}
    foreach ($pair in ($out | Out-String | ConvertFrom-Json)) {
        $opts = @{}
        foreach ($kv in $pair[1]) {
            $v = $kv[1]
            $opts[[string]$kv[0]] = if ($v -is [array]) { $v -join "`n" } else { [string]$v }
        }
        $cfg[[string]$pair[0]] = $opts
    }
    $script:PioConfigCache[$key] = $cfg
    return $cfg
}

<#
.SYNOPSIS
  Every declared [env:*] name.
#>
function Get-PioEnvNames {
    param([Parameter(Mandatory)][hashtable]$Config)
    return @($Config.Keys | Where-Object { $_ -like 'env:*' } | ForEach-Object { $_.Substring(4) } | Sort-Object)
}

<#
.SYNOPSIS
  The effective value of a -DMACRO in an env's build_flags, or $null if it is not defined.

.DESCRIPTION
  Bare `-DMACRO` (no `=`) is 1, matching the compiler. If the macro is listed more than once the last
  wins, again matching the compiler.
#>
function Get-PioEnvDefine {
    param(
        [Parameter(Mandatory)][hashtable]$Config,
        [Parameter(Mandatory)][string]$EnvName,
        [Parameter(Mandatory)][string]$Macro
    )
    $sec = $Config["env:$EnvName"]
    if (-not $sec -or -not $sec.ContainsKey('build_flags')) { return $null }
    $m = [regex]::Matches($sec['build_flags'], "-D$([regex]::Escape($Macro))(?:\s*=\s*(\S+))?(?=\s|`$)")
    if ($m.Count -eq 0) { return $null }
    $last = $m[$m.Count - 1]
    if ($last.Groups[1].Success) { return $last.Groups[1].Value }
    return '1'
}

<#
.SYNOPSIS
  The env's `extends` lineage, nearest first, including itself.
#>
function Get-PioEnvLineage {
    param(
        [Parameter(Mandatory)][hashtable]$Config,
        [Parameter(Mandatory)][string]$EnvName
    )
    $chain = @()
    $queue = @("env:$EnvName")
    while ($queue.Count -gt 0) {
        $name = $queue[0]
        $queue = @($queue | Select-Object -Skip 1)
        if ($chain -contains $name -or -not $Config.ContainsKey($name)) { continue }
        $chain += $name
        $ext = $Config[$name]['extends']
        if ($ext) {
            foreach ($p in ($ext -split '[,\r\n]')) {
                if ($p.Trim()) { $queue += $p.Trim() }
            }
        }
    }
    return $chain
}

<#
.SYNOPSIS
  The comment block immediately above a [section] header in the raw ini, if any.

.DESCRIPTION
  PlatformIO discards comments, but the ⛔ SUPERSEDED banners are written as comments, so this reads
  them from the file. A contiguous comment block immediately preceding a header describes THAT
  section, not the one above it — getting this backwards marks a good build superseded and lets the
  superseded one through.
#>
function Get-PioSectionBanner {
    param(
        [Parameter(Mandatory)][string]$IniPath,
        [Parameter(Mandatory)][string]$Section
    )
    $lines = Get-Content -LiteralPath $IniPath
    $pending = @()
    foreach ($line in $lines) {
        if ($line -match '^\s*\[(.+?)\]') {
            if ($Matches[1] -eq $Section) { return ($pending -join "`n") }
            $pending = @()
        }
        elseif ($line -match '^\s*[;#]') { $pending += $line }
        # A blank line ENDS a banner. Without this, a comment block written for one section
        # bleeds across the blank separator into the next one -- which is exactly what happened
        # on 2026-07-27: the `esp32s3-touch` REMOVED tombstone leaked into [env:esp32c3-wifi]
        # and, via `extends` lineage, marked all 15 C3 envs "SUPERSEDED", so flash.ps1 refused
        # to flash every ride build without -Force. A guard that cries wolf on the good build
        # trains you to always pass -Force, which is worse than having no guard.
        elseif ($line -match '^\s*$') { $pending = @() }
        else { $pending = @() }                        # a real option does too
    }
    return ""
}

<#
.SYNOPSIS
  Why (if at all) this env is unsafe to leave on a board someone intends to RIDE.
  Returns an array of human-readable reasons; empty means "shippable".

.DESCRIPTION
  Grounded in the repo's own words: fake_meter.py's docstring calls METER_MATCH_ANY_CPS builds
  "DESK ONLY - don't ride", and platformio.ini carries ⛔ SUPERSEDED banners.
#>
function Get-PioEnvRideBlockers {
    param(
        [Parameter(Mandatory)][hashtable]$Config,
        [Parameter(Mandatory)][string]$EnvName,
        [string]$IniPath
    )
    $reasons = @()

    $sec = $Config["env:$EnvName"]
    if ($sec -and -not $sec.ContainsKey('board')) {
        $reasons += "host-test env (no 'board' key): builds for this PC, there is nothing to flash."
    }

    if ((Get-PioEnvDefine -Config $Config -EnvName $EnvName -Macro 'METER_MATCH_ANY_CPS') -eq '1') {
        $reasons += "BENCH build (METER_MATCH_ANY_CPS=1): pairs with ANY CPS advertiser, including a stranger's meter. fake_meter.py calls this DESK ONLY."
    }
    if ((Get-PioEnvDefine -Config $Config -EnvName $EnvName -Macro 'USE_MOCK_METER') -eq '1') {
        $reasons += "MOCK meter (USE_MOCK_METER=1): broadcasts a synthesised ramp, not your real meter."
    }
    if ($EnvName -match 'probe') {
        $reasons += "diagnostic PROBE build: hardware bring-up only, not a proxy."
    }
    if ($IniPath -and (Test-Path $IniPath)) {
        foreach ($sec in (Get-PioEnvLineage -Config $Config -EnvName $EnvName)) {
            if ((Get-PioSectionBanner -IniPath $IniPath -Section $sec) -match '(?i)superseded') {
                $reasons += "marked SUPERSEDED in platformio.ini (via [$sec]) - read the banner above it before using this env."
                break
            }
        }
    }
    return $reasons
}
