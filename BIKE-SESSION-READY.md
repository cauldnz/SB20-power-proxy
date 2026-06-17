# 🟢 Bike session — READY (prepared 2026-06-17 from the desk machine)

**Start-here for the session you open on the bike machine.** Everything below was verified on the
desk before this was written. The run sheet to work through is
[`NEXT-BIKE-SESSION.md`](NEXT-BIKE-SESSION.md) — read it; this page is the status + coordinates +
"how to guide" that go with it.

> **If you're the assistant on the bike machine:** your job is to **guide the rider live** through
> `NEXT-BIKE-SESSION.md` (like the previous session) — one step at a time, watching the data land.
> The headline tonight is **§9 (ESP32 BLE proxy → SB20 — the product test)**, run *after* §5. Must-dos
> are §1, §2, §5, §7; §6 is a do-regardless capture. Don't dump the whole sheet at once — walk it.

---

## What's already done (verified on the desk, 2026-06-17)

- **Repo is current & clean.** `main` == the work branch == `030055e`, pushed. Pull it on the bike
  machine. Today's ESP32 dual-role BLE proxy is merged in.
- **Tooling smoke-tested (hardware-free):** `pytest` **121 passed, 1 skipped**; `ruff check src
  tests` clean; **openant 1.3.4** imports; the **ANT+ static-replay loopback PASSES**
  (`03_static_replay.py --radio loopback` → "twin saw spoofed Stages power"); the BLE harness
  (`fake_meter.py`, `crank_reader.py`) and `ride_web.py` all parse/run.
- **ESP32-C3 OLED board is flashed** with the current `main` firmware (`esp32c3-oled-live`, via OTA)
  and **functionally confirmed**: it received 188 W / 82 rpm from the Python `fake_meter` over BLE,
  then cleanly returned to `searching`. It's on WiFi and idle, ready.

## Device coordinates (the ESP32 you'll carry to the bike)

| | |
|---|---|
| **Hostname** | `sb20proxy.local` |
| **IP (current)** | `192.168.1.165` (DHCP on the `Donnie Boon` WiFi — should be stable; mDNS name is the safe bet) |
| **Status JSON** | `http://sb20proxy.local/` → `{source, src_power_w, src_cadence_rpm, power_w, cadence_rpm, ...}` |
| **Dashboard** | `http://sb20proxy.local/ui` (shows **METER IN → CRANK OUT**) |
| **Debug log** | `http://sb20proxy.local/log` (scan → connect → control-point activity) |
| **OLED** | shows the IP + live watts/cadence |
| **Spoofs as** | `Stages 62144` (CPS crank); **reads** any CPS meter that ISN'T named "Stages 62144" (i.e. the Assioma) |
| **Re-provision** | `http://sb20proxy.local/forget` → reboots into the `SB20-Setup` portal |

> The board must be on the **same WiFi** as the bike machine for `sb20proxy.local` + `/ui` to work.
> It's currently joined to `Donnie Boon`. If the bike is on a different network, re-provision via the
> `SB20-Setup` captive portal (see `firmware/BENCH-FLASH.md` / `NEXT-BIKE-SESSION.md` §8).

## Quick "is it alive?" check (bike machine, PowerShell)

```powershell
(Invoke-WebRequest http://sb20proxy.local/ -UseBasicParsing).Content
# healthy idle  -> "source":"searching", power 0
# reading meter -> "source":"connected","src_power_w":<your Assioma watts>
```

## Bike-machine pre-flight (only if you'll run captures / the harness there)

```bash
cd code && python -m venv .venv && .venv/Scripts/activate   # or source .venv/bin/activate (WSL)
pip install -e ".[dev,ble]"
pytest -q                                                   # 121 passed, 1 skipped
```
ANT+ captures need the stick attached (WSL: `usbipd attach`; see `NEXT-BIKE-SESSION.md` pre-flight).

## ⚠️ Restore values — WRITE THESE DOWN before changing any pairing

**Stages `62144` (L) : `4963` (R)** · crank length **165 mm** · zero-offset **L 903 / R 951**.
The SB20 pairs the crankset as a **linked L/R pair**. For the spoof tests only the **L** changes;
**R stays `4963`** (the real right crank stays in). Restore to the above before you finish.

## Tonight's path (from NEXT-BIKE-SESSION.md)

1. **§1** fresh CR2032 in the L crank · **§2** firmware + R-crank battery
2. **§5** erg-on-BLE GATE (go/no-go for the whole ESP32 direction)
3. **§6** BLE recon capture (do regardless)
4. **§7** Phase 1B ANT+ spoof-pairing proof
5. **§9** ⭐ **ESP32 BLE proxy → SB20** — the product test (exploratory; expect to iterate). The
   device reads the Assioma and re-presents it to the SB20 as the spoofed crank. Watch `/ui` and
   `/log`; each rung (pairs → shows watts → ergs) is a win. If it stalls, the SB20's writes to our
   crank (in `/log`) are the **Session G Part B** spec we refine the firmware from.

**Duplicate-advertiser gotcha (§9):** pedaling also spins the **real** Stages L crank, which also
advertises "Stages 62144" over BLE → two advertisers. To make the SB20 pair to the **ESP32**, pull
the real L-crank battery for that test (R `4963` stays), then restore.

**Live support:** narrate in chat ("battery done", "BLE on", "pairing now", "SB20 shows 210 W") and
the assistant reads the capture files + `curl`s the ESP32 as you go.
