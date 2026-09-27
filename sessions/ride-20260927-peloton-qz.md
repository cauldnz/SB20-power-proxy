# Ride 2026-09-27 — a Peloton class on the SB20 with qz driving, observed end to end

**Status: ✅ DONE (2026-09-27).**
**Outcome:** the first time anyone here watched the whole qz→Peloton→SB20 path work in anger, and it
**reset the product direction for #342** (companion to qz, not a replacement — see
[`../code/findings/peloton-integration.md`](../code/findings/peloton-integration.md) §0). Captured
43.5 min of a real class at 1 Hz with **zero impact on the rider**, via a qz feed nobody here knew
was reachable on iOS. A head unit ran 44 min on the bike with **no reboot and no heap leak**.

> **This was opportunistic, not planned.** The owner announced a class ~15 minutes before mounting
> up and asked for observability that cost him nothing. The "Plan" below is what was actually
> decided in those 15 minutes, written up afterwards so the next opportunistic session can reuse it.
> Everything in §3 is the retro, and the playbook changes it produced are already folded into
> [`PLAYBOOK.md`](PLAYBOOK.md).

---

## 1. Plan (decided in ~15 minutes, at the desk)

**Constraint set by the rider:** *"I want to focus on my workout… low impact on me"*, no laptop
downstairs, and later *"QZ will be driving the trainer"*. He would carry one board down and talk to
the session over Remote Control.

| # | Step | Why |
|---|---|---|
| P1 | **Make the bench safe** before anything else | Two head units were advertising Stages cranks and the PC was running two `fake_meter.py` CPS advertisers, in a house about to hold a real SB20 pairing |
| P2 | Pick **one** board to go downstairs | CYD: 11 h uptime vs the Guition's unexplained reboot earlier that day |
| P3 | **Neuter the board** before it leaves the bench | It must be incapable of affecting the class |
| P4 | Find an observability path that **does not compete with qz for BLE** | qz owns the bike's connections; anything of ours that subscribes is a risk to the ride |
| P5 | Record from the desk over WiFi; rider does nothing | The rider's time is the budget |

---

## 2. Actual

### 2.1 Making the bench safe — ✅

| Step | Result |
|---|---|
| `POST /ble/off` on the CYD and the Guition | ✅ both `200 BLE off — restarting quiet. WiFi stays up` — the route built in the [bench UI pass](bench-ui-pass.md) earning its keep a second time |
| Kill the two `fake_meter.py` processes | ✅ PIDs 24820, 24636 — both were advertising CPS as `CAULDT9H` from the PC |
| BLE scan from the PC to confirm what was left | ✅ only the FTMS sim (`38:44:BE:45:53:36`, −46 dBm) and unrelated household devices. **The SB20 was not in PC BLE range**, which settled that the board had to go downstairs |

### 2.2 Neutering the CYD — ✅

Three changes over HTTP, no hands on the board:

| Field | Before | After | Why |
|---|---|---|---|
| `trainer` | `SB20-FTMS-Server` | **cleared** | ⚠️ **the safety-critical one.** With a trainer bound and a workout loaded (`4x8 Threshold`), the board could have found the real SB20's FTMS downstairs and driven the rider's resistance mid-class |
| `mode` | `spoof` (`Stages 92364`) | **`corrector`** (`SB20 Corrector`) | removes any chance the SB20 mistakes it for a crank |
| `source_filter` | `CAULDT9H` | left pointing at the now-dead fake meter | matches nothing downstairs ⇒ connects to nothing |
| radio | on | **`/ble/off`** | final belt: in corrector mode it would still *advertise* as a CPS meter, and if qz auto-picks a power source it could have latched on and read 0 W |

