# 🚴 Bike session 4 — ground the Enhanced-Offset (0x10) format + FTMS erg + brake-lever probe

**Status: ✅ DONE (2026-06-21)** · ran ~09:43–11:23 local (UTC+10) · tracked in
[`sessions/README.md`](README.md). Run via [`sessions/PLAYBOOK.md`](PLAYBOOK.md).
**Outcome:** **§C FTMS erg ✅ PASS** — the SB20 accepts + holds a *third-party* Set-Target-Power (spec-built
FTMS codec validated vs real frames) — but the owner's independent cross-check surfaced a **power-topology
finding**: the SB20's erg power reads only ~½–⅔ of the Assioma (likely **single-sided**), needing a
multi-device follow-up → [`code/findings/sb20-power-topology.md`](../code/findings/sb20-power-topology.md).
**§B shifter fully mapped** (brakes app-gated; **6** buttons, 4/5≡1/2; chord/double-tap/hold-vs-tap all
characterized → hold-to-ramp viable). **ANT+ stick** brought up + documented (one permission step for desk).
**G1/G2 + §D deferred** — no flash this session (board WiFi/OTA was down; would need USB `flash_c3.py`).
**⏱ Timestamp from the start** (note `HH:MM` at the session start and each section — per the playbook, so
planned-vs-actual is recorded, not reconstructed). *(Times below are real wall-clock from the capture
`iso_time`s; the agent's local shell clock was unreliable this session — retro item.)*

### Live run log (actuals)
- **~09:43** — Session start. Synced to `origin/main` `a536b4c` (overnight FTMS stack F1–F7 + §D + `flash_c3.py`
  landed). Tools verified: venv-win python, `capture_ftms.py` (`--erg/--erg-targets/--erg-hold` present),
  `06_capture_ble.py` (`--subscribe-all/--control-point` present).
- **~09:43** — Board pre-check `curl /`: `source:searching`, heap 129 k, uptime ~227 s, **RSSI −78 dBm**
  (⚠️ below −72 OTA threshold → expect to USB-flash via `flash_c3.py` before G2, NOT `flash.ps1 -Mode usb`).
- Run order (per owner): **§C (erg) → §B (probe) → flash → G1 → G2 → §D (optional, needs ANT+ stick).**
- **09:49–09:51** — **§C FTMS erg: ✅ PASS** (ran by address; app off, phone BT off). Detail under §C below.
  Capture committed-pending: `findings/captures/G-sb20-ftms-erg-20260621-0949.jsonl`. Ran the capture
  agent-side (background) per owner — rider only pedalled.
- **10:00–10:33** — **§B shifter probe: ✅ DONE** (full detail in §B). Brakes app-gated (no BLE w/ app off);
  buttons = **6** (4/5 alias 1/2); gestures chord `0x24` / double-tap / hold-vs-taps all characterized.
  Captures `SHIFTER-probe-4-*`.
- **10:43** — **§C erg RE-TEST (owner ask — validate that "200 W" is real).** Erg drive held the SB20 at
  ~200 W (IBD 182–223 @ ~72 rpm; handshake all `success`) — **FTMS control re-confirmed solid.** But the
  **owner's Garmin read ~260 W** → a possible **~30 % under-read** by the SB20 (or its power source) vs a
  real meter. ⚠️ The intended ESP/Assioma cross-check **FAILED** — WiFi at **−89 dBm**, all 26 polls timed
  out (the `/`-endpoint reference is unusable at this signal). **Garmin = Assioma over ANT+**
  (owner-confirmed) = real pedal power ⟹ SB20 erg ~200 W ≈ **~260 W Assioma** (SB20 ~23 % low / Assioma
  ~30 % high). ⚠️ **Puzzling — no obvious source fits:** if SB20 ergs off the **Stages cranks** they read
  *high* (prior `decisions.md`: +5–13 % vs Assioma) → Assioma should be <200, but it's 260 ✗; if off the
  **ESP-relayed Assioma** (correction ~1:1, seen at 6 W→6 W) it should *match* 260, not 200 ✗; SB20-own-meter
  contradicts the "ergs off the crank" premise. **Real, unexplained ~30 % gap — flag-and-verify; resolve via
  the §D grid + a dense independent feed (ESP `/` was unusable at −89 dBm).** **Owner will provide the Garmin `.FIT`
  (dense Assioma-over-ANT+) post-ride** → desk-reconcile against the timestamped SB20 erg captures
  (`G-sb20-ftms-erg-…0949` & `…erg200-104341`), aligning on the 150/200/100 step fingerprint. No on-bike
  ANT+ bring-up needed for this. **This is the #1 desk follow-up.**
- **11:06** — **§C erg 3-way sweep (100/150/200 W), ESP now reliable (−60 dBm after the power-cycle).** SB20
  faithfully held its *own* targets (IBD means **113 / 153 / ~200 W**) but the **Assioma (ESP `src_power_w`,
  dense ~3 s poll) read 1.3–1.6× higher**: 100→**152 W (1.34×)**, 150→**248 W (1.62×)**, 200-hold smeared by
  the release tail (earlier dedicated 200-test: SB20 ~200 / Garmin-Assioma ~260 = **1.30×**). ⟹ **the SB20's
  power runs ~25–40 % below the Assioma — now repeatable across two independent methods (Garmin spot-read +
  ESP live poll).** It's the **power *scale*, not the erg control** (control holds target fine). Exact factor
  + flat-scale-vs-curve = the Garmin-`.FIT` desk job. Captures `G-sb20-ftms-erg3way-20260621-110555.jsonl`
  (SB20) + ESP poll log. ⚠️ ANT+ capture (`02_capture_assioma.py --device-id 17039`) **errored exit 2** —
  diagnose at desk (env built fine: openant 1.3.4; stick visible in WSL). ESP + FIT covered the Assioma.
- **~11:10** — **Owner hit lap again; Garmin (Assioma total) ~380 W while SB20 erg target = 200 W → ~1.9× ≈ 2×.**
  Owner hypothesis (strong): **single-sided reading**. ~2× is the fingerprint of one meter on a single leg vs
  the other on true total. **Likely resolves the topology:** the SB20 appears to be running erg off the
  **real Stages LEFT crank, single-sided (~200)**, while the Assioma DUO reads dual-sided **total (~380)** —
  i.e. **the SB20 is NOT fed by the ESP/Assioma spoof this session, it's on the real Stages L crank.** Fits:
  SB20 (200) ≠ ESP `src_power_w` (~380), and the *variable* ratio 1.3–1.9× = L/R imbalance (a single-left
  source swings with leg dominance). Loose ends: tensions with prior `decisions.md` "Stages ~5–13 % high"
  (that was probably the *combined* 62144 stream, not single-L); the ESP `forwarded` subscriber unexplained.
  ⟹ **CONCLUSION (owner): this needs a deliberate, *simultaneous* multi-device capture — SB20-FTMS + both
  Stages streams (62144/4963) + both Assioma pedals (17039/22428), over BLE *and* ANT — then reconciled.
  Not live spot-checks. THIS is the headline desk follow-up (supersedes a simple "scale factor").** Capture `findings/captures/G-sb20-ftms-erg200-20260621-104341.jsonl`.

Prepared 2026-06-19 (desk) after session 3. **~55–70 min** (G1 ~10 · G2 ~10 · C ~10 · B ~15–20
[brake + button 4/5 map + gestures] · restore ~5). Three gates + a probe.

> **Priority order, by value (front-load these):** **§C (FTMS erg) is the go/no-go for the next feature**
> — do it first if time is tight. **§C + §B share one rig setup** (connect to the SB20 directly, Stages
> app disconnected) and **need no ESP**, so batch them. **G1/G2** (the A1 `0x10` work) need the flashed
> ESP + spoof pairing and are **protocol-completeness, not a product blocker** — secondary. The button
> 4/5 map and gestures (in §B) are "do if time".

- **G1 — capture the REAL crank's `0x10` Enhanced-Offset reply** (grounds the A1 desk fix; the one byte
  sequence the spec can't give us). ⭐ do this first.
- **G2 — re-test our spoof's `0x10`** after flashing the A1 desk fix (does the Stages app's calibrate
  now COMPLETE instead of spinning?).
