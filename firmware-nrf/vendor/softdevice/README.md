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
