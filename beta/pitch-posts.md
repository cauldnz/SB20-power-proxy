# Pitch posts — SB20 Facebook group

Ready-to-post copy for recruiting ~10 pre-beta collaborators. Governed by
[`USERS-PLAYBOOK.md`](../USERS-PLAYBOOK.md): honest, fellow-rider, no hype, **no affiliation claims**
(Stages/Favero/Zwift), **no medical/performance claims, no guarantees**, experimental framing up front.
Pick **one** variant for your group, post **once**, reply genuinely. Fill the `[brackets]`.

> **Before posting:** read the group's rules — many ban selling / self-promo. This is **free, experimental,
> seeking unpaid collaborators** — not a sale. If unsure, ask a mod how to share it within the rules.

---

## Variant A — accuracy-seekers (the ~11% hook)

> **Does your SB20's power feel "off" vs your real power meter? I built something and I'm looking for a
> few testers.**
>
> Bit of a DIY project: on my own SB20 I noticed the bike's power read consistently ~10% higher than my
> Favero Assiomas — enough that my erg workouts didn't line up with my outdoor numbers. So I built a
> little battery-free ESP32 gadget that reads *your* power meter over Bluetooth, corrects it, and feeds
> it to the SB20 as the crank — so the bike trains off the meter you trust, natively, in erg.
>
> It works on my bench and I want to prove it on **other people's setups and meters**. Looking for ~10
> SB20 owners who:
> • have a **Bluetooth** power meter they trust (Assioma / Garmin Rally / Wahoo Powrlink / 4iiii /
>   Quarq / Power2Max / etc.)
> • are happy this is **experimental & DIY**, not a product, and not affiliated with Stages or Favero
> • can join a setup WiFi, do one short ride, and send me a log file (I do all the analysis)
>
> You get a pre-set-up board for your bike and (hopefully) an SB20 that finally reads your meter. In
> return I get real data across different meters. Sound interesting? Comment or DM and I'll send a quick
> 2-minute form → **[FORM LINK]**

---

## Variant B — crank-rescue (the dead-Stages hook)

> **Stages crank on your SB20 dead or dying? I'm testing a way to bring the bike back to life with any
> Bluetooth power meter.**
>
> The SB20's biggest weakness is that it only trusts its own Stages cranks — when one dies, the bike's
> erg loop dies with it, and replacements aren't cheap or easy. I've been building a small battery-free
> ESP32 device that stands in for the crank: it reads a **third-party Bluetooth power meter** (a pedal
> meter, a spare crank arm, whatever you've got) and presents it to the SB20 so the bike works again —
> erg and all.
>
> It's experimental and DIY (I built it; it's not a Stages/Favero product), but it runs on my bench and
> I'd love to prove it on a **real failing-crank setup**. If you've got an SB20 with a dead/dying crank
> **and** any Bluetooth power meter to feed it, and you're up for a short scripted ride + sending me a
> log, comment or DM — quick form here: **[FORM LINK]**
>
> No promises it'll save every setup, but if it does, you get your bike back.

---

## Variant C — short combined teaser

> **SB20 owners: testing a DIY gadget that makes the bike read *your* power meter (or rescues a dead
> crank). Want in?**
>
> Battery-free ESP32 that reads a Bluetooth power meter and feeds it to the SB20 as the crank — so the
> bike trains off the meter you trust, or keeps working after a Stages crank dies. Experimental, DIY,
> unaffiliated with Stages/Favero. Looking for ~10 testers with a Bluetooth meter who'll do one short
> ride + send a log (I do the rest). Comment/DM → **[FORM LINK]**

---

## Comment & DM reply kit (the FAQs you'll get)

Keep replies short, honest, and warm. Common ones:

- **"How much / where do I buy it?"** → "It's not for sale — it's a free experimental project and I'm
  looking for collaborators, not customers. I'll send you a pre-set-up board; in return you ride once and
  send me a log."
- **"Is this a Stages / Favero / official thing?"** → "Nope — totally independent, I built it myself.
  No affiliation with Stages, Favero or Zwift. Use at your own risk."
- **"Will it work with my [meter]?"** → "If it broadcasts **Bluetooth** (not just ANT+), almost certainly
  — Assioma/Rally/Powrlink/4iiii/Quarq/etc. all use the standard Bluetooth power format. ANT+-only meters
  I can't read yet. Pop it in the form and I'll confirm." *(cross-check supported-meters.md)*
- **"Is it safe / will it brick my bike?"** → "It sits *between* a meter and the bike over Bluetooth — it
  doesn't modify the SB20's firmware or your meter. Worst case you unpair it and you're back to normal. It
  is experimental though, so: at your own risk."
- **"Does it do erg / Zwift?"** → "Yes — the SB20 runs erg off the corrected power, and your training app
  sees the SB20 as normal. (Shifter/button support is on the list but not yet.)"
- **"How accurate is the correction?"** → "It's calibrated against *your* reference meter on a short ride,
  so it ends up reading like that meter. I'll never claim a specific number — the calibration is per-bike."
- **"What do you collect?"** → "Just the power-meter data + ride power/cadence — no personal or location
  data. I use captures to add meter support; anything I keep is anonymised. It's all opt-in."
- **"Too good to be true / what's the catch?"** → "Fair! The catch is it's early and DIY — you're helping
  me prove it, so expect a rough edge or two and an update or two. If you want a polished product today,
  this isn't it yet."

If someone's clearly a **good fit**, send them the form + the welcome flow in
[`comms-templates.md`](comms-templates.md). If a **bad fit** (ANT+-only, wants a finished product), be
kind and clear about why — a "not yet" protects you both (USERS-PLAYBOOK §1).
