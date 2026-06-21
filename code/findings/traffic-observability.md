# Traffic observability — watch every meter + the SB20, on one clock, across rides

**Status: tooling built; the ride-day runbook.** As we sort the FTMS power-topology
(`sb20-power-topology.md`) and build out erg, the recurring need is to **capture all the traffic and
data at once** — the SB20's FTMS feed *and* every meter, over BLE *and* ANT+ — then reconcile it on one
clock. This is the map of what captures what, how it aligns, and where the nRF sniffer fits.

## The capture matrix

| Device / stream | ANT+ id | BLE | Captured by |
|---|---|---|---|
| **SB20** — FTMS Indoor Bike Data (`0x2AD2`) + erg control + shifter | — | `E4:AA:5A:D6:0E:D4` | `capture_ble_multi.py` (`kind=all`) — or `capture_ftms.py` for FTMS-only |
| **Stages L / combined** (CPS) | `62144` | yes | `07_capture_multi.py` (ANT) **+** `capture_ble_multi.py` (`cps`) |
| **Stages R** | `4963` | — | `07_capture_multi.py` (ANT) |
| **Assioma L** (CPS) | `17039` | yes | `07_capture_multi.py` (ANT) **+** `capture_ble_multi.py` (`cps`) |
| **Assioma R** | `22428` | — | `07_capture_multi.py` (ANT) |

**Two transports, two OS contexts:** BLE (bleak) is **Windows-native** (WSL has no Bluetooth); ANT+
(openant + the USB stick) runs in **WSL** (or Windows-native with a WinUSB driver). So a full ride is
**two capture processes** sharing the wall clock — reconciled on `iso_time` (below). They can run on
the one machine.

## The tools (all log `iso_time` + `monotonic_s` + `kind` JSONL → `findings/captures/`)

- **`capture_ble_multi.py`** *(new)* — watch **several BLE devices at once**, one JSONL, each
  notification tagged with its device `label`. `--device LABEL:KIND:ADDRESS` (×N); KIND = `ftms` |
  `cps` | `all` (every notify/indicate char — use for the SB20 to get FTMS + the shifter together).
- **`07_capture_multi.py`** — multi-meter **ANT+** on one stick (`--meter LABEL:ANTID` ×N).
- **`capture_ftms.py`** — the SB20's FTMS surface + the guarded erg drive (the §C tool).
- **`06_capture_ble.py`** — single-device BLE deep-dive (GATT dump, control-point recon, `--subscribe-all`).

## Reconcile — the SQLite analysis layer does the alignment

`13_build_sqlite.py` imports every capture into a derived index; `power_sample` unifies **ANT broadcasts,
FTMS Indoor Bike Data, and BLE CPS** into one `(stream, power_w, cadence_rpm)` stream, keyed by the
ANT channel label or the BLE `device` label. `reconcile()` aligns two streams per time bucket:

- **`basis="mono"`** — two streams in the *same* capture (e.g. the §D ANT+ grid: `stages` vs `assioma`).
- **`basis="iso"`** — two *separate* captures on the wall-clock second (the BLE-multi file vs the
  ANT-multi file). *This is how the two-process ride aligns.*

```bash
python code/scripts/13_build_sqlite.py --reconcile --basis iso \
    --capture-a MULTI-ble-<ts>.jsonl  --stream-a sb20 \
    --capture-b MULTI-ant-<ts>.jsonl  --stream-b assioma   # add --min-cadence 0 for a CPS stream
```
(BLE CPS cadence is derived from two crank samples, so it's NULL per-row — pass `--min-cadence 0` when a
CPS stream is involved; FTMS + ANT streams carry cadence.)

## The Phase 2 run-sheet — resolve the power-topology (which meter, what scale?)

The headline desk-follow-up from session 4. Capture **all meters at once during steady holds**, both
transports, then reconcile per hold. (`sb20-power-topology.md` Phase 2.)

