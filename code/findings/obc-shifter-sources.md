# Electronic-shifter spare buttons → OpenBikeControl (OBC) — source-side research

**Status:** **research / no code yet** (feasibility survey, 2026-07-07). Grounds the *input* half of the
OBC feature: which third-party electronic shifters' **spare/custom buttons** we can listen to, over which
transport, and therefore which of our boxes can host it. The *output* half — emitting OBC — is the built,
host-tested [`obc-protocol.md`](obc-protocol.md); this doc is its sibling on the read side, the way
[`shifter-ble-protocol.md`](shifter-ble-protocol.md) is the read side for the SB20's own shifter.
Issue: [`cauldnz/SB20-power-proxy#249`](https://github.com/cauldnz/SB20-power-proxy/issues/249)
("OBC box: hook Shimano / SRAM shifter buttons → translate to OpenBikeControl").

**Clean-room note (applies throughout).** All prior art below is read **to understand the wire format**,
never copied — several projects are unlicensed hacks or have since been taken offline under manufacturer
pressure (see Di2/DiHack). We reimplement a decoder from the *observed bytes* (as we did for the SB20
shifter), exactly as CLAUDE.md's MIT/clean-room invariant requires. Nothing here is built ahead of a real
on-air capture — this is a survey to decide *what to capture and on which box*.

---

## TL;DR — the transport decides the box

The whole feasibility question collapses to one axis: **the spare buttons of every mainstream electronic
groupset are reachable over an ANT link, and mostly NOT over a plain-BLE one a third party can join.** So:

- **ANT (private-ANT or ANT+)** → needs the **nRF52840 XIAO Sense** (BLE **and** ANT via the licensed S340
  SoftDevice — see [`nrf52-sense.md`](nrf52-sense.md)). Our **ESP32-C3 is BLE-only and cannot do this.**
- **A joinable BLE control link** → either box could do it — **but no groupset currently exposes its buttons
  this way to a third party** (the shifter↔derailleur BLE links are bonded/proprietary; see below). So in
  practice **this feature is an nRF/ANT feature.** That matches the issue title's own steer ("ANT for older
  Shimano → nRF unit").

The buttons we want are the **spare/bonus/satellite** ones — the physical shift paddles usually drive the
derailleur directly and their *press* is only inferable from a gear-change event, whereas the **bonus/hood
buttons and Blips/MultiClics** are explicitly designed to be *assigned* to arbitrary actions (incl.
controlling a head unit), which is exactly the OBC use case.

---

## 1. Shimano Di2 (D-Fly / wireless unit)

**Transport: private-ANT (the "D-Fly channels"), plus BLE only to the E-TUBE app.**

- **D-Fly = the telemetry/remote bridge.** The wireless units — **SM-EWW01** (original, **ANT only**, no
  BLE), **EW-WU101/111** (adds BluetoothLE) — broadcast Di2 state (gear position, battery) *and* expose the
  **configurable buttons** to head units. On 12-speed Dura-Ace/Ultegra/105 the wireless unit is built in (no
  separate D-Fly plug-in needed).
  [BetterShifting SM-EWW01](https://bettershifting.com/component/deprecated-ant-wireless-unit-d-fly-sm-eww01/) ·
  [BetterShifting EW-WU111](https://bettershifting.com/component/d-fly-inline-wireless-unit-ew-wu111/) ·
  [Shimano D-Fly news](https://bike.shimano.com/en-US/information/news/d-fly.html)
- **It is *private* ANT, not ANT+.** There is **no ANT+ "shifting" device profile** — the ANT+ alliance
  publishes no gear-shift profile, so Shimano rolls its own network on a private key. Reverse engineers found
  it broadcasts on **2457 MHz** (nominally the ANT+ reserved frequency) using an **NRF24AP2** ANT radio, with
  its own **network key + channel timing**.
  [Titan Lab: Reverse Engineering Di2](https://titanlab.co/reverse-engineering-shimano-di2/) ·
  [Hackaday coverage](https://hackaday.com/2019/03/26/reverse-engineering-shimano-bike-electronics/)
- **The spare buttons: the "hood/top" (bonus) buttons + sprint/climber satellite shifters.** Each can be
  assigned in the **E-TUBE** app to a **"D-Fly Ch.X"**; a paired Garmin/Wahoo then maps that channel to an
  action (scroll data page, lap, zoom, lights on/off). This is the model to emulate — the button press
  arrives on a D-Fly channel and we translate it to OBC.
  [Shimano E-TUBE / D-Fly Ch.X assignment (road.cc)](https://road.cc/content/tech-news/216849-shimano-launches-new-e-tube-app-di2-customisation) ·
  [Garmin: control ANT+ lights via Di2 top buttons (road.cc)](https://road.cc/content/tech-news/205424-garmin-control-ant-lights-dura-ace-di2-top-buttons-plus-strava-beacon) ·
  [BetterShifting: enable the hood buttons](https://bettershifting.com/enable-the-buttons-on-your-di2-shifter-hoods/)
- **Prior art (offline — treat as historical / clean-room only).** `kwakeham/DiHack`
  (github.com/kwakeham/DiHack + wiki) documented the private-ANT key, timing, frequency and gear-data
  decode — **but the repo and its videos were pulled** (community consensus: legal pressure from Shimano), so
  it now **404s**; do not rely on or vendor it. `cmoski/DiHack` is a referenced mirror name. There is also a
  **peer-reviewed security paper** on the *shifter↔derailleur* control link (below).
  [DiHack (now removed)](https://github.com/kwakeham/DiHack) ·
  [Weight Weenies: "Di2 Protocol Hacked?"](https://weightweenies.starbike.com/forum/viewtopic.php?t=158303)
- **Do NOT confuse two different links.** The **shifter↔derailleur** *control* link on wireless 12-speed Di2
  is a **separate proprietary 2.4 GHz link** (not the D-Fly telemetry). The WOOT'24 "MakeShift" paper showed
  it is **unencrypted/unauthenticated and replayable** — but that's the *actuation* channel, not a button
  feed we'd listen to for OBC. For our purpose we want the **D-Fly telemetry/remote channel**, which is the
  right, intended surface for reading button events.
  [MakeShift: Security Analysis of Di2 Wireless Shifting (WOOT'24, PDF)](https://www.earlence.com/assets/papers/makeshift-woot24.pdf)

**Barrier:** the private-ANT **network key** (must be recovered/known to open the channel) + it's ANT, so
**nRF-only**. Battery/pairing: a D-Fly can carry a passkey ("Di2 wireless passkey") on newer units — a
possible pairing gate to verify on hardware.
[BetterShifting: Di2 wireless passkey](https://bettershifting.com/change-di2-wireless-passkey-using-mobile-e-tube-app-or-pc/)

## 2. SRAM eTap / AXS

**Transport: proprietary "AIREA" for shifter↔derailleur (closed); ANT+ *and* BLE for telemetry; and —
critically — newer AXS controllers broadcast their buttons over ANT+ as *controls*.**

- **AIREA is the closed part.** The shifter↔derailleur wireless (SRAM's **AIREA** protocol) runs on its own
  frequency, is **bonded/paired between components**, and its shift commands are **not publicly documented** —
  a third-party listener would have to reverse-engineer it, and it's designed to resist casual sniffing. The
  derailleur uses an **nRF52832** (BLE+ANT capable); the shifter is essentially BLE-class silicon.
  [SRAM: is eTap Bluetooth or ANT+?](https://support.sram.com/hc/en-us/articles/6086267696795-Is-the-SRAM-eTap-wireless-protocol-considered-Bluetooth-or-ANT) ·
  [MTB-News.de: AXS Funkprotokoll shifter↔Schaltwerk (RE thread)](https://www.mtb-news.de/forum/t/sram-axs-funkprotokoll-zwischen-shifter-schaltwerk.915218/)
- **The reachable part — the good news for us: newer AXS *controllers* speak ANT+.** SRAM RED AXS (2024+)
  **Bonus Buttons** (on top of the hoods), **AXS Wireless Blips**, and **MultiClics** broadcast as **ANT+
  controllers**. Garmin (Edge 540/840/1040+) and Wahoo (ELEMNT ACE/BOLT 3/ROAM 3, and older via update) pair
  them and let you **map each button to a computer action** (page scroll, lap, zoom, backlight) — exactly the
  OBC mapping. **No extra hardware** is needed (unlike Di2's D-Fly), and the AXS app enables/assigns them.
  [SRAM: what AXS data is on ANT+/BLE head units](https://support.sram.com/hc/en-us/articles/6225479178395-What-eTap-AXS-system-information-is-made-available-to-be-displayed-on-ANT-or-BLE-compatible-GPS-head-units) ·
  [Wahoo: RED AXS Bonus button + Wireless Blip computer control (ELEMNT)](https://support.wahoofitness.com/hc/en-us/articles/18856512008850-SRAM-RED-AXS-Bonus-button-and-AXS-Wireless-Blip-computer-control-ELEMNT) ·
  [SRAM: AXS buttons explained](https://www.sram.com/en/learn/axs-buttons-explained) ·
  [COROS: button control with SRAM electronic shifting](https://support.coros.com/hc/en-us/articles/30883348200724-Setting-Up-Button-Control-with-SRAM-Electronic-Shifting)
- So SRAM splits neatly: **gear-change *actuation*** = closed AIREA (hard, and not what we want); **spare
  buttons as a *remote*** = **ANT+ controls** = a documented-ish, head-unit-visible surface = our target. It's
  still ANT → **nRF box**. (The "controller advertises over ANT+" is the same shape as an ANT+ remote/lap
  button.)

**Barrier:** none of encryption for the ANT+ controller broadcast itself (it's meant to be paired by any
head unit); the barriers are **(a) it's ANT+ so nRF-only**, and **(b) only the 2024+ Bonus/Blip generation
does the ANT+ controller broadcast** — older eTap AXS exposes gear/battery telemetry but not necessarily the
button-as-remote feed. Confirm on hardware which generation the tester has.

## 3. Briefly — the others

- **Campagnolo EPS (Super Record Wireless / V3 interface).** Wireless EPS carries **ANT+ and BLE** telemetry
  (gear + battery) and Wahoo/Garmin pair it; button-as-remote exposure is not clearly documented and needs
  checking. Same rule: telemetry over ANT+ ⇒ nRF. Low priority (small user base).
  [Campagnolo: Wahoo supports EPS V3](https://www.campagnolo.com/AU/en/CampyWorld/Products/wahoo_supports_campagnolo_eps_v3) ·
  [road.cc: EPS V3 goes wireless](https://road.cc/content/tech-news/170812-campagnolo-eps-v3-electronic-groupset-goes-wireless-sort) ·
  [Titan Lab: Wireless Campagnolo EPS](https://titanlab.co/wireless-campagnolo-eps/)
- **Wahoo** makes head units, not electronic groupsets — it's a *consumer* of these ANT+/BLE controls, useful
  only as a **reference for how a button-remote is expected to behave** (what actions map where).
  [Wahoo: use electronic drivetrains (ELEMNT ACE/BOLT 3/ROAM 3)](https://support.wahoofitness.com/hc/en-us/articles/33770226935698-Use-electronic-drivetrains-with-ELEMNT-ACE-BOLT-3-or-ROAM-3)
- **Our own SB20 shifter** is the already-solved reference case: read over **BLE** (vendor char `0c46be60`,
  fully mapped) → OBC. That path fits **either** box. See [`shifter-ble-protocol.md`](shifter-ble-protocol.md).

## 4. ANT vs ANT+ — and why it picks the box

- **ANT** = the low-level MAC/radio (TDMA, 1 Mbps GFSK, 2.4 GHz). A **private ANT network** uses a
  manufacturer's own **network key**, so only devices holding that key hear each other — this is Shimano
  D-Fly and SRAM AIREA. **ANT+** = the *interoperable* layer on ANT: a **public managed network key** + a
  **published device profile**. See [`nrf52-sense.md`](nrf52-sense.md) §"ANT+ without S340" for the key/legal
  nuances already worked out for this project (the key is an on-air gate, the profile docs are public).
  [thisisant: network keys & the managed network](https://www.thisisant.com/developer/resources/tech-bulletin/network-keys-and-the-ant-managed-network)
- **There is no ANT+ "shifting" profile** — Shimano/SRAM gear telemetry rides on **private** networks or
  vendor extensions. But there **is** an **ANT+ "Controls" (Generic Control) profile** — menu navigation
  (Up/Down/Select/Back/Home) + timer/lap commands, designed to remote-control bike computers. **This is the
  profile the AXS Bonus buttons / Blips speak as ANT+ controllers**, and it is the natural source-side analogue
  of OBC's own action set (see mapping below).
  [thisisant: ANT+ device profiles](https://www.thisisant.com/developer/ant-plus/device-profiles)
- **Therefore, per source → box:**

| Source signal | Transport a listener must speak | Box that can host it |
|---|---|---|
| Shimano Di2 D-Fly buttons/gear | **private ANT** (key needed) | **nRF52840 Sense only** |
| SRAM AXS Bonus/Blip/MultiClic (2024+) | **ANT+ Controls** | **nRF52840 Sense only** |
| SRAM AIREA shift actuation | proprietary (closed, bonded) | *neither, realistically* |
| Campagnolo EPS telemetry | **ANT+ / BLE** | nRF (ANT) or either (if BLE feed exists) |
| **SB20 shifter (ours)** | **BLE** (`0c46be60`) | **either** (already done) |

---

## Feasibility table

| Source | Buttons of interest | Transport | Box | Barriers | Confidence |
|---|---|---|---|---|---|
| **Shimano Di2 D-Fly** | hood/top (bonus) buttons, sprint/climber satellite shifters, gear events | **private ANT** @2457 MHz, NRF24AP2 | **nRF (ANT)** | private **network key** (recover); possible unit passkey; prior art offline (clean-room from bytes) | **Med-High** — heavily documented historically; key + capture are the work |
| **SRAM AXS (2024+ RED)** | **Bonus buttons**, Wireless Blips, MultiClics | **ANT+ Controls** | **nRF (ANT)** | ANT-only; only newest generation broadcasts the controller feed; AXS-app enable | **High** for *newer* AXS (head units already do it); **Low** for older AXS button-as-remote |
| **SRAM AIREA (shift actuation)** | the shift command itself | proprietary bonded RF | neither | closed, paired, RE-hard; not the surface we want | **Low** — deprioritise |
| **Campagnolo EPS V3 / SRW** | gear/battery; button-remote unclear | **ANT+ + BLE** telemetry | nRF (ANT) / maybe either | button-as-remote not clearly exposed; tiny user base | **Low-Med**, unverified |
| **SB20 shifter (baseline)** | all 6 pods (mapped) | **BLE** `0c46be60` | **either** | none new | **Done** (read side mapped) |

---

## OBC mapping (source button → OBC action ID)

Target IDs are the constants in [`firmware/lib/proxy/Obc.h`](../../firmware/lib/proxy/Obc.h) (documented in
[`obc-protocol.md`](obc-protocol.md)). OBC allows **multiple action IDs per physical press**, so a button can
fire both a "shift" and a "nav"/"erg" action and work across apps without a mode switch — the same trick the
SB20 map uses.

| Physical button | Sensible OBC action(s) | Rationale |
|---|---|---|
| Di2 **hood/top-left** (bonus) | `0x10` Nav Up **+** `0x30` ERG Up | matches how head units use it (page scroll) + our erg-nudge default |
| Di2 **hood/top-right** (bonus) | `0x11` Nav Down **+** `0x31` ERG Down | symmetric |
| Di2 **sprint shifter up / down** | `0x01` Shift Up / `0x02` Shift Down | they *are* shift paddles → virtual shift |
| Di2 **climber / 3rd shifter** | `0x35` Lap (or `0x16` Menu) | spare → the discrete-action slot, mirrors SB20-3rd |
| SRAM **Bonus button** (per side) | `0x10`/`0x11` Nav **+** `0x35` Lap | ANT+ Controls already carries menu/lap semantics |
| SRAM **Blip / MultiClic** (n-way) | map each position → `0x10-0x13` Nav / `0x14` Select / `0x15` Back | Generic-Control nav set is a 1:1 fit |
| SRAM **shift paddles** | `0x01`/`0x02` Shift Up/Down | virtual shifting |

(These are *proposed defaults* to validate against a real head-unit's behaviour and the OBC reference apps,
not measured yet.)

---

## Open questions / gates (what we must capture or verify on hardware)

1. **Get on the private-ANT D-Fly channel.** Recover/confirm the Shimano D-Fly **network key + channel period
   + RF frequency** and decode a **button-press data page** (distinct from the gear-position page) — from a
   fresh capture on our own gear, not the offline DiHack repo. Gate: do we have a Di2 bike + a D-Fly unit to
   capture? (nRF ANT-sniff or a licensed ANT stick.)
2. **AXS controller generation.** Confirm which tester hardware is **2024+ RED (Bonus/Blip broadcasting ANT+
   Controls)** vs older AXS. Capture the ANT+ Controls pages a Bonus button emits and confirm they parse as
   Generic Control (menu/lap). Gate: access to a 2024+ AXS controller.
3. **nRF ANT bring-up is a prerequisite for BOTH.** This feature can't ship until the **S340/ANT path on the
   XIAO Sense is live** — see [`nrf52-sense.md`](nrf52-sense.md) §"ANT: the licensed path (S340)" (still in
   bring-up). Until then only the **BLE** sources (our SB20 shifter) are hostable, on either box.
4. **Pairing/passkey gates.** Does a Di2 unit require its wireless passkey before it will admit our listener?
   Does an AXS controller need to be *bonded* to a head unit, or does it broadcast openly once enabled? Verify
   before promising "plug-and-play".
5. **Which OBC IDs match real app expectations.** Validate the mapping table against OBC reference consumers
   (`obc-protocol.md` §Bench) and, ideally, how Garmin/Wahoo interpret the same buttons.

## Clean-room notes

- **Read to understand, never copy.** DiHack (offline), the MTB-News AXS thread, Titan Lab, and the WOOT'24
  paper are references for *what the bytes mean*; we reimplement a decoder from our **own capture** (the SB20
  shifter precedent). Some prior art is unlicensed or was taken down under manufacturer pressure — do not
  vendor it.
- **The ANT+ managed network key & the ANT+ trademark are gated; the protocol docs are public.** Same posture
  already established in [`nrf52-sense.md`](nrf52-sense.md): clean-room *implementation* against the public
  ANT Message Protocol + ANT+ Controls profile is fine; the **private** Shimano/SRAM keys are the RE target,
  and we must not redistribute a licensed key or SoftDevice.
- **Capture before code** (CLAUDE.md invariant): no Di2/AXS decoder is written before the on-air capture that
  grounds it. This doc is the survey that says *what to capture and on which box* — not an implementation.