Verified on the panel afterwards (owner's photo): `searching…`, `0 W`, in its case on the bars.

### 2.3 The observability path — the finding of the session

**What did not work:** qz's `TcpClient` template (JSON of the whole workout object) — the settings
page is populated from `template_user_ids`, i.e. **user-sideloaded `.qzt` files only**, so on iOS
*Template Settings* is empty. The owner looked and reported it missing; reading the source explained
why. Worth recording as a dead end so nobody sends an iOS user hunting for it again.

**What did work — `OSC Settings`.** A plain accordion in qz's settings, no files needed:

```
qz Settings -> OSC Settings -> OSC IP: <this machine>   (port 9000 default), then restart qz
```

qz enables OSC purely from the IP being non-empty, and sends **one UDP bundle per update**. Sink:
[`../code/scripts/qz_osc_capture.py`](../code/scripts/qz_osc_capture.py) (promoted from a scratch
script in this session, with the decoder unit-tested).

| | |
|---|---|
| capture | `../code/findings/captures/QZ-osc-peloton-20260927.jsonl.gz` |
| volume | **2609 packets over 43.5 min at exactly 1.00 Hz, zero gaps** |
| fields | 41, incl. `/QZ/Watts`, `/QZ/Cadence`, `/QZ/Heart`, `/QZ/Resistance`, `/QZ/PowerZone`, `/QZ/TargetPowerZone` |
| the ride | avg **256 W**, max 621 W, cadence avg 65 / max 102, 944 kcal, 23.1 km, 38:37 elapsed |
| zones (s) | Z1 1141 · Z2 613 · Z3 437 · Z4 238 · Z5 94 · Z6 73 · Z7 13 |

**Two limits, both found on first use — neither is a defect, both must be known before designing
against this feed:**

1. **No Peloton class fields.** `Peloton R(%)`, `T.Power`, `Peloton Offset`, time-remaining are all
   visible on the rider's phone and **none reach OSC** — they are computed inside qz. The feed gives
   the **rider**, not the **prescription**.
2. **1 Hz misses transients.** Logged max **621 W** against qz's own tile reading **746 W**. Verified
   mid-ride by comparing the capture to a photo of the phone: cadence, resistance and elapsed agreed;
   only the peak differed.

### 2.4 Board soak — ✅ and a real answer

`/status` polled at 1 Hz for the whole session (`../code/findings/perf/ride-soak-cyd-20260927.jsonl.gz`):

| | |
|---|---|
| samples | 2644 · **2521 ok, 123 failed (5 %)** — every failure a WiFi dropout, recorded as a row rather than dropping the log |
| WiFi | **−78 dBm** at the bike (below the −72 we treat as the OTA drop zone); median −74, floor −88 |
| **reboots** | **0** |
| heap | 110 KB at boot → settled to **84.6 KB by the first third → flat for the remaining two-thirds** |

**The open "does a head unit survive a whole session" question is answered — for the WiFi/LVGL path.
It is not answered for BLE**, which was off all session by design. Say so rather than claiming a soak
we did not run.

### 2.5 Incidental

- **Resistance max reads 602** in the capture, and the rider's own screen showed `MAX: 603` beside a
  live value of 60. **qz or the bike emits a ~10× spike**; not ours, but anything consuming
  `/QZ/Resistance` needs to expect it.
- The CYD showed the [#358/#364](https://github.com/cauldnz/SB20-power-proxy/pull/365) layout fix in
  a real case on real bars — first sighting outside the bench.

---

## 3. Retro

**What worked.** Refusing to compete for BLE. The first instinct was to point the board at the
SB20's crank and log power; with qz driving, that would have been a genuine risk to the rider's
class for data we got more cleanly another way. Reading qz's source found a path that touches
nothing.

**What nearly went wrong — twice.**

1. **The trainer binding.** The board went downstairs carrying a loaded workout and a bound FTMS
   trainer. Had it kept them and found the real bike, it could have driven the rider's resistance
   mid-class. Caught by reading `/status` before the board left the bench, not by any checklist.
   → **now a pre-flight item in [`PLAYBOOK.md`](PLAYBOOK.md).**
2. **Nearly rebuilding a documented capability.** A Peloton findings doc was written from scratch
   before discovering that [`peloton-integration.md`](../code/findings/peloton-integration.md) —
   with the API research, the data shapes *and* a Phase-0 capture recipe — had existed since
   2026-09-24. The duplicate was deleted and the genuinely new material folded into the existing doc.
   This is the exact failure `PROJECT-MAP.md` and the `nrf-sniffer.md` lesson warn about, repeated
   inside the same repo that documents it. **The index is not optional reading.**

**What cost time.** Nothing the rider paid for — every mistake was on the desk side while he rode.

**Tooling promoted from this session:** `code/scripts/qz_osc_capture.py` (+ unit tests). The
`/status` ride recorder stayed a scratch script: `scripts/perf_soak.py` already samples an ESP32 on
an interval into JSONL, and a near-duplicate was not worth the inventory. **If a second ride wants
it, extend `perf_soak.py` with a dropout-tolerant `/status` mode rather than adding a script.**

**Open, for the owner:** §0.4 of the Peloton doc — what qz actually reads when pedals are the
source, whether the SB20's erg loop is in the picture at all for a Peloton class, and what the
daughter's setup looks like.
