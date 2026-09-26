<#
.SYNOPSIS
  Reliable flash helper for the ESP32-C3 (OTA-first, USB fallback) — encodes the recipes we
  learned the hard way: weak-signal OTA drops and the C3 USB-JTAG bootloader wedge.

.DESCRIPTION
  OTA mode (default): builds, checks the board's WiFi RSSI (warns if it's in the drop zone),
  resolves mDNS -> IP, then runs espota with auto-retry, and verifies the reboot.
  USB mode: builds, auto-detects the C3's COM port, and uploads — with on-screen instructions
  for the manual BOOT/RESET bootloader entry the C3 needs when auto-reset wedges.

.EXAMPLE
  .\flash.ps1                                   # OTA the esp32c3-oled-live build to sb20proxy.local
  .\flash.ps1 -Mode usb                          # USB flash (auto-detect COM)
  .\flash.ps1 -Env esp32c3-wifi-live -Target 192.168.1.165
  .\flash.ps1 -NoBuild                           # skip the build, just (re)flash the existing .bin
  .\flash.ps1 -Env esp32c3-wifi-live-bench -Force # bench/mock build: refuses without -Force
#>
param(
  [string]$Env = "esp32c3-oled-live",
  [ValidateSet("ota", "usb")][string]$Mode = "ota",
  [string]$Target = "sb20proxy.local",   # OTA host (mDNS name or IP)
  [string]$Port = "",                    # USB COM port (auto-detected if empty)
  [int]$Retries = 6,
  [switch]$NoBuild,
  [switch]$Force                         # flash a bench / mock / superseded env anyway (desk use)
)
$ErrorActionPreference = "Stop"
$fw = $PSScriptRoot                                   # this script lives in firmware/
$bin = Join-Path $fw ".pio\build\$Env\firmware.bin"
# espota.py must come from the SAME Arduino core that built this firmware. The two on this machine
# are not interchangeable: Arduino 2.x's espota does `sock2.recv(37)` for the AUTH reply, Arduino 3.x
# boards answer with a 64-hex nonce (69 bytes), and on Windows a UDP recv into a short buffer RAISES
# (WSAEMSGSIZE) rather than truncating. espota catches it bare, retries ten times and reports
# "No response from the ESP" — a message that sends you looking at signal strength and firewalls
# while the board is answering perfectly. Cost ~25 minutes on the Guition, 2026-09-26.
# PLATFORMIO_CORE_DIR is how the S3/Guition family is built (the MAX_PATH workaround, DEV-PLAYBOOK),
# so honour it first; fall back to the default core for the C3.
$coreDir = if ($env:PLATFORMIO_CORE_DIR) { $env:PLATFORMIO_CORE_DIR } else { Join-Path $env:USERPROFILE ".platformio" }
$espota = Join-Path $coreDir "packages\framework-arduinoespressif32\tools\espota.py"

function Say($msg, $color = "Cyan") { Write-Host $msg -ForegroundColor $color }

# Ask the board for its AUTH nonce and check the chosen espota's buffer is big enough for the reply.
# One UDP round-trip, and it turns the six-attempt lie above into one accurate line.
function Test-EspotaFitsReply($espotaPath, $ip) {
  $expect = (Select-String -Path $espotaPath -Pattern 'sock2\.recv\((\d+)\)' | Select-Object -First 1)
  if (-not $expect) { return }                      # unknown tool shape: say nothing rather than guess
  $want = [int]$expect.Matches[0].Groups[1].Value
  $u = New-Object System.Net.Sockets.UdpClient(0)
  try {
    $u.Client.ReceiveTimeout = 3000
    $msg = [Text.Encoding]::ASCII.GetBytes("0 1 1 00000000000000000000000000000000`n")
    [void]$u.Send($msg, $msg.Length, $ip, 3232)
    $ep = New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Any, 0)
    $len = $u.Receive([ref]$ep).Length
    if ($len -gt $want) {
      Say "espota mismatch: the board replies $len bytes, $espotaPath reads $want." "Red"
      Say "  On Windows that fails as 'No response from the ESP'. Use the espota.py from the core that" "Red"
      Say "  built this env (set PLATFORMIO_CORE_DIR, e.g. C:\pio-s3 for the S3/Guition family)." "Red"
    }
  } catch { }                                        # no reply is the normal 'board busy' case; carry on
  finally { $u.Close() }
}

# ---- Ride-safety gate -------------------------------------------------------------------------
# The env names are one hyphen apart ("...-live" vs "...-live-bench") and the flags that make the
# difference are up to three `extends` hops away, so "the build you meant" and "a build that pairs
# with any stranger's power meter" look identical at the call site. Resolve the chain and say so.
. (Join-Path (Split-Path $fw -Parent) "tools\PioIni.ps1")
$iniPath = Join-Path $fw "platformio.ini"
$pioCfg = Get-PioConfig -ProjectDir $fw
$envNames = Get-PioEnvNames -Config $pioCfg
if ($envNames -notcontains $Env) {
  $stem = ($Env -split '-')[0]
  $near = ($envNames | Where-Object { $_ -like "*$stem*" }) -join ', '
  throw "no [env:$Env] in platformio.ini.$(if ($near) { " Did you mean: $near" })"
}
$blockers = Get-PioEnvRideBlockers -Config $pioCfg -EnvName $Env -IniPath $iniPath
if ($blockers.Count -gt 0) {
  Say "'$Env' is not a shippable build:" "Yellow"
  foreach ($r in $blockers) { Say "  - $r" "Yellow" }
  if (-not $Force) {
    throw "refusing to flash '$Env' without -Force. Desk/bench session? re-run with -Force. Otherwise pick a ride build (e.g. esp32c3-oled-live-ota)."
  }
  Say "-Force given: proceeding. Do NOT leave this build on a board you intend to ride." "Red"
}

