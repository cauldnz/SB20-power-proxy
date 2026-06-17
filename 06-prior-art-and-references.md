# 06 — Prior Art and References

Annotated reading list. Group by topic. Read the bolded items before writing code.

## Parent research (read first)

The owner has a parent `Research_Content` document in the project files that surveys the broader ANT+/BLE fitness-sensor ecosystem. It's the right starting point — it covers the protocols primer, Python and Rust libraries, Pi hardware paths, browser/WASM delivery, and target devices (SB20, Tacx Neo, Concept2 PM5) at a level this package doesn't duplicate. Specifically read:

- "Protocols Primer" — ANT vs ANT+ vs BLE, device profiles, GATT services
- "Python Libraries" — openant, pycycling, bleak with worked examples
- "Target Devices → Stages SB20" — the SB20's protocol surface

Below is the SB20-spoof-specific reading list that builds on top of that.

## Specs (read these first)

### **ANT+ Bicycle Power Device Profile (D00001086 Rev 5.x)**
- The official spec. Hosted PDF: https://forums.garmin.com/cfs-file/__key/communityserver-discussions-components-files/402/D00001086_5F00_ANT_2B005F00_Device_5F00_Profile_5F002D005F00_Bicycle_5F00_Power_5F00_Rev_5F00_5.0.pdf
- Read sections 7 (Power-Only Sensors), 8 (Crank Torque Sensors), 11 (Common Pages), 13 (Calibration). At least skim the rest.
- This is the contract. Spend at least an hour with it before writing any transmit code.

### ANT Message Protocol and Usage
- The lower-level ANT spec (separate document, also from thisisant.com) covers extended messages, channel ID resolution, and the like.
- Most relevant for Phase 0: section on extended messages (to capture channel IDs of incoming messages on a slave channel).

### Bluetooth GATT Specification Supplement — Cycling Power Service (UUID 0x1818)
- https://www.bluetooth.com/specifications/specs/ (look for GATT Specification Supplement)
- Mirror of XML spec: https://github.com/oesmith/gatt-xml/blob/master/org.bluetooth.service.cycling_power.xml
- Read if/when BLE support is contemplated. Not needed for ANT+-only v1.

## Libraries

### **openant — Tigge/openant**
- https://github.com/Tigge/openant
- Python ANT+ library. Active, maintained, supports both RX and TX on Garmin/Dynastream sticks.
- Install: `pip install openant`
- Linux udev (non-root USB access): write a rule for vendor `0x0fcf` directly (`/etc/udev/rules.d/42-ant-usb-sticks.rules` with `SUBSYSTEM=="usb", ATTRS{idVendor}=="0fcf", MODE="0666"`, then `udevadm control --reload-rules && udevadm trigger`). openant's bundled `python -m openant.udev_rules` helper copies from a relative `resources/` path pip doesn't ship, so it errors — see `07-hardware-and-environment.md`.
- Read: `openant/devices/power_meter.py` for the receiver pattern; `openant/easy/node.py` for channel/lifecycle; `openant/base/ant.py` for low-level message construction.
- Examples worth studying in the repo's `examples/` folder.
- Caveat per `06-rate-thread`: queue-handling in `_worker` and `_main` adds ~100 ms sleep loops that drop messages above ~15 Hz. For 4 Hz Bike Power broadcasts this won't bite, but be aware. See https://www.thisisant.com/forum/viewthread/7284 for the patch (it's small).

### pycycling
- https://github.com/zacharyedwardbull/pycycling
- A thin, well-organised wrapper around `bleak` that knows about the cycling protocols (CPS, CSCS, FTMS, Tacx FE-C-over-BLE, HRS).
- Use this if/when we go down the BLE-side spoofing path (or if we want to read Assioma over BLE rather than ANT+ as a fallback).
- The parent `Research_Content` document has a fuller writeup; install via `pip install pycycling`.

### bleak
- https://github.com/hbldh/bleak
- The cross-platform Python BLE primitive. asyncio-native. Everything BLE in Python rests on it.
- We use `bleak` indirectly via pycycling, but for any BLE peripheral / advertising work we'd need to drop down to bleak directly (or use a different lib — bleak is central-only).

### pirower/openant fork
- https://github.com/pirower/openant
- Adds extended-messages and background-scanning support to openant.
- Useful if you want extended messages without implementing them yourself.
- May be merged upstream by now — check before forking again.

## Reference implementations (study, don't copy verbatim)

