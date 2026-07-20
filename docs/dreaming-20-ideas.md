# 💭 Dreaming Session — 20 Ways to Extend the SB20 Power Proxy

*A pitch deck of where this could go next. Each idea says what it does, what it leans on
(so it's an extension, not a rebuild), and a rough effort. Dream first, prioritise later.*

---

## What we're standing on (the launchpad)

These make most of the ideas below *cheap*, because the hard parts already exist:

- **A pure, host-tested core + renderer.** Game logic, CPS/ANT codecs, correction, and the
  `LcdCanvas` UI all run and are tested on the desk with no board. New features get unit-tested
  and *seen* (montage/screenshot) before hardware.
- **A working ANT+ Bike Power master** (S340, on the nRF) — proven transmitting on air, received
  by a Garmin stick, and the **full BLE-meter → correct → ANT-out loop** is proven end to end.
- **FTMS control of the SB20** — `Set Target Power` (erg) is confirmed accepted; resistance/SIM
  are one small codec add away (verify pending).
- **The rider is a 2-axis analog controller** — power + cadence, plus free derived signals
  (smoothness/variance, surge/dₜ, steadiness/time-in-band) *and* the decoded SB20 shifter buttons.
- **Two head-units** (CYD 240×320 + S3-Touch 172×320) sharing one renderer, a **web SPA**, a
  **Garmin Connect IQ** app, and a **Watty Birds** game engine already running on hardware.
- **A real-data pipeline** — on-bike captures → codecs/fixtures → fit → deploy, with JSONL as the
  lossless record.

**Effort legend:** 🟢 XS (hours) · 🟢 S (~1 day) · 🟡 M (2–5 days) · 🟠 L (1–2 weeks) ·
🔴 XL (multi-week / research-gated).

---

## 🎮 A. Games & Play — *the power-as-controller vein*

**1. Watty Birds v2** — *make the Easter egg a keeper.*
Leaderboard (best scores in NVS), a difficulty ramp, and the **secret pedal-pattern unlock** (three
sharp surges) so it's a true hidden egg, not a demo flag. Bonus: **haptic hills** — a tall pipe
briefly bumps the SB20's grade so climbing it is physically harder.
*Leverages:* the game + FTMS. **Effort: 🟡 M** (the haptic bit needs FTMS SIM verified).

**2. Watt Lander** — *feather a steady power to land gently.*
A second game in the same engine: descend onto a pad, too much throttle you shoot up, too little you
crater. It's a sweet-spot/FTP smoothness drill in disguise, and the "gentle landing" dopamine is real.
*Leverages:* the WattyBird engine (physics/render/host-sim all reusable). **Effort: 🟢 S.**

**3. Sprint Duel** — *a 15-second drag race vs your ghost.*
Your dragster's speed = your power; the opponent is your previous best (or an AI pacer). Screen-shake,
a saved leaderboard, pure max-effort dopamine.
*Leverages:* the engine + NVS records. **Effort: 🟡 M.**

**4. The Arcade** — *a home for all the games.*
A game-select screen + an "arcade cabinet" mode that auto-launches a game during warmup/cooldown.
Turns one Easter egg into a little platform.
*Leverages:* the shared UI + the game(s). **Effort: 🟢 S.**

---

## 🏋️ B. Training & Coaching — *the games were always drills*

**5. Virtual Terrain** — *ride a route; the bike gets heavier on the climbs.*
Drive the SB20's resistance from a grade/route profile via FTMS `Set Sim Params`, so a 6% wall
actually feels like one. The foundation for haptic everything.
*Leverages:* FTMS control (needs a 5-min capture to confirm the SB20 honours SIM/resistance, not just
erg). **Effort: 🟡 M.**

**6. Smoothness & Balance Coach** — *live feedback on how you pedal, not just how hard.*
A head-unit strip showing pedaling smoothness (power variance) and L/R balance in real time — the
"derived signals" we get for free, plus the Assioma balance we already decode.
*Leverages:* `RideView` + balance decode. **Effort: 🟢 S–🟡 M.**

**7. Adaptive Workouts** — *the session meets you where you are.*
Workouts that nudge targets to your day based on recent power (a rough readiness read), instead of a
fixed prescription.
*Leverages:* `WorkoutRuntime` + calibration history. **Effort: 🟡 M.**

**8. One-Tap Calibrate + Drift Watch** — *calibration that just happens.*
Collapse the calibration wizard to a single tap, and passively flag when a meter drifts from its saved
profile ("your XCadey reads 4% low today").
*Leverages:* `CalibrationSession` + the correction profile. **Effort: 🟢 S–🟡 M.**

---

## 📊 C. Data & Records — *nothing rides off without a trace*

**9. On-device Ride Recording → .FIT export** — *turn a session into a Strava upload.*
Record power/cadence/balance to flash and export a standard **.FIT** file over the web app or USB —
Garmin/Strava-ready, no head unit needed.
*Leverages:* the IMU capture buffer pattern (already records + downloads). **Effort: 🟡 M.**

**10. Live A/B Meter Compare** — *watch two meters disagree in real time.*
A dual-meter live dashboard (Assioma vs SB20 vs XCadey) on the head-unit and web — the paired-capture
work, surfaced live. Great for trust + calibration.
*Leverages:* the multi-central paired capture. **Effort: 🟢 S–🟡 M.**

**11. Ride Replay / Ghost** — *race yourself from last Tuesday.*
Replay a recorded ride as a ghost to pace or race against — in a game, or as a target-power overlay.
*Leverages:* recording (#9) + the game engine. **Effort: 🟡 M.**

**12. The "Black Box"** — *always-on rolling capture of the last N minutes.*
A circular buffer of power + BLE/ANT events, dumped on demand — so when a ride does something weird
(dropout, spike), you have the tape.
*Leverages:* the capture discipline + JSONL. **Effort: 🟢 S–🟡 M.**

---

## 🔌 D. Platform & Hardware — *lean into what only this can do*

**13. ANT+ Stages Spoof (native link)** — *the most faithful impersonation.*
Feed the SB20 over its **internal ANT+ crank link** (not just BLE), broadcasting Bike Power pages
*as the Stages crank* + answering the calibration/zero request. Potentially the cleanest spoof of all.
*Leverages:* the working S340 ANT master + `AntBikePower` codec (encoders already exist). **Effort: 🟡 M**
(on-air test vs a real SB20 is the unknown).

**14. Electronic-Shifter Bridge** — *bring SRAM AXS / Di2 shifts into the game (and Zwift).*
Read wireless shifters over ANT+ and map them to OBC / Zwift-Click — an input axis *only the nRF* can
add. Shift to change gear in Virtual Terrain, or as game buttons.
*Leverages:* the ANT stack + the OBC shifter work. **Effort: 🟠 L** (needs a shifter ANT capture first).

**15. Multi-Sensor Hub** — *one clean profile out of a messy sensor bag.*
Fold HR + speed + power into a single, well-behaved re-broadcast, so a head unit sees one tidy device
instead of three flaky ones.
*Leverages:* the dual-role BLE/ANT plumbing. **Effort: 🟡 M.**

**16. The Correcting Bridge (productised)** — *any meter, corrected, under our own name.*
The meter-to-meter path as a tiny shippable product: read e.g. an XCadey, re-broadcast on the Assioma
scale under our own honest identity — zero spoofing, any head unit accepts it. This is arguably the
most *broadly useful* thing here.
*Leverages:* corrector mode (already built). **Effort: 🟢 S–🟡 M** (mostly packaging + a clean setup UX).

---

## ✨ E. Immersion & Novel — *make it feel like more than a number*

**17. Haptic Games** — *the resistance fights back.*
Games where the SB20 physically reacts — a hill in Watty Birds, a "wall" in Sprint Duel — via FTMS
SIM. The bike becomes a force-feedback controller. No $20-display game does this.
*Leverages:* the games + FTMS (#5's foundation). **Effort: 🟡 M.**

**18. Sonification / Engine Mode** — *power you can hear.*
Map watts to an engine/turbine sound that revs with effort (on a companion device or a buzzer/speaker).
Weirdly motivating, and a genuinely different feedback channel.
*Leverages:* the live power stream. **Effort: 🟢 S–🟡 M.**

**19. Web Arcade** — *play Watty Birds in a browser.*
The renderer is pure and portable — reimplement the blit in a `<canvas>` and drive it over Web
Bluetooth using the *same game core*. Share a link; play from any laptop connected to the meter.
*Leverages:* the pure engine + the web SPA + `BleTransport`. **Effort: 🟡 M.**

---

## 🌍 F. Reach & Community

**20. Party Mode (multiplayer)** — *head-to-head Watty Birds.*
Two head-units (or a phone + a head-unit) race live over BLE/ANT/WiFi — a shared course, two birds,
one winner. Or a sprint race. The social hook that makes people show up.
*Leverages:* the games + the radios + WiFi. **Effort: 🟠 L.**

---

## ⭐ Top picks — where I'd start

A blend of *quick wins that ship* and *one strategic bet*:

1. **#2 Watt Lander** (🟢 S) — a second game for almost free; proves the engine is a platform.
2. **#16 The Correcting Bridge** (🟢 S–M) — the most broadly *useful* thing; turns a project into a
   tool people want.
3. **#9 Ride Recording → .FIT** (🟡 M) — unlocks Strava/Garmin and makes every ride count.
4. **#5 Virtual Terrain** (🟡 M) — the keystone for all haptic/immersion ideas; do the FTMS-SIM
   verify first and a whole branch of the tree opens.
5. **#1 Watty Birds v2 + #13 ANT Stages spoof** — one for delight, one for the core mission.

**Suggested arc:** a quick win (#2) to build momentum → the FTMS-SIM verify that unblocks the immersion
branch (#5) → the genuinely useful #16 → then pick a strategic bet (#13 for the mission, or #20 for the
fun). Each is a small branch → PR → merge, the way everything else has landed.

---

## 🚀 Moonshots (bonus, for the truly ambitious)

- **A "Trainer OS"** — the head-unit as an open, hackable trainer brain: games, workouts, terrain,
  recording, sensor-hub, all first-class. The 20 above are its app catalogue.
- **A tiny games SDK** — the pure engine + host-sim + montage tooling, packaged so *anyone* can write
  a power-driven game and see it before touching hardware.
- **Structured-workout marketplace over the web SPA** — share/import workouts + games as small files.

*Grounded in what already runs. Pick one, and it's a weekend, not a rebuild.*