- **C — FTMS erg-acceptance** ⭐ — does the SB20 erg off a **third-party** Set-Target-Power? The go/no-go
  for the *shifter-buttons-adjust-erg-watts* feature (`code/findings/shifter-erg-control.md`).
- **B — brake-lever / silent-channel shifter probe** (do the brake levers fire on `be61`/`beb0` or FTMS
  Status `0x2ADA`? — the session-3 open thread).

> ⚠️ **Realistic-time note (session-3 lesson):** "verify/retest" steps (G2) can become *investigation* if
> they fail — budget for it, don't assume quick. The time budget above already pads them.

> **Why G1 matters:** session 3 proved our old `0x10` reply (`20 10 01 00 00`, the 5-byte `0x0C` shape)
> leaves the app spinning. The desk fix sends the **spec-correct Enhanced shape**
> `20 10 01 <offset s16> <mfgCompanyId u16> <mfgData…>` (see `Cps.h encodeEnhancedOffsetCompResponse`),
> but the **exact company-id + any trailing manufacturer data are unknown** and were never passively
> sniffable. G1 actively elicits them from the real crank by *writing* `0x10` to it and logging its reply.

## Bring (pre-flight — per the playbook checklist)
A **fresh CR2032** + coin/screwdriver (the real L crank `62144` has read **12–14%** before — and **G1
needs it awake**, so check/replace it first; reinsert for the restore). Phone with the **Stages Cycling**
app. This chat open.