The most architecturally relevant references are the ones that **broadcast** as a fitness device — that's the unique challenge of this project. There are several.

### **dhague/vpower — Virtual Power Meter**
- https://github.com/dhague/vpower
- Reads sensor data, computes power, **rebroadcasts as a standard ANT+ Bike Power meter**.
- Architecture is exactly the pattern we need: source → transform → master broadcast.
- Use as the reference for "how to be an ANT+ master power meter from Python on a Pi".

### **zwack (Node.js)**
- Node.js implementation of an FTMS / Cycling Power Service server (BLE peripheral).
- Broadcasts simulated power/cadence/speed as a real fitness device — useful reference for how to spoof a power meter on the BLE side, including the flag-byte layouts and notification cadence.
- Search GitHub for "zwack" — multiple forks; the original by paixaop is the canonical one.

### **OpenRowingMonitor**
- Node.js project that turns a generic rowing machine into a smart connected rower.
- Exposes both BLE FTMS Rower Data and Concept2 PM5-compatible BLE — i.e. **emits the same protocol that a real PM5 emits**, fooling rowing apps into treating it as a Concept2 erg.
- One of the cleanest open-source examples of "spoofing a name-brand fitness device protocol", with thoughtful attention to robustness, calibration, and reconnect handling. **Strongly recommended reading** even though it's a different device class.

### **raralabs/pm5-emulator (Node.js)**
- A pure-software emulator of the Concept2 PM5 over BLE. Same architectural problem as ours: pretend to be a specific manufacturer's device convincingly enough that real apps accept the connection.
- Has a copy of the Concept2 protocol spec mirrored in `docs/resources/`.
- Worth studying for how it handles the device-discovery / advertising / GATT-server lifecycle — much of which translates to BLE-side spoofing if we ever go there.

### tijmenvangulik/ErgometerJS
- https://github.com/tijmenvangulik/ErgometerJS
- TypeScript / JS driver for the Concept2 PM5 (BLE and USB-via-WebHID). Cordova / Electron / web compatible.
- Cross-platform delivery reference; useful if we eventually want a non-Pi distribution.

### olympum/ant-cycling-power
- https://github.com/olympum/ant-cycling-power
- Node.js, but the `power-meter.js` file shows the byte-level construction of a page 0x10 broadcast clearly. Quick reference for the page format if you want to sanity-check your encoding.

### TimoWintz/ant_nrf_powermeter
- https://github.com/TimoWintz/ant_nrf_powermeter
- Homemade power meter on nRF52832 hardware. Lower-level than we need but useful for understanding what a "minimal" power meter implementation looks like in C against the Nordic SoftDevice ANT stack.

### Tea and Tech Time — Arduino BLE Cycling Power Service
- https://teaandtechtime.com/arduino-ble-cycling-power-service/
- Walk-through of building a BLE Cycling Power peripheral on an Arduino Nano 33 BLE. Useful reference for Cycling Power Measurement (0x2A63) byte layout.