1. **BLE half** (Windows) — the SB20 (FTMS + shifter) + both meters' BLE CPS:
   ```bash
   python code/scripts/capture_ble_multi.py --duration 900 \
       --device sb20:all:E4:AA:5A:D6:0E:D4 \
       --device stagesL:cps:<stages-62144-ble-addr> \
       --device assiomaL:cps:<assioma-17039-ble-addr> \
       --output code/findings/captures/MULTI-ble-$(date +%Y%m%d-%H%M).jsonl
   ```
   (BLE addresses: scan once with `06_capture_ble.py --adv-only`, or reuse a prior `G-*-ble-*` capture.)
2. **ANT half** (WSL, the stick — see the permission fix below) — all four ANT ids on one clock:
   ```bash
   python code/scripts/07_capture_multi.py --duration 900 \
       --meter stages:62144 --meter stagesR:4963 \
       --meter assiomaL:17039 --meter assiomaR:22428 \
       --output code/findings/captures/MULTI-ant-$(date +%Y%m%d-%H%M).jsonl
   ```
3. **Drive erg + hold steady** (so each target is a clean hold the reconcile can bin): from the dev box,
   `ride_control.py` / `capture_ftms.py --erg`, or the Ride Director. **Hit lap / note the time at each
   hold** (the §C trick that let the FIT align).
4. **Reconcile** each pair on `iso` and read the per-hold ratio (SB20 FTMS vs Assioma-total vs
   Assioma-L vs Stages) — settles single-vs-dual-sided, which meter the SB20 erg uses, and the true scale.
   **Cheap diagnostic:** pull the real L-crank battery — if the SB20 still gets power it's ESP-fed, if it
   drops it was on the real Stages crank.

## ⚙️ ANT+ stick on WSL — the permission fix (the in-session `[Errno 13]`)

The ANT capture died `[Errno 13] Access denied (insufficient permissions)`: openant couldn't claim the
USB stick because the `MODE=0666` udev rule isn't applied (WSL has no systemd by default to run udev).
Fix it **once**, at the desk, before the ride — verify, don't discover it on the bike:

1. **Enable systemd in WSL** (so udev applies the rule): in WSL, `/etc/wsl.conf` →
   ```ini
   [boot]
   systemd=true
   ```
   then from Windows `wsl --shutdown` and reopen WSL.
2. **Ensure the udev rule exists** (from CLAUDE.md §Setup):
   ```bash
   sudo tee /etc/udev/rules.d/42-ant-usb-sticks.rules >/dev/null <<'RULE'
   SUBSYSTEM=="usb", ATTRS{idVendor}=="0fcf", MODE="0666"
   RULE
   sudo udevadm control --reload-rules && sudo udevadm trigger     # then re-plug the stick
   ```
3. **Confirm USB passthrough** (Windows → WSL): `usbipd list` then `usbipd attach --wsl --busid <id>`
   (the Dynastream/Garmin `0fcf:1009` ANT-USB-m). `lsusb` in WSL should show it.
4. **Verify the claim works** *before* the ride: `python code/scripts/07_capture_multi.py --meter
   test:62144 --duration 5 --output /tmp/ant-smoke.jsonl` — it should open the channel, not `[Errno 13]`.

**Fallbacks:** `sudo` the capture (root bypasses the rule), or run ANT+ **on Windows-native** with a
WinUSB driver (Zadig → libusbK on the ANT stick) so BLE *and* ANT share one machine/clock. Fold the
working recipe back here after the next ride.

## Tier 3 — passive sniff the SB20 ↔ Stages-app conversation (the nRF stick)

Everything above works by *us connecting* to a device. The one thing it can't see is the **live
conversation between the Stages app and the SB20** (what the app actually writes to erg, whether it
bonds) — and that becomes the key diagnostic *if* the SB20 ever refuses our third-party control or the
topology stays murky. The ESP32 **can't** do this (no promiscuous mode / connection-following); the
right tool is a **Nordic nRF52840 dongle + the nRF Sniffer for Bluetooth LE + Wireshark** (~AU$15–35).
Caveat: it only decodes an **unencrypted** connection (if the SB20↔app link is bonded/encrypted, even a
real sniffer sees ciphertext — capture the pairing to get the keys). Order one; when it arrives, add an
`nrf-sniffer.md` with the Wireshark setup. *(See the earlier note: ESP32 = active roles; nRF = passive
sniff.)*