## Restore values — WRITE DOWN before changing any pairing
**Stages `62144` (L) : `4963` (R)** · crank length **165 mm** · ANT+ zero-offset **L 903 / R 951**.
Spoof reads `ASSIOMA`, advertises `Stages 62144`. SB20 itself = `E4:AA:5A:D6:0E:D4`.

---

## 0 · Pre-flight (desk-verified; bike just flashes)
The board needs the **A1 desk-fix firmware** (this branch / PR — the spec-correct `0x10` Enhanced reply).
From `firmware/`:
```powershell
cd firmware ; .\flash.ps1            # build + OTA esp32c3-oled-live -> sb20proxy.local (retries)
```
Confirm: `curl http://sb20proxy.local/` → `source:searching`, low uptime. Open a rolling `/log`:
```powershell
while ($true) { (iwr http://sb20proxy.local/log -UseBasicParsing).Content; sleep 3 }
```

## G1 · Capture the REAL crank's 0x10 Enhanced-Offset reply ⭐ (do FIRST)
This writes `0x10` to the **real** Stages L crank and logs its indication — the bytes that ground
`SPOOF_MFG_COMPANY_ID` (+ any manufacturer data). Native-Windows PowerShell:

1. **Power the ESP spoof OFF** (unplug it) so the only `Stages 62144` advertising is the **real** crank,
   and **leave the L-crank battery IN** (we need the real crank awake). Keep the cranks **still** (it's a
   zero-reset). *(Fallback if it grabs the wrong device: target the real crank by `--address` — its addr
   is in `G-crank62144-ble-zero-20260615-070353.jsonl`.)*
2. Run:
   ```powershell
   C:\repos\cauldnz\SB20-power-proxy\code\.venv-win\Scripts\python.exe `
     C:\repos\cauldnz\SB20-power-proxy\code\scripts\06_capture_ble.py `
     --name 'Stages 62144' --duration 120 `
     --control-point enhanced-offset-compensation `
     --output "C:\repos\cauldnz\SB20-power-proxy\code\findings\captures\G-crank62144-ble-enhanced-0x10-$(Get-Date -Format yyyyMMdd-HHmm).jsonl"
   ```
   The script connects, subscribes to the control-point indication, writes `0x10`, and logs the reply raw.

**✅ Pass:** the JSONL has a `ble_cp_indication` whose `raw_hex` starts `2010 01…` — that's the real
Enhanced reply. **Commit the JSONL.** Tell me the bytes; I ground `SPOOF_MFG_COMPANY_ID` (+ mfgData) in
them, update the golden test, and reflash. *(If the write is rejected/needs bonding, note it — the
spec-structure fix from G2 may still satisfy the app on its own.)*