### mau-lima/ESP32-Bike-Powermeter
- https://github.com/mau-lima/ESP32-Bike-Powermeter
- License: **MIT** — permissively licensed, so we can borrow/adapt directly (unlike GPL prior art like QZ), with attribution.
- C++ / PlatformIO (VS Code) proof-of-concept that emulates a BLE bicycle power meter on a bare ESP32, broadcasting the standard Cycling Power Service so off-the-shelf cycling apps accept it as a real power meter. Small (≈6 commits), no sensors yet — it transmits **fabricated** power/cadence values to demonstrate the BLE-peripheral side.
- **Why it's worth keeping**: it's the minimal end of the "be a convincing BLE CPS peripheral on an ESP32" spectrum — relevant if we ever pursue the ESP32 BLE-only deployment target noted under the QZ `QZ_ESP32/` entry above (much smaller/cheaper than a Pi; ANT+ on ESP32 is impractical, so it'd be BLE-side spoofing only). The PlatformIO project layout and the CPS GATT-server/advertising setup are the useful parts to study; the synthetic-value generation is a stand-in for what would become our source→transform stage.
- Caveat: proof-of-concept maturity (no calibration, no real measurement, low commit count). Treat as a starting skeleton for the BLE-peripheral mechanics, not a robustness reference — for that, OpenRowingMonitor / pm5-emulator / QZ are stronger.

### PeloMon (ihaque)
- https://ihaque.org/posts/2021/01/04/pelomon-part-iv-software/
- A Peloton-to-BLE-Cycling-Power bridge. Not directly applicable but the writing about CPS gotchas (Garmin requiring CSCS too, etc.) is good.

### **qdomyos-zwift (QZ)** — `cagnulein/qdomyos-zwift`
- https://github.com/cagnulein/qdomyos-zwift
- License: **GPL-3.0** (see licensing note below)
- C++/Qt cross-platform application (Mac, Linux, iOS, Android, plus an ESP32 variant). 758+ stars, 7,000+ commits, 44+ contributors, on App Store and Play Store. The most substantial open-source prior art in this space by a large margin.
- **What it does that's directly relevant**: bridges dozens of proprietary indoor fitness bikes/treadmills/ergs into standard BLE FTMS/CPS/FE-C broadcasts. For each supported bike, QZ:
  - Reads the bike's native protocol (often proprietary; e.g. Echelon, Domyos, Peloton, Toorx)
  - Transforms into normalised power/cadence/speed/HR
  - Re-broadcasts as a standard BLE peripheral implementing FTMS, Cycling Power Service, CSCS
- **For our SB20 project specifically**: not a direct solution (QZ goes bike→apps; we go *into* the bike). But it's by far the strongest reference for "be a convincing BLE fitness device" — which is exactly our `StagesBleTarget` problem if we go down the BLE-spoofing route as a fallback to ANT+. Their FTMS/CPS server implementation is extensive and battle-tested across many real apps.
- **Architecture pattern that validates our design**: per QZ's own `CLAUDE.md`, they use a hierarchical device architecture with a `bluetoothdevice` abstract base class that "defines common metrics (speed, cadence, heart rate, power, distance)" and integrates with "virtual devices for app connectivity." This is essentially a C++/Qt version of our `PowerSource`/`PowerTarget` seam. Independent convergence on the same shape from a much larger codebase is encouraging.
- **Onboarding-a-new-device process**: QZ supports new bikes via Android-side BLE sniffing (with nRF Connect or similar) to "guess how they communicate." Our Phase 0 plan does the equivalent for ANT+ via openant capture. If we ever extend to bridging additional bikes in the future, this is the methodology.
- **For Use Case 4 (foundation for future projects), QZ is the reference for**:
  - **Peloton integration** — bidirectional: reads bike metrics from a Peloton, *sends auto-resistance back*. Same conceptual problem as our SB20 spoof (closed-system resistance control). If/when a Peloton bridge becomes a project, study QZ first.
  - **Zwift integration** — including the `zwiftplay` submodule (reverse-engineered Zwift Play controller protocol).
  - **Wahoo Direct Connect** — Wahoo's over-Wi-Fi trainer protocol; QZ has an implementation.
  - **Multi-source aggregation** — combining HR (ANT+/BLE/Apple Watch), power, cadence into one normalised stream.
  - **MQTT and OpenSoundControl integration** — built in.
- **Embedded variant**: the `QZ_ESP32/` directory contains an ESP32 build. Worth knowing about as a possible deployment target — an ESP32 is much smaller and cheaper than a Pi, though ANT+ on ESP32 is impractical so this would be BLE-only.
- **Claude Code conventions to study**: QZ uses Claude Code extensively in production. Evidence: `CLAUDE.md` at the repo root, a `.claude/` folder, "@claude" in their contributors list, and CI history shows many merged PRs from `claude/<feature>-<id>` branches. Read QZ's `CLAUDE.md` early — it documents their build commands, architecture patterns, and explicit verification steps for adding new device patterns ("Search for Similar Patterns... Analyze Pattern Specificity..."). Borrow conventions where useful for our own `CLAUDE.md`.
- **Licensing flag** (GPL-3.0): we can read and study QZ freely. We should NOT copy code from QZ into our project unless we're prepared to GPL-3.0 our project too. Plan to release this project under MIT or Apache 2.0 (more permissive, more common for hobby tooling), so any borrowing from QZ must be a clean-room reimplementation based on understanding the protocol or technique, not the code.

### TrainerRoad PowerMatch
- Not open source, but the concept (use external power meter as truth, drive smart trainer's resistance via FE-C) is well documented in TR's docs and is the "v0 state of the art" we're trying to improve on by moving the substitution into the bike itself rather than the app.

## Stages SB20 documentation (key excerpts)

These are official Stages docs; they explain what we're working with. Cite them in the Phase 0 report when relevant.

- Pair replacement power meter to SB20: https://support.stagescycling.com/support/solutions/articles/11000092592-pair-a-replacement-power-meter-to-a-stages-sb20-smart-bike
- How the SB20 power meters work: https://support.stagescycling.com/support/solutions/articles/11000094586-comment-fonctionnent-les-capteurs-de-puissance-du-stagesbike- (in spite of the URL, content is in English; documents that the L crank combines and re-broadcasts both sides)
- Riding with single-sided power: https://support.stagescycling.com/support/solutions/articles/11000092760-riding-the-stagesbike-with-left-only-power
- Recommended Zwift settings: https://support.stagescycling.com/support/solutions/articles/11000098622-empfohlene-einstellungen-zwift (key quote: "Right power meter ... Do not ever pair to this sensor. It will only send half power.")
- Pairing devices manual page: https://manuals.stagescycling.com/en/stages-bike/user-guide/pairing-devices/
- L power meter replacement: https://support.stagescycling.com/support/solutions/articles/11000131003-left-power-meter-replacement
- R power meter replacement: https://support.stagescycling.com/support/solutions/articles/11000131050-right-power-meter-replacement

### PedalSmart.blog (community / DIY — the best SB20-specific hardware resource we've found)
- https://www.pedalsmart.blog/
- A DIY maintenance & repair blog by "Craig", focused specifically on the **Stages SB20**: drive
  belt tension/alignment, flywheel bearings, bottom bracket, crank smoothness, **shifter buttons**,
  and power-meter / wireless-connectivity (Zwift + Stages app) troubleshooting — with video guides.
- No protocol / reverse-engineering content (mechanical + standard troubleshooting only), but it's
  invaluable for understanding the SB20's internals if we ever open one up, and the **shifter-button
  repair** material is directly relevant to the future "read the SB20 shifters over BLE"
  investigation (see `code/findings/forward-plan.md` → backlog).

## Favero Assioma documentation

- Favero FAQ: https://cycling.favero.com/faq/
- Assioma DUO/UNO user manual: https://fccid.io/2ATKD-ASSIOMA/User-Manual/Users-Manual-4371180.pdf
- Assioma PRO RS manual: https://www.manualslib.com/guide/4042999/favero-assioma-pro-rs-assioma-pro-rs-2-assioma-pro-rs-1-773-20-02-773-20-01-manual.html

Key facts for this project:
- ANT+ Bike Power profile compliant
- Pair to the LEFT pedal only on ANT+ (left transmits combined L+R)
- Self-calibrates while pedalling; manual zero-offset also supported
- Cycling Dynamics (PCO, PP, TE, PS, RP) over ANT+ when head unit supports them

## Forum discussions worth bookmarking

- TrainerRoad — SB20 + Assioma reconciliation: https://www.trainerroad.com/forum/t/bang-on-wrong-what-is-going-on-assioma-vs-sb20/69976 (background context on why these two meters report differently — relevant for "are our spoofed numbers believable?" questions)
- TrainerRoad — Stages SB20 owners: https://www.trainerroad.com/forum/t/stages-stagesbike-sb20-smart-bike/20502 (general SB20 community)
- TrainerDay SB20 owners: https://forums.trainerday.com/t/stages-sb20-owners-group/1014
- thisisant.com forum: openant rate issues: https://www.thisisant.com/forum/viewthread/7284
- thisisant.com forum: extended-messages discussion: https://www.thisisant.com/forum/viewthread/7298

## Hardware vendors

### ANT+ USB sticks
- Garmin (Dynastream) ANTUSB-m — VID 0x0FCF, PID 0x1009 — recommended.
- Garmin (Dynastream) ANTUSB2 — VID 0x0FCF, PID 0x1008 — also fine.
- CYCPLUS U1 — cheaper clone, generally works with openant. Quality variable.
- Suunto Movestick Mini — also available; openant compatible.

Buy two if budget allows. They're inexpensive (typically £20–35 each).

### Raspberry Pi
- Raspberry Pi 4 (2 GB or 4 GB) is plenty. Pi 3B+ also works. Pi Zero 2 W might struggle with two USB ANT sticks; not recommended for the deployment device.

## Things that look relevant but aren't (avoid these rabbit holes)

- **Garmin Connect IQ ANT+ APIs** — those are for apps running on Garmin watches/computers, not for our use case.
- **ant-android / ANT+ Android API** — proprietary, requires the ANT+ plugin services, doesn't help with PC/Pi-side spoofing.
- **PowerMatch settings deep-dives in TR/Zwift** — interesting context but not part of the implementation.
- **Calibrating the actual Stages cranks** — out of scope; we're replacing them.
