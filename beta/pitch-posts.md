# Pitch posts — SB20 Facebook group

Ready-to-post copy for recruiting ~10 pre-beta collaborators. Governed by
[`USERS-PLAYBOOK.md`](../USERS-PLAYBOOK.md): honest, fellow-rider, no hype, **no affiliation claims**
(Stages/Favero/Zwift), **no medical/performance claims, no guarantees**, experimental framing up front.
The product is **power-meter pedals becoming the crank replacement** — so the ask is for owners of
**Bluetooth power pedals**. Describe the hardware generically (a small USB-powered device), not by chip.
Pick **one** variant for your group, post **once**, reply genuinely. Fill the `[brackets]`.

> **Before posting:** read the group's rules — many ban selling / self-promo. This is **free, experimental,
> seeking unpaid collaborators** — not a sale. If unsure, ask a mod how to share it within the rules.

---

## Variant A — accuracy-seekers (the ~11% hook)

> **Does your SB20's power feel "off" vs your power pedals? I built something and I'm looking for a few
> testers.**
>
> Bit of a DIY project: on my own SB20 I noticed the bike's power read consistently ~10% higher than my
> Favero Assioma pedals — enough that my erg workouts didn't line up with my outdoor numbers. So I built
> a small, hopefully-low-cost device (runs off USB power) that reads **your power pedals** over Bluetooth,
> corrects them, and feeds them to the SB20 as the crank — so the bike trains off the pedals you trust,
> natively, in erg.
>
> It works on my bench and I want to prove it on **other people's pedals and bikes**. Looking for ~10 SB20
> owners who:
> • have **Bluetooth power-meter pedals** they trust (Favero Assioma / Garmin Rally / Wahoo Powrlink / SRM
>   EXAKT / etc.)
> • are happy this is **experimental & DIY**, not a product, and not affiliated with Stages or Favero
> • can join a setup WiFi, do one short ride, and send me a log file (I do all the analysis)
>
> You get a pre-set-up device for your bike and (hopefully) an SB20 that finally reads your pedals. In
> return I get real data across different pedal brands. There's more coming too — next on my list is
> **controlling erg target power from the SB20 shifter buttons** and **Zwift integration** — but step one
> is nailing the pedals. Interested? Comment or DM and I'll send a quick 2-minute form → **[FORM LINK]**

---

## Variant B — crank-rescue (the dead-Stages hook)

> **Stages crank on your SB20 dead or dying? I'm testing a way to bring the bike back to life with your
> power pedals.**
>
> The SB20's biggest weakness is that it only trusts its own Stages cranks — when one dies, the bike's erg
> loop dies with it, and replacements aren't cheap or easy. I've been building a small USB-powered device
> (hopefully cheap to make) that stands in for the crank: it reads your **Bluetooth power-meter pedals** and
> presents them to the SB20 so the bike works again — erg and all. Your pedals become the crank.
>
> It's experimental and DIY (I built it; it's not a Stages/Favero product), but it runs on my bench and I'd
> love to prove it on a **real failing-crank setup**. If you've got an SB20 with a dead/dying crank **and**
> a set of Bluetooth power pedals to drive it, and you're up for a short scripted ride + sending me a log,
> comment or DM — quick form here: **[FORM LINK]**
>
> No promises it'll save every setup, but if it does, you get your bike back — and there's more on the way
> (shifter-button erg control, Zwift integration) once the core is solid.

---

## Variant C — short combined teaser

> **SB20 owners with power pedals: testing a DIY device that makes the bike read *your* pedals (or rescues
> a dead crank). Want in?**
>
> Small USB-powered gadget that reads your **Bluetooth power pedals** and feeds them to the SB20 as the
> crank — so the bike trains off the pedals you trust, or keeps working after a Stages crank dies.
> Experimental, DIY, unaffiliated with Stages/Favero. More coming (shifter-button erg control, Zwift
> integration), but pedals first. Looking for ~10 testers with Bluetooth power pedals who'll do one short
> ride + send a log (I do the rest). Comment/DM → **[FORM LINK]**

---

## Comment & DM reply kit (the FAQs you'll get)

Keep replies short, honest, and warm. Common ones:

- **"How much / where do I buy it?"** → "It's not for sale — it's a free experimental project and I'm
  looking for collaborators, not customers. I'll send you a pre-set-up device; in return you ride once and
  send me a log. (It's a small USB-powered gadget — aiming for it to be cheap to make.)"
- **"Is this a Stages / Favero / official thing?"** → "Nope — totally independent, I built it myself. No
  affiliation with Stages, Favero or Zwift. Use at your own risk."
- **"I don't have pedals — I've got a [crank-arm / spider] power meter, will that work?"** → "Not for this
  — it's specifically built around power *pedals* replacing the crank, so it needs a Bluetooth power-meter
  pedal set. Crank-arm and spider meters are a different (later) thing."
- **"Will it work with my [pedals]?"** → "If they broadcast **Bluetooth** (not just ANT+), almost certainly
  — Assioma / Rally / Powrlink / EXAKT all use the standard Bluetooth power format. ANT+-only I can't read
  yet. Pop them in the form and I'll confirm." *(cross-check supported-meters.md)*
- **"Is it safe / will it brick my bike?"** → "It sits *between* your pedals and the bike over Bluetooth —
  it doesn't modify the SB20's firmware or your pedals. Worst case you unpair it and you're back to normal.
  It is experimental though, so: at your own risk."
- **"Does it do erg / Zwift?"** → "Yes — the SB20 runs erg off the corrected power, and your training app
  sees the SB20 as normal. Controlling the erg target from the SB20's shifter buttons, and tighter Zwift
  integration, are what I'm building next."
- **"How accurate is the correction?"** → "It's calibrated against *your* pedals on a short ride, so it
  ends up reading like them. I'll never claim a specific number — the calibration is per-bike."
- **"What do you collect?"** → "Just the power data + ride power/cadence — no personal or location data. I
  use captures to add pedal support; anything I keep is anonymised. It's all opt-in."
- **"What's coming next?"** → "Once the pedals are rock-solid: erg-target control from the SB20 shifter
  buttons, and better Zwift integration. Testers get those over-the-air as they land."
- **"Too good to be true / what's the catch?"** → "Fair! The catch is it's early and DIY — you're helping
  me prove it, so expect a rough edge or two and an update or two. If you want a polished product today,
  this isn't it yet."

If someone's clearly a **good fit** (SB20 + Bluetooth power pedals), send them the form + the welcome flow
in [`comms-templates.md`](comms-templates.md). If a **bad fit** (no pedals, ANT+-only, wants a finished
product), be kind and clear about why — a "not yet" protects you both (USERS-PLAYBOOK §1).
