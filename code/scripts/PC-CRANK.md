# PC spoofed-crank rig (`fake_crank.py`) — capabilities, limits, and the verdict

A Windows BLE peripheral that impersonates the Stages crank in Python, so we can **capture and
iterate the SB20's control-point/erg handshake in seconds** instead of ~5 min per ESP reflash.
Built on `sb20proxy.ble.winrt_peripheral.WinrtCpsPeripheral` (WinRT GATT server; bleak is
central-only).

## Run it

```powershell
$env:PYTHONPATH = "code\src"
code\.venv-win\Scripts\python.exe code\scripts\fake_crank.py --seconds 600
```

Emits the real **`0x2F`** frame (power ramp 100–300 W + balance + accumulated torque + crank-rev
cadence — the encoder is golden-tested against the captured `2f00ae00583cf7e600be6c`), exposes the
**Cycling Power Control Point (0x2A66)**, and prints + captures every write a connected central
makes (decoded via `cps.decode_control_point`).

## What works

- A central that connects **by CPS service UUID or by address** (a phone CPS app, a `bleak`
  client, our ESP32 central) sees power + cadence and can write the control point — and we log it.
- The full CPS read surface (Feature `0x0008030B`, Sensor Location `0`) and control-point write
  capture — the core for handshake discovery.

## WinRT limits (confirmed empirically — the honest part)

1. **No custom advertised name.** `GattServiceProviderAdvertisingParameters` exposes only
   `is_connectable` / `is_discoverable` / `service_data` (+ PHY) — **no name field**. The peripheral
   advertises under the **PC's system Bluetooth name**, not `Stages 62144`. **The SB20 pairs the
   crank by name, so it will not recognise this rig as the crank** unless you first rename the PC's
   Bluetooth name to `Stages 62144` (Windows: rename the device / registry
   `HKLM\SYSTEM\...\Bluetooth` local name; reboot the BT stack). Invasive, but it's the workaround.
2. **One primary service per provider.** WinRT `GattServiceProvider` advertises one service; adding
   the Stages **proprietary `d445fe01` service** (part of the SB20's "genuine Stages?" check) is not
   straightforward. The ESP exposes it natively.

## Verdict / recommended path

- **Reliable SB20 handshake capture = the ESP32 + `/log`** (it advertises the correct name *and* the
  faithful GATT incl. `d445fe01`, and now logs every control-point/proprietary write). Iterate by
  reading `/log`, adjusting the firmware, reflashing (~5 min). A future speedup: make the ESP's
  control-point responses runtime-configurable (HTTP) so response experiments need no reflash.
- **This PC rig is best for:** a phone/`bleak` client test of our CPS surface, fast Python iteration
  of *response logic* once we know what the SB20 writes, and a renamed-PC SB20 test if we want to try
  the fast path on-bike.

So: tomorrow, **lead with the ESP** for the real capture; keep this rig as the fast-iterate option,
gated on the rename workaround.
