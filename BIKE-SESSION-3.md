# 🚴 Bike session 3 — verify the firmware fixes + map the shifters

**Status: 🟡 PLANNED** · tracked in [`sessions/README.md`](sessions/README.md). **As you guide this
live, record each step's result inline** (`✅`/`❌`/`⚠️` + observed values / `/log` lines), then set
`Status: ✅ DONE (date)` with a one-line Outcome here. See CLAUDE.md → *Session plans & the session ledger*.

> **Start-here for tonight.** Today = **session 3**. This is the whole run sheet — self-contained.
> It's **live-guided**: narrate each step in chat ("flashed", "battery out", "pairing now", "pressed
> LEFT-up") and I read `/log` + the capture files off the machine as you go. Don't rush ahead — walk it.

Prepared 2026-06-19, on branch **`bike-session-3`** (cut from `main` after session 2 + PR #5). Two goals:

- **A. Verify the control-point / reconnect firmware fixes** (PR #5) — the things that broke in session 2.
- **B. Comprehensively map the shifter-over-BLE protocol** — toward emulating a **Zwift Click** (your ask).

**~45–60 min.** Bring: a fresh **CR2032** + coin/screwdriver (for restoring the L crank), your phone
with the **Stages Cycling** app, and this chat open.

---

## Status going in (what session 2 + PR #5 established)

- ✅ **Power acceptance works.** Session 2 proved Assioma → ESP → SB20 relays **power *and* cadence**,
  crank-free (the real L-crank battery was pulled). The fidelity fix landed.
- ✅ **PR #5 fixed the session-2 breakages** (this is what we verify in **A**): the ESP now *answers*
  the SB20's control point (zero-reset / crank-length), **re-advertises on disconnect** (no reboot
  needed), and **pins the meter source** so it stops bouncing between the Assioma and the real cranks.
- 🔬 **Shifters broadcast over BLE** (discovered session 2). The SB20 fires char `0c46be60` on each
  press with a one-hot gear bitmask. **B** maps it fully. See `code/findings/shifter-ble-protocol.md`.

---

## Device coordinates (the ESP32 you carry to the bike)

| | |
|---|---|
| **Hostname / IP** | `sb20proxy.local` → `192.168.1.165` (DHCP on the **`Donnie Boon`** WiFi; mDNS name is the safe bet) |
| **Status JSON** | `http://sb20proxy.local/` → `{source, src_power_w, src_cadence_rpm, power_w, cadence_rpm, rssi, ...}` |
| **Dashboard** | `http://sb20proxy.local/ui` — METER IN → CRANK OUT |
| **Debug log** | `http://sb20proxy.local/log` — scan → connect → **control-point writes** (the spec we need) |
| **OTA flash** | `POST http://192.168.1.165/update` (multipart) — see Pre-flight |
| **Spoofs as** | `Stages 62144` (CPS crank); **reads** the meter named **`ASSIOMA`** (loops are guarded — never reads its own spoof) |
| **Cal offset answered** | `903` (the captured 0xAC zero-offset — this is what the zero-reset returns) |
| **The SB20 itself** | `Stages Bike 0105`, addr **`E4:AA:5A:D6:0E:D4`** · FTMS `0x1826` + CSC `0x1816` + vendor `0c46be5f` |
| **Bike-machine repo** | `C:\repos\cauldnz\SB20-power-proxy` |

> The ESP must be on the **same WiFi as the bike machine** for `sb20proxy.local` / `/ui` / `/update` to
> resolve. It's joined to `Donnie Boon`. Different network at the bike? Re-provision via the `SB20-Setup`
> captive portal (`firmware/BENCH-FLASH.md`).

## ⚠️ Restore values — WRITE THESE DOWN before you change any pairing

**Stages `62144` (L) : `4963` (R)** · crank length **165 mm** · zero-offset **L 903 / R 951**.
The SB20 pairs the crankset as a **linked L/R pair**; for the spoof only the **L** changes, **R stays
`4963`** (the real right crank stays in). Restore to the above before you finish.

---

## 0 · Pre-flight — flash the latest firmware + confirm it's alive · ~10 min

The board must run **this branch's** firmware — the **PR #5** control-point/reconnect fixes (what **A**
verifies) **plus** the OLED-off-loop perf fix (renders on its own task, so the panel never stalls the
hot loop; `WiFi -XX` RSSI is back on the OLED title). Built + compile-verified at the desk; the bike
just flashes it.