# Authenticated push OTA: read OTA_PASSWORD from the (gitignored) ota_secret.h so espota can pass -a.
# Single source of truth — the firmware compiles in the same value. Absent ⇒ push OTA is disabled on the
# board (fail-closed, 2026-06-24 security review); use -Mode usb instead.
$otaPass = $null
$otaSecret = Join-Path $fw "ota_secret.h"
if (Test-Path $otaSecret) {
  $m = Select-String -Path $otaSecret -Pattern '#define\s+OTA_PASSWORD\s+"([^"]*)"'
  if ($m) { $otaPass = $m.Matches[0].Groups[1].Value }
}

if (-not $NoBuild) {
  Say "Building $Env ..."
  python -m platformio run -e $Env -d $fw
  if ($LASTEXITCODE -ne 0) { throw "build failed" }
}
if (-not (Test-Path $bin)) { throw "firmware.bin not found ($bin) - build first (drop -NoBuild)" }

if ($Mode -eq "ota") {
  # RSSI pre-flight: OTA gets unreliable below ~ -72 dBm on the C3.
  # Read /status, NOT / — `/` used to serve the status JSON but now serves the HTML dashboard, so
  # ConvertFrom-Json threw on every run and the weak-signal warning had been silently dead (found
  # 2026-09-23). A pre-flight that always lands in its own catch is worse than none: it prints a
  # yellow line that looks like a network hiccup while the check it exists for never happens.
  $ip = $Target
  try {
    $j = (Invoke-WebRequest "http://$Target/status" -TimeoutSec 5 -UseBasicParsing).Content | ConvertFrom-Json
    $rssi = [int]$j.rssi
    $c = if ($rssi -ge -70) { "Green" } elseif ($rssi -ge -72) { "Yellow" } else { "Red" }
    Say "Board up, WiFi RSSI = $rssi dBm" $c
    if ($rssi -lt -72) { Say "  weak signal - OTA may drop; move the board nearer the AP (watch 'WiFi -XX' on the OLED)." "Yellow" }
  } catch { Say "couldn't read http://$Target/status - is the board up? (continuing anyway)" "Yellow" }
  try { $ip = ([System.Net.Dns]::GetHostAddresses($Target) | Where-Object { $_.AddressFamily -eq 'InterNetwork' } | Select-Object -First 1).IPAddressToString } catch {}

  if (-not $otaPass) {
    Say "No ota_secret.h found - push OTA is disabled on boards built without it (fail-closed)." "Yellow"
    Say "  If this board has no OTA_PASSWORD baked in, espota will not connect - use: .\flash.ps1 -Mode usb" "Yellow"
  }
  Test-EspotaFitsReply $espota $ip
  $espotaArgs = @('-i', $ip, '-p', '3232', '-f', $bin, '-r')
  if ($otaPass) { $espotaArgs += @('-a', $otaPass) }

  $ok = $false
  for ($i = 1; $i -le $Retries; $i++) {
    Say "OTA attempt $i/$Retries -> $ip ..."
    python $espota @espotaArgs
    if ($LASTEXITCODE -eq 0) { Say "OTA OK" "Green"; $ok = $true; break }
    Start-Sleep -Seconds 3
  }
  if (-not $ok) { throw "OTA failed after $Retries attempts - weak signal, or push OTA disabled (no ota_secret.h)? Move closer, or use -Mode usb." }

  Say "waiting for reboot ..."
  for ($i = 0; $i -lt 12; $i++) {
    Start-Sleep -Seconds 3
    try { Invoke-WebRequest "http://$Target/stats" -TimeoutSec 4 -UseBasicParsing | Out-Null; Say "back up: http://$Target/stats" "Green"; break } catch {}
  }
}
elseif ($Mode -eq "usb") {
  if (-not $Port) {
    $Port = Get-CimInstance Win32_PnPEntity |
      Where-Object { $_.Name -match 'COM\d+' -and $_.DeviceID -match 'VID_303A' } |
      ForEach-Object { if ($_.Name -match '(COM\d+)') { $Matches[1] } } | Select-Object -First 1
  }
  if (-not $Port) { throw "no ESP32-C3 USB port (VID_303A) found - plug it in (and check the cable carries data)." }
  Say "USB port: $Port"
  Say "If the upload fails with 'No serial data received' / 'Unable to verify flash chip connection'," "Yellow"
  Say "  the C3 USB-JTAG didn't enter the bootloader. Recover: HOLD BOOT, TAP RESET, RELEASE BOOT, then re-run." "Yellow"
  python -m platformio run -e $Env -d $fw -t upload --upload-port $Port
  if ($LASTEXITCODE -ne 0) { throw "USB upload failed - do the manual BOOT/RESET bootloader entry above, re-run, then power-cycle the board." }
  Say "USB flash OK (power-cycle if it doesn't reboot on its own)" "Green"
}