## G2 · Re-test our spoof's 0x10 (the A1 payoff)
1. Reconnect/power the ESP; **pull the real L-crank battery** so the SB20 pairs to the ESP (`Stages 62144`).
   SB20 → Stages app → Pair with Bluetooth → pedal a few strokes (wake the chain), confirm power+cadence.
2. Stages app → **calibrate / zero-reset.** Watch `/log` for `[cp] write 10` then our reply.

**✅ Pass:** the calibrate UI **COMPLETES** (no longer spins), link holds, `/log` shows our `20 10 01 …`
Enhanced reply. **❌ Still spins:** the app wants the real captured bytes from G1 (or bonding) — paste
`/log` + which we tried; desk-iterate the Config values and reflash (~5 min).

## B · Brake-lever / silent-channel shifter probe (~10 min)

> ### ✅ §B DONE (2026-06-21 10:00–10:33) — brakes app-gated · buttons = 6 (4/5 alias 1/2) · gestures characterized
> **Brakes — ❎ no BLE signal with the app off.** Capture `SHIFTER-probe-4-20260621-1000.jsonl` (420 s, SB20
> direct `E4:AA:5A:D6:0E:D4`, Stages app closed): rider squeezed **LEFT brake ×3** then **RIGHT brake ×3** →
> **zero** frames on `0c46be60`, `0c46be61`, `0c46beb0`, or FTMS Status `0x2ADA` (only idle Indoor-Bike-Data
> / CSC). **Rider reports the brakes *do* slow the flywheel — but only when the Stages app is connected;
> inert with it closed.** ⟹ the brake function is **app-gated**, not a free always-on BLE button like the
> shifters. Catching brake traffic would need the **app connected + a dual-connection sniff** → follow-up
> thread (lower priority). *(Process miss: the 420 s window lapsed during an off-bike discussion, so this
> file holds only the brake test + idle; the button-4/5 map + gestures are captured separately below.
> Lesson → PLAYBOOK: stop the capture when the live work pauses.)*
>
> **Buttons 4 & 5 — ❎ aliased to 1 & 2 (no new BLE bits); budget stays at 6.** Capture
> `SHIFTER-probe-4-buttons45-20260621-101727.jsonl`. Each press → a `0c46be60` frame `<u16 A><u16 btn-bitmap>`;
> the **button-bitmap field is one-hot and matches session 3**: **L1 `0x01`, L2 `0x02`, L3 `0x04`, R1 `0x08`,
> R2 `0x10`, R3 `0x20`**. **LEFT 4 → `0x01` (byte-identical frames to L1), LEFT 5 → `0x02` (= L2); RIGHT 4 →
> `0x08` (= R1), RIGHT 5 → `0x10` (= R2)** — both sides. So the hidden 4/5 are indistinguishable from 1/2
> over BLE ⟹ **6 usable signals, not 10**; the shifter→erg / Zwift-Click button budget is **6**
> (`shifter-erg-control.md`). *(Aside: each discrete press emits a burst of ~10–15 repeat frames `0100<bit>`
> then a terminal `0400/0800 <bit>` — relevant to the hold-vs-tap gesture test next.)*
>
> **Gestures** (capture `SHIFTER-probe-4-gestures-20260621-102429.jsonl`; 3rd/control buttons L3 `0x04`,
> R3 `0x20`). Key: the button-bitmap field is a **true OR of buttons currently down**, not strictly one-hot.
> - **Chord (L3+R3 together) — ✅ usable.** Holding both yields a sustained **`01002400`** (bitmap `0x24` =
>   `0x04`|`0x20`) on all 3 reps. Press/release edges are messy (whichever lands first shows alone for a
>   frame), but the both-down `0x24` state is clean ⟹ a "both-3rd-buttons" chord is a distinct **7th** signal.
> - **Double-tap (R3) — ✅ detectable.** Each physical tap = a short `0100<bit>` burst ended by a release
>   edge `0800<bit>`; a double-tap = **two press→release cycles**, inter-tap gap ~110–180 ms ⟹ separable from
>   a single press (one cycle). [A-field convention: `0x01` = held/repeat, `0x08` = release edge.]
> - **Hold vs taps — ✅ cleanly distinguishable (resolves the session-3 UNRESOLVED question).** Capture
>   `SHIFTER-probe-4-holdtaps-20260621-103001.jsonl`. **HOLD** (L3, ~2 s ×2) = one *continuous* `01000400`
>   stream at a steady ~8 Hz (burst every ~120 ms) for the whole hold, with a periodic **`030004000400`
>   "still-held" auto-repeat marker (~7/s)**, ended by **exactly one** release `04000400` (two holds → two
>   releases, ~4.3 s & ~5.3 s). **5 FAST TAPS** = **five** short `01000400` bursts (~0.1–0.2 s), each ended by
>   its own release `08000400`, ~150–360 ms apart, with **no `0300…` marker at all**. ⟹ held-vs-tapped is
>   decided by (a) the periodic `0300…` still-held marker (hold-only), (b) continuous-stream duration, and
>   (c) release-edge count (1 vs N). **hold-to-ramp viable** (ramp on the ~8 Hz still-held stream, stop on
>   release); **multi-tap counting viable** (count releases). [Caveat: terminal edge code is not a reliable
>   short/long classifier — holds ended `04…`, taps `08…` here, but single presses in the button-map varied.]

