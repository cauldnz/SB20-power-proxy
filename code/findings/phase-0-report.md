# Phase 0 Report — SB20 Power Proxy

**Status: Phase 0 substantially COMPLETE. The proxy approach is validated and de-risked.**
*Last updated 2026-06-15. This report is the single current source of truth; it synthesises
the append-only `decisions.md` log (read that for chronology/evidence, this for conclusions).*

> One-line verdict: **It will work, and the simple way.** The SB20 consumes crank power
> as-is (no rescaling), we have captured the exact bytes a crank must emit (including the
> calibration handshake), and feeding live Assioma watts will make erg targets land on true
> Assioma power. No calibration model is required for the core goal.

---

## 1. What's PROVEN (the de-risking)

| # | Finding | Status | Evidence |
|---|---|---|---|
| 1 | **The SB20 does NOT internally rescale crank power** — it's pass-through. | ✅ CONFIRMED | `bike_FEC(105) / crank(62144) = 0.997` over 149 s (`QUICK-multi-20260615`). So erg target X → you produce X *Assioma* watts when we feed Assioma. **The biggest risk is closed.** |
| 2 | **The calibration handshake is capturable AND we have the exact response.** | ✅ CONFIRMED | C-0 PASS (`C0-ack-dryrun-20260614-164426`): crank broadcasts page **0x01, `kind="broadcast"`**, `cal_id=0xAC`, offsets **903 / −950** matching the app. |
| 3 | **Stages crank manufacturer_id = 69 (0x45).** | ✅ CONFIRMED | Page 0x50 on air (Sessions A). The H2 "smoking gun" — entering Assioma IDs failed because the bike expects the Stages identity/contract, not just any ID. |
| 4 | **Single-source mapping:** read ONE channel, spoof ONE master. | ✅ CONFIRMED | Assioma runs **Unified-channel-L** (L pedal 17039 sends combined L+R); Stages L crank 62144 likewise combines+rebroadcasts. Right side never needs touching. |
| 5 | **The Stages↔Assioma discrepancy is torque/cadence-shaped, not constant.** | ✅ CONFIRMED | ~13% high @ 60 rpm vs ~5% @ 100 rpm at fixed power (P=τ·ω slope error). *Diagnostic only — the proxy eliminates it, see §3.* |
| 6 | **BLE calibration needs NO bonding** on either meter. | ✅ CONFIRMED | Zero-reset over BLE succeeded with no pairing: crank `200c010000`, Assioma `200c01ffff`. Simplifies the ESP32 path. |
| 7 | **The Stages crank is reachable over BLE in ANT+ mode** (advertises `Stages 62144`, full CPS). | ✅ CONFIRMED | `G-crank62144-ble-20260615` (target by address, not the generic "Stages" name). |
| 8 | **All Stages devices + the Assioma run on Nordic nRF** (expose DFU 0xfe59). | ✅ CONFIRMED | Relevant to the ESP32/BLE endgame. |

---

## 2. The SPOOF SPEC (what the proxy must emit)

This is the protocol contract to reproduce — ports across languages (Python/Pi today, ESP32/C later).

### ANT+ Bike Power master (primary path)
- **Network key:** `[0xB9,0xA5,0x21,0xFB,0xBD,0x72,0xC3,0x45]` · **RF freq:** 57 (2457 MHz) · **Channel period:** 8182 (~4 Hz) · **Device type:** 0x0B (11) · **Transmission type:** 5 (master) · **Device number (ANT+ ID):** 62144 (the Stages left crank).
- **Pages to emit** (observed mix, per ~4 s window): **0x10** Power-Only, **0x12** Crank Torque (dominant), **0x13** Torque Effectiveness/Pedal Smoothness, plus a **commons burst ~every 30 s**: **0x50** (manufacturer_id **69**, model 3, hw_rev 3), **0x51** (sw 1.8.2, serial 11821518), **0x52** battery.
- **0x10 Power-Only layout:** byte0 page; b1 event count; b2 pedal balance; b3 cadence; b4-5 accumulated power LE; b6-7 instantaneous power LE.
- **Calibration response (when the bike sends a manual-zero request):** broadcast page **`01 AC FF FF FF FF <offset_sint16_LE>`** (cal_id 0xAC = success). A sane fixed offset (e.g. the captured 903) satisfies it.
- **ID-matching alone is insufficient** (proven: entering Assioma IDs in the app failed). The full contract above is required.