**Recommended — the reliable flash helper** (`firmware/flash.ps1`, Windows PowerShell from `firmware/`):
it does an RSSI pre-flight, OTA with auto-retry, and verifies the reboot.
```powershell
cd firmware ; .\flash.ps1            # build + OTA esp32c3-oled-live -> sb20proxy.local (retries)
```
**Manual alternative** (build on any pio host — WSL is fine, Bluetooth isn't needed to build — then
push the image over WiFi):
```bash
cd firmware && pio run -e esp32c3-oled-live-ota
curl -F firmware=@.pio/build/esp32c3-oled-live-ota/firmware.bin http://192.168.1.165/update
```

- **Confirm it's alive + on the new build:** `curl http://sb20proxy.local/` → expect `source:searching`
  and a **low uptime** (it just rebooted). Then pedal a few strokes and confirm `source:connected` with
  `src_power_w` tracking the Assioma. *(Bonus observability this session: `curl /stats` → loop p50/p95/max
  + heap + reboot count — handy if anything feels janky.)*
- **OTA dropping?** It's the signal. The C3's OTA gets unreliable below ~**−72 dBm** — check the
  `WiFi -XX` line on the OLED (or `rssi` in `curl http://sb20proxy.local/`) and move the board nearer the
  AP, then retry. `flash.ps1` warns you and retries automatically.
- **Can't reach `/update`?** The board may be on a different network — re-provision (portal) or flash
  over **USB** (`.\flash.ps1 -Mode usb`, or `pio run -e esp32c3-oled-live -t upload`; C3 USB-JTAG wedge →
  HOLD BOOT, TAP RESET, RELEASE BOOT, re-run, power-cycle — see `firmware/BENCH-FLASH.md`).

Open a **rolling `/log` window** in a second terminal now — you'll watch it through both A and B:
```powershell
while ($true) { (iwr http://sb20proxy.local/log -UseBasicParsing).Content; sleep 3 }
```

---

## A · Firmware-fix verification — the session-2 failures, now fixed · ~20 min

**Set-up.** Fresh **CR2032 not needed yet** — for this part **pull the real LEFT-crank battery** so the
ESP is the only `Stages 62144` (the real L also advertises that name → duplicate). Leave **R `4963`** in.
Then: SB20 → Stages app → **Pair with Bluetooth** → pair to **Stages 62144** (now only the ESP).
Pedal → confirm **power + cadence** show on the SB20 / OLED / `/ui` (the session-2 regression check).

Now, watching the `/log` window, run each check:

1. **Zero-reset COMPLETES.** Stages app → calibrate / zero-reset. Expect it to **succeed** now — we ACK
   `0x10` (Enhanced Offset Compensation) with a synthetic success — instead of spinning. `/log` shows
   `[cp] write 10` then our response, **no disconnect**. *(Last time it spun forever.)*
2. **Crank length sets + reads back.** Set crank length (e.g. 170 mm) → expect it to **stick** and the
   app to read it back (we handle `0x04` set + `0x05` request). `/log` shows `[cp] write 04 …`.
3. **Reconnect without a reboot.** Force a disconnect (toggle the app's pairing, or walk the ESP out of
   range and back) → the ESP should **re-advertise and the SB20 reconnect on its own**, with **no ESP
   power-cycle**. `/log` shows `[srv] disconnect …` then a fresh `[srv] connect …`, no reboot between.
4. **Constant handshake holds.** Confirm `[prop fe02] write bfda1853` still appears on each connect.

**✅ Pass:** all four behave as above → the PR #5 responder is correct on the real bike.
**❌ Any step still fails:** **save the `/log` dump** and paste it — that's the exact spec we iterate
the responder against (reflash ~5 min). Tell me which step and what the app UI showed.

