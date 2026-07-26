# ANT SoftDevice drop zone (S340) — LOCAL FILES ONLY, NEVER COMMIT

The nRF52840's ANT (and concurrent ANT+BLE) radio needs Nordic's **S340 SoftDevice**, which is
licensed by Garmin/Dynastream and **must not be committed to any repo** (the whole directory is
gitignored except this README).

## What to download (owner action — needs a free thisisant.com account)

1. Log in / register as an **ANT+ Adopter** at <https://www.thisisant.com> (free tier is fine).
2. **S340 SoftDevice**: Downloads → Software → *ANT SoftDevices* → download the latest
   **S340 nRF52840** zip (e.g. `ANT_s340_nrf52840_7.x.x.zip`).
3. **ANT+ Network Key**: Downloads → *ANT+ Network Key* (accept the adopter agreement). It's an
   8-byte hex key.
4. Drop the files here:
   - `firmware-nrf/vendor/softdevice/ANT_s340_nrf52840_<ver>.zip` (or the unzipped hex + headers)
   - `firmware-nrf/vendor/softdevice/ant_network_key.h` containing:
     ```c
     #pragma once
     // The licensed ANT+ network key (thisisant.com adopter download). NEVER commit.
     #define ANT_PLUS_NETWORK_KEY { 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x?? }
     ```

The firmware builds BLE-only until these exist; the ANT radio seam compiles itself in when the
key header is present.

## Already have them? (worktrees start empty)

These files are **downloaded once per machine and gitignored** — they are never in git, so a fresh
`git worktree` gets an *empty* `vendor/softdevice/` even though your main checkout is fully
provisioned. Before building an S340 env in a worktree, copy them across from the main checkout:

```powershell
# from the worktree
$src = "C:\repos\<owner>\SB20-power-proxy\firmware-nrf\vendor\softdevice"   # your main checkout
Copy-Item "$src\ANT_s340_nrf52840_6.1.1" .\firmware-nrf\vendor\softdevice\ -Recurse -Force
Copy-Item "$src\ant_network_key.h"       .\firmware-nrf\vendor\softdevice\ -Force
```

The **ANT+ network key lives in `ant_network_key.h` here** — a gitignored header, never committed,
never pasted into a tracked file or a commit message. Same for the SoftDevice zip and its extracted
tree. A provisioned copy usually already has the evaluation `ANT_LICENSE_KEY` uncommented (step 2
below), so the copy builds as-is; check before redoing it.

## Building the S340 firmware (`pio run -e xiao-sense-s340`)

Once the files above are in place, two extra provisioning steps (both LOCAL, on gitignored files):

1. **Extract the S340 zip** here so the env's `-I` paths resolve:
   `firmware-nrf/vendor/softdevice/ANT_s340_nrf52840_6.1.1/…` (the env expects
   `ANT_s340_nrf52840_6.1.1.API/include/` + the `ANT_s340_nrf52840_6.1.1.hex` under that dir).
2. **Enable the ANT license key** — the S340 `nrf_sdm.h` hard-`#error`s without `ANT_LICENSE_KEY`. For
   **non-commercial use** (this project), *uncomment* the `//#define ANT_LICENSE_KEY "…"` **evaluation-key**
   line just above that `#error` (~line 191). It's the header's own documented mechanism. (Commercial use
   requires a purchased key from ANT Wireless.)

Then `pio run -e xiao-sense-s340` links the app at 0x31000 (S340 map). The env is **local-only** — CI never
builds it (these files are gitignored). Flashing it is a SoftDevice swap — see
`sessions/nrf-s340-ant-bringup.md`.

## Flashing gotchas (measured 2026-07-27)

- **No env emits a `.uf2`.** Not `xiao-sense-s340`, and not the stock `xiao-sense` either — the
  Adafruit builder produces only `.hex`/`.zip` here. `flash.ps1` now converts the `.hex` itself
  (`uf2conv.py -f 0xADA52840`), so this is handled; the point is not to be surprised by a missing
  `.uf2` or to conclude the env is broken.
- **A 1200-baud touch is not a reliable way into the bootloader on this board.** It does reset into
  it, but Windows enumerates only the CDC interface — the mass-storage one never appears, so no
  `XIAO-SENSE` drive mounts and nothing can be copied. **Double-tap RST** brings up both.
- **The S140 app base is 0x27000, not 0x26000.** The Seeed core ships S140 **v7**
  (`nrf52840_s140_v7.ld`, `ORIGIN = 0x27000`); 0x26000 is the *v6* value that older notes in this
  repo quoted. `flash.ps1` reads the ORIGIN out of the ldscript rather than hardcoding either.