### BLE Cycling Power peripheral (ESP32 path — gated on Session G)
- Advertise as `Stages 62144`, services: GAP/GATT, **0x1818 CPS**, 0x180a DIS (Stages Cycling / SPM2 / serial 11821518), 0x180f battery, Stages custom **fe01** (d445fe02 write+notify, d445fe03 notify), Nordic DFU 0xfe59.
- **CPS Measurement (0x2A63):** flags **0x2F** (balance + accumulated torque + crank revs). CPS Feature 525067.
- **Control Point (0x2A66):** answer Start Offset Compensation with `20 0C 01 <offset_sint16_LE>` (success); **no bonding required**. (Crank-length read uses a non-standard framing — value in trailing 2 bytes.)

---

## 3. Architecture decision (settled)

**Feed live Assioma watts directly into the SB20's crank input; no calibration model.**
- The proxy *replaces* the power the erg loop reads with Assioma watts. The loop closes on that number by construction → erg target = Assioma watts. The Stages cranks leave the loop entirely.
- The torque-shaped Stages↔Assioma offset (§1.5) is the **diagnosis** of why the cranks-in-loop setup mis-trains (and why the owner's manual hacks — dual-FTP and the deliberate 165 mm crank-length fudge — only "roughly" worked). The proxy **eliminates** it; it does not need to model it.
- Source side: only the **Favero/Assioma crank-length** must stay correct (172.5). The SB20-app crank length becomes moot once spoofing.
- The power×cadence calibration grid is now **research/optional**, not on the delivery path.

---

## 4. Device identities (the inventory)

| Role | ANT+ ID | BLE name | Model / serial / fw | Battery |
|---|---|---|---|---|
| **Stages L crank** (spoof target) | **62144** | `Stages 62144` | SPM2 / 11821518 / fw 1.8.2 · mfr_id **69** | **14% ⚠ replace CR2032** |
| Stages R crank | 4963 | `Stages 4963` | serial 20421194 / fw 1.8.2 | — |
| **SB20 bike** | FE-C **105** | `Stages Bike 0105` | SB20 / H0512210105 / sw 1.12.4+3792 | — |
| **Assioma L pedal** (input) | **17039** | `ASSIOMA17039L` | Assioma / 17039.013.118 / fw 06.24 | 73% |
| Assioma R pedal | 22428 | `ASSIOMA22428R` | 22428.113.119 | — |
| Assioma spare pair (L) | 29064 | — | — | — |

Crank lengths: physical holes **172.5** · Assioma (Favero app) **172.5** · SB20 app **172.5** · StagesPower in-meter **165** (06-14) / BLE-read **172.5** (06-15) — see §5 open item.

---

## 5. What's OPEN

| Open item | Why it matters | How to close |
|---|---|---|
| **Crank-length authority** — does the in-meter 165 or the bike-app 172.5 drive consumed watts? | Explains the measurement history (whether 1.085→1.13 is the length fix or something else). **Moot for the proxy** (crank leaves the loop). | 5-min experiment: change one length, capture crank 0x10 power before/after, watch if watts move. |
| **Session G Part C — erg-works on BLE-paired cranks** | The go/no-go GATE for the entire ESP32/BLE direction. | Flip the bike app's "Pair with Bluetooth" ON, set an erg target, confirm the bike holds power. No special kit. |
| **Session G Part B — bike's BLE pairing/bonding/calibration-write** | Needed to *build* the BLE impersonator. | ESP32 impersonation firmware (`raedian-probe#1`) or a replacement nRF dongle (current one is MIA). |
| **ANT+ vs BLE offset semantics** (903 vs ~0) | For the digital twin's fidelity. | Confirm against the CPS spec; capture both during one zero-reset. |
| **"External"/erg ride mode without a crank spoof?** | If the SB20 accepts external resistance control without needing a valid crank, it could be a far simpler path. | Test the app's External/Power-Erg setup mode behaviour. |
| **Does the SB20 BLE pair accept any CPS peripheral or only Stages-branded?** | Decides how exact the BLE impersonation must be. | Hardware test once Part C passes. |

---

## 6. Next-steps plan

> **The detailed, operational version of this section now lives in
> [`forward-plan.md`](forward-plan.md)** (desk/bike lanes, the Phase 1A build order with a
> hardware-verified openant master path, dependency graph, and a take-to-the-bike card at
> [`../../NEXT-BIKE-SESSION.md`](../../NEXT-BIKE-SESSION.md)). The summary below remains the
> at-a-glance map.

Phase 0 has answered the feasibility questions; the work now forks into a build track and a
cheap on-bike track that can run in parallel.

### Track A — Phase 1: static replay (the next engineering milestone, recommended)
Prove the SB20 *accepts a spoofed crank*, independent of any live Assioma input.
1. Build the **ANT+ master/TX** side (new territory — all captures so far were RX/slave). Reference: `dhague/vpower`. Re-broadcast a *captured* Stages stream (`A-stagesL-steady-20260614`) on ANT+ ID 62144 with the §2 spec, including a fixed 0xAC calibration response.
2. With the real cranks' batteries out (or unpaired), pair the SB20 to the replayed master; confirm it displays power and **erg reacts** to the replayed numbers.
3. This is the keystone proof for the whole project (ANT+ *and* BLE) — if the SB20 accepts our replay, impersonation works.
→ Then **Phase 2 (live proxy):** swap the replay source for `07_capture_multi.py`'s live Assioma-L (17039) listen → emit as 62144. Latency target <250 ms.

### Track B — opportunistic on-bike (cheap, next time at the bike)
- **Fresh CR2032 in the crank** (it's at 14%).
- **Crank-length experiment** (§5) — 5 min, resolves the measurement history.
- **Session G Part C erg-on-BLE gate** (§5) — the ESP32-direction go/no-go.
- (Optional) the steady-hold power×cadence grid for a clean calibration curve — research only.

### Track C — ESP32 / BLE / twins (gated on Track B Part C passing)
- The **impersonation firmware** (`raedian-probe#1`) — reuses the `esp32_bridge_spec.md` NimBLE+OTA+OLED scaffold; it's both the Part B capture tool and the proxy's BLE peripheral.
- **Digital twins** built from the `G-*` BLE captures (advert+GATT+CPS+calibration already in hand) to test the proxy without riding.

### Track D — documentation tidy (small, see §7)
Refresh the stale front-door docs to point here.

---

## 7. Known doc-debt (tidied where high-value; the rest flagged)

The capture/analysis tooling and this report are current. A few front-door docs still describe a
*pre-capture* project and should be read with that in mind until refreshed:
- **README** status line updated to point here; **HANDOFF** carries a "Phase 0 largely done — read this report + decisions.md" banner.
- **Append-only `decisions.md` self-corrections** (the latest entry always wins): `Stages 4963` is the **right crank** (early "= bike CPS" is stale); the crank **IS** BLE-reachable in ANT+ mode; the "crank-length prediction confirmed" was **retracted** (now open, §5); the 165 mm setting was a **deliberate** gain hack. When in doubt, trust this report and the newest `decisions.md` entry over older ones.
- A `captures/README.md` index now lists the real capture files (names had drifted; `G-stagesL-ble-recon` is actually the **bike** FTMS device, not the L crank).
- **Cleanup completed (Rev 13):** status banners added to `01`/`02`/`03`/`04`/`05`/
  `CLAUDE-CODE-PROMPT` pointing here; `01`/`04` open-question lists annotated with their
  answers; `code/README` status + script list corrected; `09` mentions 07/08/06;
  `findings/README` points here; `RIDE-CARD` flags the agent-driven default; and
  `src/sb20proxy/cli.py` added so the declared `sb20proxy` command resolves (real CLI in
  Phase 1). Remaining known stub: `src/sb20proxy/` is otherwise skeletal — built out in Phase 1.