Session 3 mapped all 6 shifter buttons (one-hot on `0c46be60`) but the **brake-lever buttons** and the
**silent channels `0c46be61` / `0c46beb0`** are untested. Connect to the **SB20 itself**, Stages app
**disconnected**:
```powershell
C:\repos\cauldnz\SB20-power-proxy\code\.venv-win\Scripts\python.exe `
  C:\repos\cauldnz\SB20-power-proxy\code\scripts\06_capture_ble.py `
  --address E4:AA:5A:D6:0E:D4 --subscribe-all --duration 180 `
  --output "C:\repos\cauldnz\SB20-power-proxy\code\findings\captures\SHIFTER-probe-4-$(Get-Date -Format yyyyMMdd-HHmm).jsonl"
```
Narrate each action: **squeeze LEFT brake ×3, RIGHT brake ×3** (pause between), then any other buttons.
Watch whether `0c46be61`, `0c46beb0`, or **FTMS Status `0x2ADA`** ever fires.

**Also — map the hidden buttons 4 & 5 (the key button-budget test).** There are **5 buttons per side**
(4 & 5 are under the bar tape); session 3 only mapped 1/2/3 (bits `0x01/0x02/0x04` L, `0x08/0x10/0x20` R).
The app *config* ties **1≡4** and **2≡5** (can't set them separately), but **if they emit different BLE
bits we can still separate all five.** So press, narrating each precisely: **LEFT button 1, then LEFT
button 4** — same bit (`0x01`) or a new one? Then **LEFT 2, then LEFT 5**. Repeat on the RIGHT. Result
decides the budget: distinct bits → up to **10** usable signals (set a Profile slot to "external" to free
the pair); same bit → 1≡4 are indistinguishable to us and we're back to the 6 we have.

**Then — input-gesture characterization** (grounds the **control-button** gestures, `shifter-erg-control.md`
— the two **3rd** buttons are reserved for Zwift/ESP/menu control and need gestures; erg uses the main
up/down buttons in erg mode). With the same capture running, **narrate *exactly* what you physically do
for each — a single hold and N taps look the same in the frames, so the narration is the ground truth**
(session 3 had an unrecorded "10 separate clicks" misread later as one hold — don't repeat that):
1. **Chord:** press **LEFT-3rd + RIGHT-3rd at the same time** ×3 — does `0c46be60` show one frame with
   **both bits** (`0x0024`) or two separate events? (decides if a "both buttons" chord is usable).
2. **Double-tap:** **RIGHT-3rd quick double-tap** ×3, narrate "double-tap" — is the gap between the two
   `03/04/08` bursts clean enough to detect vs a single press?
3. **Hold vs taps — the multi-shift question** (UNRESOLVED — session 3 couldn't distinguish them). Do both,
   narrating which: (a) **LEFT-3rd HELD ~2 s** ×2 — say "holding now … released"; (b) **LEFT-3rd TAPPED 5×
   fast** — say "five separate taps". Compare: does a single *hold* emit **repeated `03` commits** (the
   bike auto-repeating, at what rate?) plus a long `01` stream, vs the taps' **one `03` each**? This is the
   "hold-to-ramp" question for the erg feature — get it from a clean, narrated capture, not inference.

**✅ Pass:** brake capture lands + the three gestures are recorded. If a brake squeeze fires a char → new
thread; if nothing → brakes aren't on BLE (consistent with the aero-remote hypothesis). Either way, send
the JSONL — the gesture frames decide the erg button-input design.

---

## C · FTMS erg-acceptance — does the SB20 erg off a THIRD-PARTY Set Target Power? ⭐ (~10 min)

> ### ✅ RESULT — PASS (2026-06-21 09:49–09:51) — the SB20 ergs off a third-party Set-Target-Power
> Connected by `--address E4:AA:5A:D6:0E:D4`; Stages app closed + phone Bluetooth off (sole controller).
> Every control-point op ACKed **success**, no `control-not-permitted`:
> - Request-Control `80 00 01` · Start/Resume `80 07 01` · Set-Target-Power ×3 `80 05 01 9600` / `…c800`
>   / `…6400` (150/200/100 W) · Reset/release `80 01 01`.
> - Status `0x2ADA` Target-Power-Changed confirmed each: `08 9600` / `08 c800` / `08 6400`.
> - **Indoor Bike Data power tracked the targets** (decoded with the spec layout — flags `0x00C5` =
>   cadence b2/power b6/avg-pwr b7): 150 W → settled ~150–158; 200 W → ~195–217 (≈205); 100 W → ~80–125
>   (≈100); cadence 82–94 rpm. Overshoot to ~250 W on the initial ramp, then erg clamped — normal.
> - Static reads: **Feature `8a4000000e200000`** → Target-Setting **bit3 Power-Target supported**;
>   **Power Range `0000a00f0100`** = 0–4000 W, 1 W step. DIS: *Stages Cycling* / *SB20* / SN `H0512210105`
>   / FW `1.1` / SW `1.12.4+3792`. GATT also exposes shifter svc `0c46be5f` (chars `be60`/`be61`) +
>   `0c46beaf` (`beb0` notify / `beb1` write-no-resp) — relevant to §B.
>
> **Verdict:** the *shifter-buttons-adjust-erg-watts* feature is **real**, and the spec-built FTMS codec
> (`ftms.py`/`Ftms.h`) is **validated against real frames** — these become the golden source.
> **Bonus (shift-in-erg): ✅ INERT.** Rider tapped **LEFT-up** main shift during the 200 W hold → **no
> resistance change felt; erg held 200 W.** Note: the **SB20 has no on-device display** — the Stages app is
> the only UI and it was disconnected here — so "inert" = no change in *feel/power*, not a screen read; and
> the shifter char `0c46be60` was not subscribed in this FTMS capture, so there's no byte record. Rider
> narration is the ground truth. → **the main up/down shift buttons can be repurposed for erg ± with no app
> Profile change** (`shifter-erg-control.md`).

**The go/no-go for the *shifter-buttons-adjust-erg-watts* feature** (owner ask;
`code/findings/shifter-erg-control.md`). The SB20 is a full FTMS machine — the Stages app drives erg by
writing **Set Target Power** to Control Point `0x2AD9`. **Unknown:** will the SB20 accept that op from
*us* (not the app) and actually hold the target? FTMS machines can refuse a secondary controller
(`control-not-permitted`). One capture settles it; nothing gets built until it passes.

**Setup:** connect to the **SB20 itself** (`E4:AA:5A:D6:0E:D4`), **Stages app DISCONNECTED** from the SB20
(FTMS expects one controller). Put the bike in **erg/target-power mode** if there's a manual way; **pedal
steadily throughout** so the logged power can be seen to track (or not). The capture tool does the erg
handshake itself — Request-Control → Start → Set-Target-Power — and logs the SB20's `0x80` responses.

```powershell
C:\repos\cauldnz\SB20-power-proxy\code\.venv-win\Scripts\python.exe `
  C:\repos\cauldnz\SB20-power-proxy\code\scripts\capture_ftms.py `
  --name SB20 --duration 240 --erg --erg-targets 150,200,100 --erg-hold 25 `
  --output "C:\repos\cauldnz\SB20-power-proxy\code\findings\captures\G-sb20-ftms-erg-$(Get-Date -Format yyyyMMdd-HHmm).jsonl"
```

