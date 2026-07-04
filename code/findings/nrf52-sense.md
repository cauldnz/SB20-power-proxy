# Seeed XIAO nRF52840 Sense — the BLE(/ANT) bridge board

**Status:** bring-up in progress (2026-07-04 overnight). The fourth head-unit-family board, and the
first with ANT capability — target use: the track-bike repeater-with-correction, plus IMU capture.

## Board facts

- **Seeed Studio XIAO nRF52840 Sense** — nRF52840 (1MB flash / 256KB RAM), BLE 5 + ANT-capable
  radio, **LSM6DS3TR-C** 6-axis IMU (internal I2C at 0x6A, power gated by
  `PIN_LSM6DS3TR_C_POWER`), PDM mic, RGB LED (active low), no WiFi.
- Enumerates as **VID 2886 PID 8065** (application CDC); desk port **COM18**
  (serial `CA387478E20D0342`). UF2 bootloader (double-tap RST) + adafruit-nrfutil CDC DFU.
- SWD recovery pads are on the underside (needs a J-Link/DAP probe) — treat bootloader/SoftDevice
  surgery as brick-risk unless staged carefully (see §ANT).

## Toolchain (PlatformIO)

- Stock PlatformIO has **no XIAO nRF52840 board defs**. Use **maxgerhardt/platform-nordicnrf52
  `#develop`** (the `master` branch lacks the XIAO boards — cost a debug loop) with board
  **`xiaoblesense_adafruit`** = the Adafruit-fork core: Bluefruit dual-role BLE (like the ESP32
  proxies), FreeRTOS, S140 SoftDevice.
- GOTCHA: if a stock `nordicnrf52` platform is already installed, PlatformIO silently uses it
  instead of the git-URL fork (same name) → "Unknown board ID". Delete
  `~/.platformio/platforms/nordicnrf52` first.
- Project: `firmware-nrf/` (env `xiao-sense`).

## ANT: the licensed path (S340)

ANT (and concurrent ANT+BLE) needs Nordic's **S340 SoftDevice** + an **ANT+ network key**, both
licensed via a free thisisant.com adopter account and **not redistributable** — they live in
`firmware-nrf/vendor/softdevice/` (gitignored; see its README for the exact download steps).

