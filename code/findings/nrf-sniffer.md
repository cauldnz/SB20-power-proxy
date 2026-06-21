# nRF Sniffer — passively watch the Stages-app ↔ SB20 BLE conversation

**Status: tooling staged on the bike machine (2026-06-21); firmware flash pending (physical).** The
passive-sniff tier from [`traffic-observability.md`](traffic-observability.md): everything else captures
by *us connecting* to a device; the nRF Sniffer is the one tool that sees the **live conversation between
the Stages app and the SB20** — what the app actually writes to erg, whether it bonds — which becomes the
key diagnostic if the SB20 refuses our third-party control or the power-topology stays murky.

## The hardware

A **Makerdiary nRF52840-MDK USB Dongle** (USB `VID 1915 / PID C00A`). As shipped here it ran the **nRF
Connect "connectivity" firmware** (enumerates as *nRF52 Connectivity* + a CDC serial port, **COM11**; the
"nRF52 Connectivity — Error" in Device Manager is just the missing nRF-Connect driver — irrelevant once
it runs the sniffer). It has Makerdiary's **UF2 bootloader**, so flashing is drag-drop, no nrfutil/JLink.

## What's already installed/staged on the bike machine

| Piece | State |
|---|---|
| **Wireshark** | ✅ 4.6.6 (`winget install WiresharkFoundation.Wireshark`) |
| **nRF Sniffer extcap plugin** (v4.1.1) | ✅ staged in `%APPDATA%\Wireshark\extcap` (`nrf_sniffer_ble.py` + `.bat` + `SnifferAPI/`) |
| **Python deps** (`pyserial>=3.5`, `psutil`) | ✅ installed in the `py -3` interpreter the plugin uses |
| **Sniffer firmware** (matched v4.1.1 `.uf2`) | ⏳ cloned, not yet flashed — `C:\repos\nrf52840-mdk-usb-dongle\firmware\ble_sniffer\nrf_sniffer_for_bluetooth_le_v4.1.1.uf2` |

Firmware + extcap come as a **matched pair** from `github.com/makerdiary/nrf52840-mdk-usb-dongle` (the
sniffer firmware version and the extcap version must agree — don't mix Nordic's extcap with this firmware).

## The one remaining step — flash the sniffer firmware (physical)

1. **Enter the UF2 bootloader:** unplug the dongle, **press and hold its button**, plug it back into USB,
   **release** once it mounts as a USB drive named **`UF2BOOT`**.
2. **Flash:** copy `nrf_sniffer_for_bluetooth_le_v4.1.1.uf2` (path above) onto the `UF2BOOT` drive. It
   reboots itself when the copy finishes.
3. After flashing, the dongle's button becomes **RESET** — double-click it to re-enter the bootloader
   later (e.g. to restore the connectivity firmware).

**Verify:** the USB identity changes (no longer "nRF52 Connectivity"), and
`py -3 "%APPDATA%\Wireshark\extcap\nrf_sniffer_ble.py" --extcap-interfaces` now lists an
`nRF Sniffer for Bluetooth LE COMxx` interface (before flashing it printed the extcap header but no
interface). Then Wireshark → that interface appears in the capture list.

## Using it on the SB20

1. Open **Wireshark** → double-click the **nRF Sniffer for Bluetooth LE COM11** interface.
2. In the sniffer toolbar's **Device** dropdown, pick the **SB20** (`E4:AA:5A:D6:0E:D4`) to *follow* it —
   the sniffer locks onto that device's connections (it can only follow one connection at a time).
3. **Start sniffing BEFORE the app connects** so the connection setup (and any pairing) is captured —
   then open the **Stages app** and connect to the SB20. Pedal / drive erg.
4. Watch the **FTMS Control Point** (`0x2AD9`) writes the app makes (Request Control / Set Target Power)
   and the **shifter** char — the conversation we can't see by connecting ourselves.

**Encryption caveat:** if the SB20 ↔ app link is **bonded/encrypted**, the connection payload is
ciphertext unless the sniffer has the key. Capture the **pairing** (sniff from before the first connect)
and the extcap can derive/accept the key (LTK / passkey field in its toolbar); a `result 0x05
control-not-permitted` to *us* in §C plus an encrypted app link would point to "the app bonds, we don't".

**Save the capture:** Wireshark → save as `.pcapng` → commit to `findings/captures/` as the canonical
record (the BLE-link analogue of our JSONL). Note it in the relevant session / `decisions.md`.

## Why the ESP32 couldn't do this

The ESP32's BLE stack has no promiscuous / connection-following mode — it can't sniff a connection between
two *other* devices. The nRF52840 + this firmware follows the channel-hop sequence. (ESP32 = *active*
roles — the spoof, the FTMS loop; nRF = *passive* sniff. See `traffic-observability.md`.)