**✅ Pass:** responses come back `80 00 01` (Request-Control success) and `80 05 01` (Set-Target-Power
success), **and the Indoor Bike Data power tracks the 150/200/100 targets** as you pedal → **the
shifter-erg feature is real**; build it grounded in the captured FTMS frames. **❌ Fail:**
`…05` = `control-not-permitted`, or power ignores the targets → the SB20 won't erg off a third party over
BLE (or needs to be the *sole* controller / bonding); tell me the exact response bytes — that decides the
feature and the alternative-app path. **Commit the JSONL** either way (passive Indoor Bike Data + the
Feature/Power-Range reads are useful regardless). *(Fallback if `--name SB20` doesn't match: use
`--address E4:AA:5A:D6:0E:D4`.)*

**Bonus — shift-in-erg behaviour** (decides the erg button allocation; the feature plans to repurpose the
**main up/down buttons** for erg ± *in erg mode*). While pedalling at a held erg target, **press a main
shift button (LEFT-up) a few times** and watch the SB20/Stages app + the Indoor Bike Data power: does the
gear change **do anything** in erg (resistance/power blip, an on-screen gear number), or is it **inert**
(erg overrides)? **Inert → we can repurpose the shift buttons for erg with no app Profile change; does
something → we'd disable shifting via a Profile in erg.** Note what you see.

