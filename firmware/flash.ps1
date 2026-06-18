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
#>
param(
  [string]$Env = "esp32c3-oled-live",
  [ValidateSet("ota", "usb")][string]$Mode = "ota",
  [string]$Target = "sb20proxy.local",   # OTA host (mDNS name or IP)
  [string]$Port = "",                    # USB COM port (auto-detected if empty)
  [int]$Retries = 6,
  [switch]$NoBuild
)
$ErrorActionPreference = "Stop"
$fw = $PSScriptRoot                                   # this script lives in firmware/
$bin = Join-Path $fw ".pio\build\$Env\firmware.bin"
$espota = Join-Path $env:USERPROFILE ".platformio\packages\framework-arduinoespressif32\tools\espota.py"

function Say($msg, $color = "Cyan") { Write-Host $msg -ForegroundColor $color }

if (-not $NoBuild) {
  Say "Building $Env ..."
  python -m platformio run -e $Env -d $fw
  if ($LASTEXITCODE -ne 0) { throw "build failed" }
}
if (-not (Test-Path $bin)) { throw "firmware.bin not found ($bin) - build first (drop -NoBuild)" }

if ($Mode -eq "ota") {
  # RSSI pre-flight: OTA gets unreliable below ~ -72 dBm on the C3.
  $ip = $Target
  try {
    $j = (Invoke-WebRequest "http://$Target/" -TimeoutSec 5 -UseBasicParsing).Content | ConvertFrom-Json
    $rssi = [int]$j.rssi
    $c = if ($rssi -ge -70) { "Green" } elseif ($rssi -ge -72) { "Yellow" } else { "Red" }
    Say "Board up, WiFi RSSI = $rssi dBm" $c
    if ($rssi -lt -72) { Say "  weak signal - OTA may drop; move the board nearer the AP (watch 'WiFi -XX' on the OLED)." "Yellow" }
  } catch { Say "couldn't read http://$Target/ (continuing anyway)" "Yellow" }
  try { $ip = ([System.Net.Dns]::GetHostAddresses($Target) | Where-Object { $_.AddressFamily -eq 'InterNetwork' } | Select-Object -First 1).IPAddressToString } catch {}

  $ok = $false
  for ($i = 1; $i -le $Retries; $i++) {
    Say "OTA attempt $i/$Retries -> $ip ..."
    python $espota -i $ip -p 3232 -f $bin -r
    if ($LASTEXITCODE -eq 0) { Say "OTA OK" "Green"; $ok = $true; break }
    Start-Sleep -Seconds 3
  }
  if (-not $ok) { throw "OTA failed after $Retries attempts - weak signal? Move the board closer, or use -Mode usb." }

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
