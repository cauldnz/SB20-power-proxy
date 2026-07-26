# 08 — Risks and Gotchas

> ⛔ **SUPERSEDED — historical.** Part of the **pre-pivot brief**, written before the on-bike captures
> and before the firmware existed. Kept for provenance. For the current state read
> **[`PROJECT-MAP.md`](PROJECT-MAP.md)** (what already exists) and
> **[`code/findings/decisions.md`](code/findings/decisions.md)** (what was decided and measured).
> Where this doc disagrees with those, **they win.**

A living list. Append as you discover more. The idea is to bank lessons so that future-you (or another implementer) doesn't repeat mistakes already made.

## Likely-to-bite gotchas

### ID collisions

If a real Stages crank and your spoof are both broadcasting on the same ANT+ device ID at the same time, the SB20 will see corrupted/conflicting data and behave unpredictably. The mitigation is to physically remove the batteries from real cranks during spoof testing — not just "the app said it was unpaired." App-level unpair does not stop the crank from broadcasting; it stops the bike from listening on that ID. The crank continues to broadcast for any other listener.

Use a different ANT+ ID for the spoof during early testing — pick something memorable like 99999 — so that even if a real crank is broadcasting, it's on a different channel from your spoof and you can be confident which packets are which in captures.

### Channel ID collision the other way

If you accidentally choose a spoof ID equal to the user's real Stages crank ID, and the SB20 has been re-paired to your spoof, but then you also accidentally reinsert the battery in the real crank, you have two devices broadcasting on the same channel ID simultaneously. ANT+ does not have collision detection in the way wired Ethernet does; the bike just sees garbled or interleaved messages. Easy to set up; very confusing to debug. Prevention: always use a distinctly different spoof ID, never re-use the real one.

### `openant` rate limiting at higher Hz

The default openant `_worker` and `_main` queue handling adds sleeps that drop messages above ~15 Hz. For the standard 4 Hz Bike Power profile this isn't a problem. If Phase 0 reveals Stages cranks broadcast at higher rates (e.g. 8 Hz), you'll need to apply the patch from https://www.thisisant.com/forum/viewthread/7284 (essentially: comment out the 100 ms sleep on line ~167 of `ant/base/ant.py` and minimise queue dwell). Don't apply this preemptively — only if you actually see drop-outs at high rates.

### Extended messages aren't on by default

Stock openant slave channels don't request extended messages from the stick, so you don't see channel ID or RSSI metadata on incoming packets. To know which device sent which packet (essential when multiple devices are nearby), you must explicitly enable extended messages by sending the 0x66 command (Enable Extended Messages) before opening channels. The pirower fork of openant has this built in; otherwise implement it via openant's lower-level message-send API.

### CR2032 battery contact tabs

Removing CR2032 cells repeatedly from the Stages cranks risks bending or breaking the contact tabs. Be gentle. If you bend a tab, use a tiny screwdriver to ease it back into shape. A broken tab is a real failure that makes the crank unusable until you can rework it.

### Latency in the Assioma → proxy → SB20 chain

End-to-end latency is at minimum one full master broadcast period at the spoof side. At 4 Hz that's 250 ms worst case. Plus Assioma's own 4 Hz period. Plus USB latency. Plus any processing. Realistic best case: 100–150 ms. Realistic worst case: 400+ ms in degraded conditions.

This may or may not affect "feel" of erg mode. Phase 2 testing answers that. Mitigations if needed:
- Run the spoof at 8 Hz (channel period 4091)
- Subscribe to Assioma's higher-rate "fast mode" if it has one (some power meters do; whether Assioma does is a Phase 0/1 question)
- Don't add any smoothing / averaging in the proxy — pass through immediately

### USB bus flakiness on the Pi

ANT+ sticks plus other USB peripherals on a Pi can lead to occasional disconnects, especially under power-supply stress. Use a quality 3A+ Pi PSU (or the official Pi 5 5A USB-C supply). Consider a powered USB hub if running multiple sticks. Watch `dmesg` for USB resets.

Under-volting on the Pi is the single most common cause of ANT stick weirdness. The parent research is emphatic on this point — if you see intermittent stick drops or BlueZ stalls, suspect the PSU before you suspect the code.

### Wi-Fi interference on 2.4 GHz

Both ANT+ and BLE share the 2.4 GHz band with Wi-Fi. On a Pi running both:

- A Pi heavily uploading/downloading over 2.4 GHz Wi-Fi will produce real BLE/ANT+ dropouts during pairing and capture.
- The parent research's recommendation: **for production, use wired Ethernet and disable 2.4 GHz Wi-Fi**, or force Wi-Fi to 5 GHz only.
- For development on a laptop near other 2.4 GHz devices (microwaves, cordless phones, lots of Wi-Fi APs), expect the occasional weird capture. Check if turning Wi-Fi off cleans things up before assuming there's a code bug.

### Pi Bluetooth firmware update needed

Per QZ's installation docs (and confirmed across many Pi-based BLE projects): **update the Pi's Bluetooth firmware before doing serious work**. A fresh `apt update && apt full-upgrade` typically pulls in the firmware updates that BlueZ depends on. Older Pi OS images ship with a Bluetooth firmware that has known issues with sustained connections, advertising, and concurrent ANT+ USB traffic. If your Pi-side BLE is misbehaving and the laptop with the same code works fine, this is the first thing to try.