**Prior art (all read-to-understand; reimplement where licenses demand):**
- [orrmany/SDAntplus](https://github.com/orrmany/SDAntplus) — ANT+ profiles piggy-backed on
  Bluefruit52 for CONCURRENT BLE+ANT against S340. License: "(Modified) MIT" — read the full text
  before vendoring anything; the sd_ant_* wrapper layer is thin enough to write clean regardless.
- [cujomalainey/ant-arduino](https://github.com/cujomalainey/ant-arduino) — framework-neutral ANT
  driver, best for serial ANT radios; SoftDevice mode exists.
- Integration recipe (blogarak.wordpress.com, S340 + Adafruit Feather nRF52840 — the same core we
  use): custom board json (`"softdevice": {"sd_flags":"-DS340","sd_name":"s340",
  "sd_version":"6.1.1","sd_fwid":"0x00B9"}`), custom linker script (app FLASH origin **0x31000**
  vs S140's 0x26000; RAM origin 0x20006000), S340 API headers into the core's
  `cores/nRF5/nordic/softdevice/s340_nrf52_6.1.1_API/include/`, and a **bootloader rebuilt against
  S340** (the Adafruit bootloader checks the SD fwid; see Adafruit_nRF52_Bootloader issues #73 and
  #146).
- **Brick-risk staging:** prove the SoftDevice swap on the **nRF52840 dongle first** (COM13 —
  Nordic Open Bootloader, button-forced DFU = recoverable), which doubles as the ANT signal source
  for twin tests. Only then repeat on the Sense (UF2 bootloader replacement over CDC DFU; SWD pads
  are the only recovery if it goes wrong).

## Architecture (mirrors the ESP32 proxies)

- Same pure core reused by reference: `firmware/lib/proxy/` (`Cps.h` codec, `Correction.h`).
- Radio seam: `IRadioSource` / `IRadioSink` so the bridge composes any of BLE-in/ANT-in ×
  BLE-out/ANT-out once S340 lands. BLE-in → BLE-out ships first (Bluefruit central + peripheral).
- No WiFi ⇒ the control/telemetry surface is a **custom GATT service** (contract in
  `firmware-nrf/GATT.md`) consumed by the Web Bluetooth app (public Pages repo — HTTPS is
  mandatory for Web Bluetooth) and the Garmin Connect IQ app (Edge 540 / Epix Gen 2).

Sources: [SDAntplus](https://github.com/orrmany/SDAntplus) ·
[ant-arduino](https://github.com/cujomalainey/ant-arduino) ·
[S340+PlatformIO recipe](https://blogarak.wordpress.com/2020/03/29/platformio-ide-integration-for-the-nrf52840-feather-express-with-s340/) ·
[Adafruit bootloader S340 issue](https://github.com/adafruit/Adafruit_nRF52_Bootloader/issues/73)

## Can we do ANT+ WITHOUT the S340 SoftDevice? (clean-room research, 2026-07-05)

**Question (owner):** while thisisant activation is pending (≤1 business day), can we skip the
licensed S340 entirely and hand-roll ANT+ (concurrent with BLE) in clean-room firmware?

**Answer: technically the radio can, legally the protocol docs allow it — but for THIS project
it's the wrong tool, and it can't get us off the adopter hook anyway. Three findings:**

1. **The radio hardware can generate ANT frames bare-metal — "the SoftDevice unlocks the radio"
   is a myth.** The nRF52840 `RADIO` peripheral is a generic 1 Mbps GFSK transceiver; register-
   level packet TX/RX with a configurable access address + CRC is routine (Nordic's own ESB /
   Gazell and the NordicSnippets bare-metal examples all do exactly this). ANT's on-air framing
   (1 Mbps GFSK, ANT+ device profiles on RF channel 57 = 2457 MHz) is within that capability. The
   S340 provides the ANT **MAC/TDMA timing engine + message protocol + BLE coexistence
   arbitration** — a *software stack*, not radio access.

2. **The ANT+ network key is an ON-AIR gate, not a software check — so bare-metal doesn't dodge
   the license.** thisisant is explicit: an ANT device "will not hear transmissions occurring on
   other networks" — the network key materially segregates traffic on air. A perfect bare-metal
   ANT stack therefore *cannot* talk to a real ANT+ power meter or Garmin head unit without the
   **ANT+ managed network key**, which comes only through the (free) adopter agreement the owner
   just applied for. Brute-forcing / sniffing the key to avoid the agreement would be a licensing
   violation AND pointless (it arrives free in ≤1 day). Note: the **public network key** (a real
   value — NOT all-zeros, which is invalid) allows private ANT experimentation, but by definition
   can't interoperate with the ANT+ ecosystem — useless for a repeater whose whole job is talking
   to real ANT+ gear. The protocol docs themselves (ANT Message Protocol & Usage; the ANT+
   Bicycle Power device profile D00001086) are PUBLIC, no NDA — so clean-room *implementation* is
   legal; only the key + the ANT+ trademark are gated.

3. **Concurrent ANT+BLE is the genuinely hard part — and it's exactly what S340 sells.** The
   bridge needs two radios live at once (ANT-in + BLE-out to the web app / head unit). Bare-metal,
   that means writing a hard-real-time radio time-slice arbiter between two independent-timing
   protocols — reimplementing the core value of the S340, with brick/timing risk, to replace
   something we get free.

**The one bare-metal option that IS worth keeping in the back pocket:** a **single-protocol** ANT
mode — ANT-in → correct → ANT-out, NO concurrent BLE — becomes tractable precisely because it
drops the arbitration. It can even use the *licensed* ANT+ key loaded as DATA (an 8-byte array in
the gitignored key header, not the SoftDevice binary) — clean-room-legal once we're an adopter. A
viable fallback ONLY if S340 proves problematic on the XIAO's UF2 bootloader.

**Recommendation: wait for the S340** (free, ≤1 business day, gives concurrent ANT+BLE = the
bridge's actual need). Tonight stays BLE + Connect IQ; the drop-zone watch stays armed. Sources:
[Network Keys / Managed Network](https://www.thisisant.com/developer/resources/tech-bulletin/network-keys-and-the-ant-managed-network) ·
[public key FAQ](https://www.thisisant.com/developer/resources/tech-faq/how-do-i-get-the-public-network-key) ·
[nRF52 ANT (SoftDevice) ](https://www.thisisant.com/developer/components/nrf52832) ·
[NordicSnippets bare-metal radio](https://github.com/andenore/NordicSnippets) ·
[ANT+ Bicycle Power profile D00001086](https://www.thisisant.com).