---

## B · Comprehensive shifter probe — toward a Zwift Click · ~20 min

**Why:** the headline future feature — read the SB20's shifter buttons and re-present them to **Zwift as
a Zwift Click**, giving an SB20 rider Zwift **virtual shifting** with no extra hardware (one ESP can be
both the crank-power peripheral *and* the Click). Tonight is the **read-side map**: which button → which
gear bit, the full range, and what the silent channels do.

Connect to the **SB20 itself** and subscribe to **everything**. Do this with the **Stages app
disconnected** from the SB20 (avoid contention). On the bike machine, native PowerShell:

```powershell
C:\repos\cauldnz\SB20-power-proxy\code\.venv-win\Scripts\python.exe `
  C:\repos\cauldnz\SB20-power-proxy\code\scripts\06_capture_ble.py `
  --address E4:AA:5A:D6:0E:D4 --subscribe-all --duration 300 `
  --output "C:\repos\cauldnz\SB20-power-proxy\code\findings\captures\SHIFTER-probe-3-$(Get-Date -Format yyyyMMdd-HHmm).jsonl"
```

Once it reports connected + subscribed, **press deliberately and narrate each press** (so I can line up
timestamps). The open questions, in order:

1. **One button at a time, isolated** — the key map. LEFT-up ×3 (pause ~3 s), LEFT-down ×3 (pause),
   RIGHT-up ×3 (pause), RIGHT-down ×3. Maps each button → **which way the gear bit walks** (up vs down).
2. **Walk the full range.** Shift all the way one way, then all the way back → the **total gear count**
   (bits past `0x20`? does the field widen past one byte?) and whether it **clamps or wraps** at the ends.
3. **The 3rd button per side.** Press it alone — what does it do? (No haptic felt in session 2.)
4. **The silent channels** `0c46be61` and `0c46beb0` — does *anything* ever wake them? (Second shifter
   pair? a long-press / mode? front vs rear?)
5. **(Optional, cautious — only if 1–4 are done.)** The write candidate `0c46beb1`
   (write-without-response) — can we **write a gear value to inject a shift**, reading it back on
   `0c46be60`? Read-side first; probe writes gently and narrate, in case it desyncs the bike's gear.

**✅ Pass:** the JSONL captures the per-button bit walk + range behaviour. **Send me the file** — I'll
finish `code/findings/shifter-ble-protocol.md` and we scope the Zwift-Click research from there.

*(Reference — session 2 saw type-`01` payloads `01 00 <gear:u16 LE>`, one-hot: Right ①②③ → `0x08/0x10/0x20`,
Left ①②③ → `0x01/0x02/0x04`. Per-button direction + range are exactly what tonight nails down.)*

---

## 🔁 Restore (before you leave)

Reinsert **both** crank batteries → re-pair the SB20 to **`62144` (L) : `4963` (R)**, **165 mm**,
zero-offsets **903 / 951**, back to normal mode → pedal once to confirm the **real cranks** read.

---

## If anything goes sideways

- **Can't reach the ESP** (`/`, `/log`, `/update` time out) → wrong WiFi or asleep. Check it's powered
  and on `Donnie Boon`; try the IP `192.168.1.165` directly; last resort re-provision via `SB20-Setup`.
- **OTA flash drops** → weak signal (see Pre-flight); move the board nearer the AP, or USB-flash.
- **Meter bounces** in `/log` (connects to a `Stages …` crank, not the Assioma) → tell me; we pin the
  source to the Assioma's exact address (`Config::METER_ADDRESS`) and reflash. *(Should be fixed by the
  PR #5 pinning, but the two Assioma pods + the real cranks are all CPS advertisers — flag it if it bounces.)*
- **Shifter capture connects but no notifications on press** → confirm the Stages app is **disconnected**
  from the SB20 (contention), and that you're on the SB20's own address `E4:AA:5A:D6:0E:D4`.
- **Ctrl-C any capture any time** — already-captured data is kept. Screenshot any confusing app UI; I'll read it.