### macOS USB permissions

Even after `brew install libusb`, macOS may refuse USB access without `sudo`. Workarounds vary by macOS version; expect to spend an hour solving permissions if you go this route. If you don't strictly need macOS, develop on Linux.

### Module versus package import confusion

openant historically used `import ant` (the legacy package name); newer versions use `import openant`. Check the version (`pip show openant`) and use the version-appropriate import. Mixing the two will cause subtle bugs.

### SB20 firmware version variance

Stages firmware 3.7.0+ moved workout scaling out of the bike firmware into the Stages Cycling app. We don't know yet whether other 3.7.0+ changes affect the crank-pairing protocol. Older bikes on pre-3.7.0 firmware may behave differently from newer ones in ways that aren't documented. Record the firmware version (visible in the Stages app) on every Phase 0 capture, and if results from another SB20 owner ever look different, the version is the first thing to ask about.

## Conceptual gotchas

### "Pairing" means different things at different layers

In ANT+ protocol terms, a "channel" is established when a slave finds a master matching its device ID/type/transmission type. There's no real "pairing" — just channels.

In the Stages app's UI, "pairing" is the multi-step user-facing flow: enter ID, wait for the bike to find it, perform zero-reset.

In the SB20 firmware, "paired" likely means a persistent channel ID is stored, and the bike will continue to attempt to attach on that ID across power cycles.

When debugging, be precise about which "pairing" you mean. A capture showing "channel established" doesn't mean the Stages app's pairing flow has completed.

### "It worked before" is a debugging trap

If something worked yesterday and doesn't today, plausible causes in priority order:
1. A battery is low (Stages crank, Assioma, or remote)
2. Another nearby ANT+ device is interfering on the same channel ID
3. The SB20's stored pairing got cleared by an app update or a bike power cycle
4. Your code changed in a way you forgot about
5. The protocol changed (vanishingly unlikely)

Resist the urge to immediately blame your code. Re-check fundamentals first.

### The Assioma will calibrate itself silently

Assioma's auto-calibration kicks in periodically while pedalling, which can cause brief power glitches in the data. This is normal Assioma behaviour, not a problem with the proxy. If you see weird single-second power spikes/dips, capture and check whether they correspond to internal Assioma calibration events.

## Ethical / legal flags

### Spoofing manufacturer ID

If Phase 0 reveals that we need to present as Stages's manufacturer ID to make the SB20 accept us, we are technically misrepresenting a manufacturer on-air. In practice:

- The on-air identity is presented only to one's own bike, for personal interoperability and to keep owned hardware working — not to any third party or marketplace.
- ANT+ device-type/manufacturer fields are not regulated legal identities — spoofing them on a private network for personal/research use is universally considered acceptable.
- We are not selling anything, and nothing claims to *be* a Stages product.
- Note that the Stages brand and IP now belong to Giant Group (SPIA Cycling) — i.e. an active company, not a defunct one — so "no one is harmed because the maker is gone" is no longer the right framing. The better framing is the one above: private interoperability with hardware you own. If the project is published, the README should state plainly what's being spoofed and why, and avoid any implication of affiliation or endorsement.

### Liability

Indoor cycling is generally low-physical-risk, but erg-mode resistance can produce high torque if the proxy reports zero power (the bike will "ramp up to find power"). A worst-case bug:

- Proxy reports 0W
- SB20 erg target is 200W
- Bike applies maximum resistance trying to make rider produce 200W
- Rider is doing nothing because of, say, an Assioma dropout
- When rider re-engages they hit a wall of resistance unexpectedly

This is mostly a discomfort/surprise issue rather than a safety issue, but the proxy should fail safe by *stopping* its broadcast (not transmitting zero) when input data is stale or missing. The SB20 will gracefully back off resistance if it hasn't seen a power update for a few seconds.

### Intellectual property

The ANT+ network key is technically licensed by ANT+. It's embedded in every standard ANT+ library (including openant) and is widely available. Personal use is universally tolerated. If this project is ever commercialised, look at licensing properly. For an open-source hobby tool, the precedent is clear and unproblematic.

### License hygiene with prior art

Several prior-art projects have copyleft licenses, most notably `cagnulein/qdomyos-zwift` (GPL-3.0). The intended license for *this* project is permissive (MIT or Apache 2.0) so that other SB20 owners — and any future descendants of the source/target abstraction — can adopt it freely.

Practical implication: we can **read** GPL-3.0 prior art, **understand** what they do, and **reimplement** the same protocol behaviour in our own code. We must not **copy** code (or significant prose) from a GPL-3.0 source into the project. If you find yourself wanting to copy a function verbatim from QZ, stop and reimplement it. If you find yourself studying how QZ handles a specific protocol detail, that's fine — but write up the technique in your own words in `findings/` and then implement from your notes, not from their source. This is the standard clean-room pattern.

The closer the implementation is to a transcription, the less defensible "clean-room" gets — so prefer to study a section, close the file, and then implement.

## Things to capture in `findings/decisions.md` as we go

- Every time you choose a numeric value (channel period, transmission type, manufacturer ID), record what you chose and why.
- Every time a hypothesis is refuted, record the refutation and the new working hypothesis.
- Every time a "it works now" moment happens, record what change made it work — these are the moments that get forgotten the fastest.
