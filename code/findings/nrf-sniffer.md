# nRF Sniffer — passively watch the Stages-app ↔ SB20 BLE conversation

**Status: ✅ flashed + verified live (2026-06-21).** The
passive-sniff tier from [`traffic-observability.md`](traffic-observability.md): everything else captures
by *us connecting* to a device; the nRF Sniffer is the one tool that sees the **live conversation between
the Stages app and the SB20** — what the app actually writes to erg, whether it bonds — which becomes the
key diagnostic if the SB20 refuses our third-party control or the power-topology stays murky.

## TL;DR — quick start (the dongle is already flashed + live)

The dongle stays flashed with the sniffer firmware (enumerates at PID `522A`), so a normal capture is
just these four steps — the rest of this doc is the *why* + the one-time flashing recipe.

1. **Plug it in.** `sniff_ble.py` auto-detects it by PID `522A` — no Wireshark clicking needed.
2. **Scan** to get the target's *advertising* MAC (it can differ from the connect-time address, so always
   scan first and follow what the sniffer reports):
   ```bash
   python code/scripts/sniff_ble.py --scan-only --duration 12
   ```
3. **Capture — START IT BEFORE THE APP CONNECTS.** This is the one rule that matters: the sniffer can only
   *follow* a connection whose setup it witnessed; start late and you get **adverts only, no ATT/GATT**.
   Begin the capture, *then* open the Stages app + connect to the SB20 + ride:
   ```bash
   python code/scripts/sniff_ble.py --device <MAC-from-step-2> --duration 420 \
       --output code/findings/captures/SNIFF-sb20-app-$(date +%Y%m%d-%H%M).pcap
   ```
   (`--duration` secs; auto-detects the COM port. It's time-bounded + shuts the dongle down cleanly.)

   **⚠️ Follow the RIGHT device — and verify it live.** The SB20 presents **several BLE personalities**
   (the bike's FTMS at `E4:AA:5A:D6:0E:D4`, crank addresses "Stages 62144", an **"SB20 Bridge"**), and a
   controller app may connect to a different one than you expect — a 2026-07-06 qdomyos ride drove the
   bike's resistance **without ever connecting to `E4:AA:5A`**, so a 30-min follow of that address caught
   zero GATT. The cheap live check: **within ~1 min of the app connecting, the followed device's adverts
   must STOP** (a connected peripheral goes quiet). If `sniff_ble.py` keeps logging steady packet counts
   from adverts alone, you're following the wrong device — re-target *now*, not after the ride.
4. **Analyze.** Open the `.pcap` in Wireshark, or run it through the tshark→SQLite indexer
   (`sb20proxy.analysis.pcap_sqlite`) — **never hand-parse Nordic BLE pcaps**. Commit the `.pcap` to
   `findings/captures/` as the canonical record + note it in the session / `decisions.md`.

**Encrypted link?** If the app↔SB20 is bonded, payloads are ciphertext unless the sniffer caught the
**pairing** — so capturing from *before the first connect* matters doubly. **Fresh/wiped dongle?**
Re-flash via nrfutil DFU (it is **NOT** a UF2 drag-drop board) — see *How it was flashed* below.

## The hardware

An **nRF52840 USB dongle** (Nordic `VID 1915`). As shipped here it ran the **nRF Connect "connectivity"
firmware** (PID `C00A`, enumerates as *nRF52 Connectivity* + a CDC serial port; the "nRF52 Connectivity —
Error" in Device Manager is just the missing nRF-Connect driver — irrelevant once it runs the sniffer).

> ⚠️ **This dongle is NOT a UF2 drag-drop board** — despite the "MDK" label, it carries the **Nordic Open
> DFU Bootloader** (serial DFU, PID `521F`), *not* Makerdiary's UF2 bootloader. Entering the bootloader
> presents a **CDC serial port only — no `UF2BOOT` drive** ever mounts, so the wiki's "copy the `.uf2`"
> path does **not** apply here. It flashes the Nordic way: `nrfutil` DFU over the serial port (below).
> (Either it's a genuine Nordic nRF52840 Dongle / PCA10059, or its UF2 bootloader was replaced by nRF
> Connect at some point. Bootloader PIDs seen: app `C00A` → bootloader `521F` → sniffer app `522A`.)

## What's already installed/staged on the bike machine

| Piece | State |
|---|---|
| **Wireshark** | ✅ 4.6.6 (`winget install WiresharkFoundation.Wireshark`) |
| **nRF Sniffer extcap plugin** (v4.1.1) | ⚠️ was staged in `%APPDATA%\Wireshark\extcap` — **a Wireshark upgrade wiped it (found 2026-07-06)**. Durable copy: the makerdiary checkout — pass `--extcap-dir C:\repos\nrf52840-mdk-usb-dongle\tools\ble_sniffer\extcap` to `sniff_ble.py` (or re-stage into `%APPDATA%`, which survives until the next upgrade) |
| **Python deps** (`pyserial>=3.5`, `psutil`) | ✅ installed in the `py -3` interpreter the plugin uses |
| **`nrfutil`** + `nrf5sdk-tools` (DFU flasher) | ✅ `winget install NordicSemiconductor.nrfutil` then `nrfutil install nrf5sdk-tools` (gives legacy `pkg` + `dfu`) |
| **Sniffer firmware** (matched v4.1.1) | ✅ **flashed** — converted from the v4.1.1 `.uf2` to `C:\repos\nrf_sniffer_ble_v4.1.1.hex` → DFU package `C:\repos\sniffer_dfu.zip` |

Firmware + extcap come as a **matched pair** from `github.com/makerdiary/nrf52840-mdk-usb-dongle` (the
sniffer firmware version and the extcap version must agree — don't mix Nordic's extcap with this firmware).
The `.uf2` payload is a bare app at `0x1000` (no SoftDevice), so it ports cleanly to a `.hex`/DFU package.

## How it was flashed — nrfutil DFU (NOT UF2 drag-drop)

Because this dongle has the Nordic Open Bootloader (no UF2 drive), it's flashed over serial DFU. One-time
prep already done (see the table); the repeatable recipe:

1. **Convert** the matched `.uf2` → `.hex` (the `.uf2` is a bare app at `0x1000`; preserve that address)
   → `C:\repos\nrf_sniffer_ble_v4.1.1.hex`. *(Done — script was a small UF2-block parser; the `.hex` is
   committed-adjacent on the bike machine.)*
2. **Package** it for DFU (unsigned is fine — the dongle's Open Bootloader is signature-less):
   ```
   nrfutil pkg generate --hw-version 52 --sd-req 0x00 \
       --application C:\repos\nrf_sniffer_ble_v4.1.1.hex --application-version 1 \
       C:\repos\sniffer_dfu.zip
   ```
3. **Enter the bootloader:** unplug, **press and hold the button**, plug back in, hold ~2 s, release. It
   comes up as a **CDC serial port** (Nordic Open Bootloader, PID `521F`) — note the COM number. *(The
   bootloader window is short; have step 4 ready, or use a watch-and-flash loop that polls for PID `521F`.)*
4. **Flash:**
   ```
   nrfutil dfu usb-serial -pkg C:\repos\sniffer_dfu.zip -p COM<dfu> -t 60
   ```
   → `Device programmed.` The dongle reboots into the sniffer app (PID `522A`, a new CDC port).

**Verified (2026-06-21):** flashed `Device programmed.` over COM12; rebooted as **PID 522A on COM13**; and
`py -3 "%APPDATA%\Wireshark\extcap\nrf_sniffer_ble.py" --extcap-interfaces` lists
`nRF Sniffer for Bluetooth LE COM13` (before flashing it printed the extcap header but no interface). The
`SyntaxWarning: invalid escape sequence '\s'` lines are Python-3.13 nags in Nordic's script — harmless.

**To restore the connectivity firmware later:** same DFU flow with the `connectivity_*.hex` (or use nRF
Connect for Desktop → Programmer, which drives the same Open Bootloader).

## Hands-free capture — `scripts/sniff_ble.py` (the primary path; no Wireshark clicking)

For a Claude-driven bike session we don't drive Wireshark by hand — `code/scripts/sniff_ble.py` drives
Nordic's `SnifferAPI` directly (the same library the extcap uses): **scan → match the target by MAC →
`follow()` → stream a `.pcap`** (link-type `LINKTYPE_NORDIC_BLE`). It's time-bounded with a hard ceiling and
shuts the dongle down cleanly — it won't hang.

```bash
# confirm the dongle + see advertisers (and the SB20's *advertising* MAC, which may differ from
# the connect-time E4:AA:5A:D6:0E:D4 — always scan first and follow what the sniffer reports):
python code/scripts/sniff_ble.py --scan-only --duration 12

# follow the SB20 for 7 min, auto-detecting the dongle's COM port (PID 522A):
python code/scripts/sniff_ble.py --device <SB20_ADV> --duration 420 \
    --output code/findings/captures/SNIFF-sb20-app-$(date +%Y%m%d-%H%M).pcap
```

It needs `pyserial` and the staged `SnifferAPI` (found automatically in `%APPDATA%\Wireshark\extcap`, or pass
`--extcap-dir <makerdiary>\tools\ble_sniffer\extcap`). The pure address helpers are host-tested in
`code/tests/test_sniffer.py` (`sb20proxy.ble.sniffer`); the serial I/O is the hardware seam in the script.
The `.pcap` opens straight in Wireshark for analysis. Used by **session 6**.

## Using it on the SB20 — Wireshark GUI (interactive alternative)

1. Open **Wireshark** → double-click the **nRF Sniffer for Bluetooth LE COM13** interface.
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