---

## D · Calibration grid — Stages↔Assioma (OPTIONAL, ~25 min, only if legs + time) 🟢

**Lowest priority — do the gated items (G1, C) FIRST.** A meter-vs-meter calibration of the SB20's two
meters (native Stages L crank + Favero Assioma). *Why it's worth it:* a **dress-rehearsal of the
Session-5 meter-to-meter fit pipeline** with a *known* delta (we measured ~9 % on the short QUICK-multi
sample), and it tells us whether that delta is **flat** (→ scale+offset) or **power/cadence-dependent**
(→ a grid model). The harness is **built + desk-verified** (`calgrid` workout, see `ride-director.md`).

**Needs:** the **ANT+ stick** on the bike machine (this path is ANT+, not BLE) + your Stages & Assioma
**ANT+ device numbers** + your Stages **FTP**. The Ride Director shows the **Stages** target watts —
chase the number on your phone and **hold each block as steady as you can**.

1. **Run the grid + paired capture** (drives the phone + logs both meters on one clock):
   ```bash
   python code/scripts/ride_web.py --live --workout calgrid --ftp <YOUR_STAGES_FTP> \
       --stages-id <STAGES_ANT_ID> --assioma-id <ASSIOMA_ANT_ID> \
       --output code/findings/captures/CAL-grid-$(date +%Y%m%d-%H%M).jsonl
   ```
   Open the printed URL on your phone, pedal, **press Start**. The grid is a power spine
   (40→110 % FTP @ 90 rpm, ~2 min each) + cadence rows (70 % & 90 % FTP at 60/75/105 rpm) + a 30 s
   coast (stop pedalling — the zero point). ~23 min.
