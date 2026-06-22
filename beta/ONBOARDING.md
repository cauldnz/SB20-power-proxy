# SB20 Power Proxy — tester onboarding

Welcome, and thank you for helping test this. In ~15 minutes you'll have your SB20 reading the power
**you** choose. Everything here is done **off the bike** — you won't pedal until it's confirmed working.

> ⚠️ **Experimental.** This is a hobbyist, clean-room project — **not affiliated with Stages or Favero**.
> Use at your own risk. It impersonates your SB20's crank over Bluetooth; nothing is modified inside the bike.

## What this does
Your little board reads a Bluetooth power source and re-presents it to the SB20 as the bike's own crank,
so the SB20 broadcasts **that** power to Zwift/your apps. Two reasons people use it:
1. **Your own meter** — make the SB20 read your Assioma (or other BLE pedal meter) instead of its Stages
   crank (which tends to read a bit high).
2. **Crank rescue** — if your Stages crank is dead/dying, read the *surviving* crank (or any meter) and
   restore full power on the bike.

## What you need
- The **board** we sent you (it has a tiny screen).
- Your **power source**: a BLE pedal meter, *or* your surviving SB20 crank.
- Your **phone or laptop** (to do the one-time WiFi setup).
- Your SB20 and your usual training app.

## Setup (one time, ~10 min, off the bike)
1. **Power the board** (USB). The screen shows `SB20 SETUP`.
2. On your phone, **join the WiFi network `SB20-Setup`**. A setup page opens (or visit `http://192.168.4.1`).
3. **Join your home WiFi** through that page (so we can send you updates later). The board reboots and
   shows its IP on the screen.
4. Open the board's **dashboard** in a browser (the IP on the screen, or `http://sb20proxy.local/`).
5. Tap **⚙ Source** (top right) → **Scan** → **pick your power source** from the list (your meter, or
   your surviving crank — a Stages crank shows a “crank” tag) → **Save**. The board restarts to apply it.
   *(If we pre-configured your meter, it may already be selected — just confirm.)*
6. Back on the dashboard you'll see **METER IN → CRANK OUT** with live numbers (and your L/R balance)
   when you spin the source, and the **Source** tile shows your meter's name.

## The crank step (important — read this)
Your real Stages crank and the board both pretend to be "the crank", so they'd clash on air. Before you
ride, **either**:
- **Pull the battery** out of your SB20's left crank (the master), **or**
- (if we set you up that way) leave it — we'll have told you which.

*Crank-rescue testers:* if your left crank is already dead, there's nothing to pull — you're set.

## Pair & ride
1. In your training app, **pair to the SB20** exactly as you normally do.
2. **Pre-flight (still off the bike):** confirm the board's dashboard shows your source connected and a
   sensible power number when you spin it. **Green dashboard = good to ride.** (See the [ride protocol](TESTER-RIDE-PROTOCOL.md).)
3. Ride. The power your app shows should now match your meter's own app.

## If something's not right
- **Your meter isn't in the scan list**, or the **power looks wrong** → don't fight it on the bike.
  Grab a log for us: open `http://<board-ip>/log`, **save the page**, and send us the file (plus your
  meter's exact model). We'll add support and **push you an update over WiFi** — no need to send anything back.
- **Screen/board froze** → unplug/replug, and tell us what you were doing.

## Updates
We improve the firmware over the air. When we tell you an update is ready, just make sure the board is on
your WiFi (the screen shows its IP) and follow the one-line instruction we send.

## What we collect
Only your power meter's Bluetooth data + ride power/cadence — to add support and check accuracy. No
location or personal data. You're opting in by testing; tell us anytime if you'd rather stop.

Questions? Reply to us anytime. The ride protocol on the next page keeps your bike time to a minimum.
