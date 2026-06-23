# Pre-beta recruiting & selection

How we go from "an SB20 Facebook group" to **~10 well-chosen collaborators**. Governed by
[`USERS-PLAYBOOK.md`](../USERS-PLAYBOOK.md) (the principles) and
[`code/findings/beta-program.md`](../code/findings/beta-program.md) (the operating plan). The
ready-to-post copy is [`pitch-posts.md`](pitch-posts.md); the message templates are
[`comms-templates.md`](comms-templates.md).

> **Goal of the pre-beta:** ~10 SB20 owners reading their own **power pedals** (or restored from a dead
> crank by their pedals), a live capture→fix→OTA loop, and a growing supported-**pedal** library — *not*
> scale. Quality of fit and of the experience beats headcount. It's fine to ship 6 great ones, hold the rest.

## The funnel

```
Facebook post ─▶ interested comment/DM ─▶ screening form ─▶ selection ─▶ ship a board ─▶ onboard ─▶ loop
   (pitch)          (reply + form link)      (~7 Qs)        (fit, not FIFO)   (pre-flashed)
```

Expect heavy drop-off (that's healthy): a recruiting post may draw 30–50 reactions, ~10–20 comments,
maybe 15 form starts → you *select* ~10. Over-recruit slightly (12–14 forms) so you can pick for fit
and meter-brand spread and still land 10 good ones.

**Non-negotiable: the user owns a Bluetooth power-meter PEDAL set.** The product is pedals → crank
replacement; crank-arm and spider meters are out of scope. Two pools, both pedal owners:

- **Accuracy-seekers** — own **Bluetooth power pedals** they trust (Favero Assioma, Garmin Rally, Wahoo
  Powrlink, SRM EXAKT…), bothered the SB20 reads ~11% off them. Want the SB20 to read *their pedals*
  natively in erg.
- **Crank-rescue** — a **dead/dying Stages crank**, replaced by their **pedals**; want their bike working
  again. Most motivated, most engaged, unambiguous value.

Aim for **≥2–3 crank-rescue** (the clearest win) and a **spread of pedal brands** among the accuracy pool
(each new brand that works grows the library — the whole point).

## Selection criteria (a fit judgement, not first-come)

Score each applicant; pick the best fit + brand spread, not the fastest to reply.

| Signal | Strong fit | Screen out / hold |
|---|---|---|
| **Owns Bluetooth power-meter PEDALS** (the hard requirement) | yes — pedals, BLE | no pedals (crank-arm/spider) → *out of scope*; ANT+-only → *can't read*; say so kindly |
| **Use case is clear** | accuracy *or* crank-rescue, stated | "just curious", no real need |
| **Comfortable with experimental/DIY, at-own-risk, unaffiliated** | yes | wants a polished consumer product *now* |
| **Willing to do the async loop** | join WiFi · pull the dead crank's battery if needed · send a log · take an OTA | wants live hand-holding / can't send a file |
| **Pedal brand we don't have yet** | **bonus** — promotes those pedals to Verified | (still fine, just lower novelty) |
| **Crank-rescue case** | bonus (motivation + clear value) | — |

Cross-check the pedals against [`supported-meters.md`](../code/findings/supported-meters.md): **Verified**
(Assioma) = lowest risk; **Expected** (mainstream BLE power pedals) = high value (their `/diag` promotes
the brand); **Out of scope** (crank-arm/spider, ANT+-only) = decline at screening, with the reason.

## The screening form (~7 questions)

Host on a **Microsoft/Google Form** (zero infra — see Hosting). Questions (mirrors
`beta-program.md` §Screening):

1. **SB20 confirmed?** Firmware version (Stages app → About)?
2. **Which use case** — read your pedals natively, or rescue a failing crank with your pedals?
3. **Your power pedals (required):** exact **brand + model**, and do they broadcast **Bluetooth** (not just
   ANT+)? *(If they pair to a phone app over Bluetooth, or Zwift sees them as a Bluetooth power source →
   yes.)* **No power pedals = not a fit** (crank-arm/spider meters are out of scope).
4. **Single-sided or dual?** (left-only pedal vs dual — sets the ×2 doubling toggle.)
5. **If crank-rescue:** which crank is dead (L/R)? *(Your pedals replace it; the dead crank just gets its
   battery pulled so the SB20 pairs to our device.)*
6. **Training app(s)** you use (Zwift, etc.).
7. **Willing to:** join a setup WiFi · pull a crank battery if needed · send a log file after a ride ·
   take an OTA update? And **comfortable that this is experimental, unaffiliated with Stages/Favero, use
   at own risk?** (All yes = a good fit.)
+ a contact handle (DM / email) and a consent checkbox (see `USERS-PLAYBOOK` §6 / `beta-program.md`
  §Data & consent).

## What a selected user gets

A **pre-flashed C3-OLED board**, pre-configured to their meter where the screening let us, plus the
[onboarding one-pager](ONBOARDING.md) and the [ride protocol](TESTER-RIDE-PROTOCOL.md). Boards iterate by
**OTA** — none ever comes back. (Cost note: the owner ships the first ~10 at their own cost as the
collaboration trade; not a sale.)

## Hosting (what, if anything, we host)

Keep infra near-zero for the pre-beta; self-host only what genuinely needs it.

| Need | Recommended (pre-beta) | Alternative |
|---|---|---|
| **Signup / screening form** | **Microsoft/Google Form** — zero hosting, built-in responses, owner already in the MS ecosystem | a self-hosted form on Unraid if branding matters |
| **Browser-flash page** (ESP Web Tools, needs HTTPS) | **GitHub Pages** — already documented in [`firmware/webflash/README.md`](../firmware/webflash/README.md); free, HTTPS, no infra | **Unraid + reverse proxy** (Caddy / Nginx Proxy Manager, auto Let's-Encrypt) if self-hosting the `.bin` |
| **A landing page / docs** (optional, later) | **Unraid + Caddy** (cheap, owner-controlled, auto-HTTPS) | **Azure Static Web Apps** if a custom domain + CDN + managed TLS is wanted at scale |

**Recommendation:** start with a **Form + GitHub Pages** (no servers to run). Spin up the **Unraid +
reverse-proxy** box only when there's a reason to self-host (a branded signup page, hosting the firmware
`.bin`, or a tester portal); reach for **Azure** if/when this goes past the pre-beta to a public,
custom-domain product. Nothing in the pre-beta *requires* a server.

## Timeline (rough)

1. **Now:** post the pitch (one group, one post) → collect form responses for ~3–5 days.
2. **Select** ~10 for fit + brand spread; send the "you're in" + consent message; decline/hold the rest
   kindly (keep a waitlist).
3. **Pre-flash + ship** (or hand over) the boards, pre-configured from the screening.
4. **Onboard + first rides** over the following week; run the capture→OTA loop weekly.
5. **Exit review** at ~10 working setups (see `USERS-PLAYBOOK` §7).

## Don'ts (Facebook + good-citizen)

- **Check the group's rules first** — many ban selling / self-promo. We're **seeking unpaid collaborators
  for a free experimental project**, not selling; frame it that way and post once (no spamming).
- **No affiliation claims** (Stages/Favero/Zwift), **no medical/performance claims**, **no guarantees.**
- One honest post + genuine replies beats five salesy ones. If a mod removes it, ask them how to share it
  within the rules rather than reposting.
