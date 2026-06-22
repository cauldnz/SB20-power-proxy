# Tester ride protocol — short, scripted, off-the-bike-first

Your time on the bike matters to us. **You never pedal until the board has been verified ready.** A test
ride is a few minutes of riding, not an evening of fiddling. The board records what we need; you send us a
file afterwards and we do the analysis.

## Pre-flight (off the bike — this is the gate)
Do this with the board powered and on WiFi, **before you get on**:

- [ ] Board screen shows its **IP** (not `SB20 SETUP`).
- [ ] Open the **dashboard** (`http://<board-ip>/`). The **Source** tile shows your meter's name
      (e.g. `ASSIOMA17039L (connected)`) — that's your confirmation the board found *your* source.
      *(Or tap **⚙ Source**: it reads “**Reading … ✓**” when connected.)*
- [ ] **Spin your power source** (pedal the meter / turn the crank by hand): **METER IN** power moves
      and **CRANK OUT** mirrors it. *(Cadence and L/R balance show too, if your meter sends them.)*
- [ ] In your training app, the **SB20 is paired** and shows power when you spin.
- [ ] The real left crank battery is **out** (unless we told you otherwise).

**All five ticked = green. Only then mount the bike.** If any fail → **don't ride yet**, send us a
[diagnostic](#sending-us-a-diagnostic) and we'll sort it. Don't troubleshoot in the saddle.

**Optional (for the most reliable ride):** once green, tap **“Ride mode — turn WiFi off”** at the bottom
of the dashboard. That frees the board's radio for Bluetooth only (it can't freeze under WiFi+BLE load).
The dashboard goes away until you **power-cycle** the board — that's expected; the power proxy keeps working.

## The ride (we'll send you a specific short script; the default is)
1. **5 minutes easy**, steady. Glance once: the power in your app should track your meter's own app/head
   unit (if you have it visible). They won't be identical second-to-second — that's normal.
2. **A couple of harder efforts** (~20–30 s) if you're up for it — just to see power tracks under load.
3. Stop. That's it — usually **under 10 minutes** of actual riding.

You don't need to watch screens the whole time or stay on a call. The board logs continuously.

## After the ride (60 seconds)
- Fill the **short form** we sent (did power match? any freeze? which meter?).
- If anything looked off, **send us a diagnostic** (below). Otherwise you're done.

## Sending us a diagnostic
1. Open `http://<board-ip>/diag` in a browser.
2. **Save the page** (Ctrl/Cmd-S) — or copy all the text into a file. It has your config, the live status,
   and your meter's raw signal (no personal data).
3. Send it to us with your **meter's exact model**. We decode it offline, add/fix support, and **push you
   an update over WiFi**. Nothing comes back to us physically. *(The `/log` page is also there if we ask.)*

## What we will *not* do to you
- Send you to the bike before it's verified working.
- Ask you to debug live or read byte values.
- Make you re-flash, return the board, or run developer tools.
- Drip-feed requests — we batch what we need into one message.

If a session can't be made ready, we'll say **"hold off"** rather than waste your ride. Thank you — every
log you send makes this work for the next SB20 owner.
