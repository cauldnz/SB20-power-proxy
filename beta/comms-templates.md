# Comms templates — pre-beta tester lifecycle

Fill-in-the-blank messages for each step. Tone per [`USERS-PLAYBOOK.md`](../USERS-PLAYBOOK.md) §5:
a fellow rider who built a thing — honest, short, no hype, no affiliation/medical/performance claims,
bad news delivered early and plainly. Keep them async; never require a live session.

---

## 1 · Selected — welcome + consent

> Hey [name] — you're in! Thanks for offering to help prove this out.
>
> Quick honest reset before we start: this is **experimental and DIY** (I built it; not a Stages/Favero
> product), so expect a rough edge or two and a couple of updates. Your job is small: set the board up off
> the bike, do one short ride, and send me a log — I do all the analysis and push fixes to your board over
> the air (it never has to come back).
>
> **What I'll collect:** just your power-meter data + ride power/cadence — no personal or location data.
> Anything I keep to add meter support is anonymised. All opt-in, and you can stop any time.
>
> If you're good with that, reply "yes" and I'll get a board set up for your **[meter / crank-rescue]**
> setup. Anything change since the form (meter model, firmware)?

## 2 · Board on the way / ready

> Your board's set up and pre-configured for your **[meter model]**. [Posting it to you / Here it is].
>
> When it arrives, **don't ride yet** — first do the 10-minute off-bike setup: [ONBOARDING one-pager link].
> The key check is the dashboard showing your meter connected and the power tracking when you spin it. Ping
> me when you see that and I'll confirm you're green to ride. (Pre-flight first, pedals second — saves us
> both a frustrating first session.)

## 3 · Pre-flight confirmed → cleared to ride

> That's exactly what I want to see — green. You're cleared. When you've got 10–15 min, here's the short
> ride: [TESTER-RIDE-PROTOCOL link]. It's literally "spin easy, watch the two numbers line up, do a couple
> of efforts, stop." Then send me the **/diag** file from the board (the protocol shows where). No rush —
> whenever suits.

## 4 · Post-ride follow-up (nudge, if quiet)

> No pressure at all — just checking in. When you get a chance for that short ride + the /diag file, I'll
> take a look and let you know how it's reading vs your meter. If anything was confusing in the setup, tell
> me — that's useful signal too.

## 5 · It worked — confirm + thank

> Looked at your log — [your XCadey was reading ~X% off, now corrected to within ~Y W of your Assioma /
> the SB20 is reading your meter cleanly]. So your bike's now training off [meter], natively. 🎉
>
> Ride it normally for a bit and tell me if anything drifts or feels off. And thank you — your data just
> [confirmed / added] support for [meter brand], which helps the next person too.

## 6 · Meter not recognised / power looks wrong (the OTA loop)

> Thanks — this is genuinely useful. Your [meter] sends its data a little differently than the ones I've
> tested, so the board didn't pick it up cleanly. **This is fixable and on me, not you.** The /diag file you
> sent has exactly the bytes I need; I'll add support for your meter and push an update to your board over
> the air — no need to send anything back. I'll ping you when it's ready (usually a day or two), then one
> more short ride confirms it. Hang tight.

## 7 · OTA update ready

> Update's ready for your board — it adds [support for your meter / the fix for X]. To take it: [one-line
> OTA step / "it'll pick it up automatically next time it's on your WiFi"]. Once it's updated, redo the
> quick pre-flight (meter connected on the dashboard) and, if green, that same short ride + /diag so I can
> confirm it's landing right. Thanks for the patience.

## 8 · Decline / not a fit (kindly)

> Hey [name] — really appreciate you offering. Honest answer: your [ANT+-only meter / setup] is one I
> **can't support yet** — the device reads Bluetooth power meters, and yours broadcasts ANT+ only, so it'd
> just frustrate us both right now. I'm keeping a note of it though, and if I add a path for that I'll
> circle back. Thanks for the interest — and feel free to capture/share anything about your setup that
> might help.

## 9 · Waitlist / hold (good fit, but full)

> Hey [name] — you're a great fit and I'd love to include you, but I'm keeping the first round small
> (~10) so I can actually support everyone well. You're top of my list for the next batch — I'll reach out
> the moment a spot opens or the next round starts. Thanks for being patient.

## 10 · Wrap / thank-you (end of pre-beta)

> That's a wrap on the first round — thank you, genuinely. Where things landed for you: [one line]. Your
> rides + logs [added meter X / proved the crank-rescue case / found the Y bug], which is exactly what this
> needed. I'll keep your board updated as things improve; tell me the **one feature** you'd most want next
> (shifter buttons? something else?) — that list is shaping what I build. Cheers for helping prove it.

---

**Per-ride feedback form (60 seconds, optional but useful):** what worked · did the power match your meter ·
any hang/disconnect · which meter/app · one thing that annoyed you. (A Form; pairs with the /diag file,
which is the real signal.)