2. **Fit** (commit the capture first — it's the canonical record):
   ```bash
   python code/scripts/09_fit_calibration.py --input code/findings/captures/CAL-grid-<ts>.jsonl \
       --target stages --ref assioma --mode auto \
       --output code/findings/calibration-stages-assioma.json
   ```
3. **Check cadence-dependence** (the C-rows answer this): `python code/scripts/08_analyze_grid.py
   --input code/findings/captures/CAL-grid-<ts>.jsonl --target stages --ref assioma`.
4. **Record** the chosen fit (scale/offset or grid) + residual into the Retro, and whether the residual
   shows cadence structure. (*Optional:* from the dev box you can watch the live Stages−Assioma Δ at
   `http://<bike-host>:8080/api/control/state` and nudge a block longer with `ride_control.py extend`
   if a point hasn't settled.)

---

## 🔁 Restore (before you leave)
Reinsert **both** crank batteries → re-pair the SB20 to **`62144` (L) : `4963` (R)**, **165 mm**, ANT+
offsets **903 / 951**, normal mode → pedal once to confirm the real cranks read.

## Retro (2026-06-21 — see [`sessions/PLAYBOOK.md`](PLAYBOOK.md) §4)
- **Went well:**
  - **Front-loading §C** (the gate) paid off — clean PASS early, then everything after was upside.
  - **"Agent runs the tooling, rider only pedals/presses + narrates"** — smooth; the rider never touched a
    keyboard. Agent launched every capture (background) + tailed the line-buffered JSONL live for real-time
    pass/fail. Both folded into the playbook.
  - **The owner's cross-check instinct** (independent power read) surfaced the session's biggest finding —
    the ~2× SB20-vs-Assioma gap — that the SB20's own number would have completely hidden. Two methods
    agreed (Garmin spot-read + ESP live poll).
  - **ANT+ stick brought up in-session** (~3 min to openant) from a cold WSL with no prior env.
- **Went wrong / slow / confusing (+ root cause):**
  - **Agent shell clock a day + ~6 h off** (sandboxed clock) → first timestamps wrong. Fix: anchor on the
    capture-file `iso_time`s (real-process truth). → playbook.
  - **ESP WiFi died mid-session** (−89 dBm, every HTTP poll timed out) → lost the live Assioma cross-check on
    the first 200 W test. Root cause: **the C3 doesn't roam between APs** (rider moved downstairs). Fix:
    power-cycle to re-associate (rider's own diagnosis). → playbook.
  - **A 420 s shifter capture lapsed during an off-bike discussion** → only idle captured, had to re-run.
    Root cause: launched the capture before the rider was acting. Fix: launch only when ready. → playbook.
  - **ANT+ capture blocked by `[Errno 13]`** (USB perms) — udev rule present but WSL has no systemd to apply
    it, and no passwordless sudo to self-fix. Documented the fix (systemd / udev reload / sudo); pre-stage
    next time. → playbook.
- **Planned vs actual (timestamps):** planned **~55–70 min** (G1·G2·C·B·restore). Actual **~100 min**
  (09:43–11:23) — but **scope changed live**: G1/G2 deferred (board un-flashable, WiFi down), and §C grew
  from a ~10 min gate into a **~45 min owner-driven power-validation investigation** (3 erg runs + ANT+
  bring-up) that wasn't budgeted. §B ran ~33 min as planned. Lesson: the planned gates fit; the *emergent*
  investigation is what blew the estimate — and it was the right call (it found the real issue).
- **Changes folded into the playbook this session** (all live): C3-doesn't-roam→power-cycle; agent-runs-the-
  tooling; line-buffered live-tail + stop-when-done; don't-trust-the-shell-clock (anchor on capture
  `iso_time`); launch-capture-only-when-the-rider-acts; independent-reference-meter-feed pre-flight; the
  ANT+/usbipd/WSL bring-up procedure + the `[Errno 13]` permission gotcha.
- **Next gate + desk work that must precede it:**
  1. **🎯 The power-topology investigation — [`sb20-power-topology.md`](../code/findings/sb20-power-topology.md)**
     (THE headline follow-up): reconcile the owner's Garmin `.FIT` (dense Assioma, ANT+) against the SB20
     erg captures, then design + run a **simultaneous multi-device capture** (SB20-FTMS + Stages 62144/4963
     + Assioma 17039/22428, BLE *and* ANT) to settle single-vs-dual-sided + which meter feeds the SB20.
     **Prereqs:** the Garmin `.FIT` (owner to send); ANT+ permission fix (WSL systemd / udev).
  2. **G1/G2** (the `0x10` work) — still pending; needs the board **USB-flashed** (`flash_c3.py`; OTA was
     dead this session) + the real L-crank awake. **§D** calibration grid — needs the ANT+ path working.
