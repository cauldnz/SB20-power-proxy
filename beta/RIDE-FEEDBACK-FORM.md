# Ride feedback — 60 seconds after your ride

Thanks for riding! This is the **system of record** for the beta (with the `/log` capture) — it's
how we know what's working and what to fix next. Copy the block below into your reply (or the linked
form) and fill the blanks. **One per ride.** If anything looked wrong, also send a `/diag` save (last
line) — that's the real signal.

> The first field — **build** — is on your board's dashboard (top of `http://sb20proxy.local/`, or the
> `version` line in `…/status`). It tells us exactly which firmware you rode, so we know if a fix has
> reached you yet.

```
SB20 proxy — ride feedback
build (dashboard version): ......        date: ........        app (Zwift/etc): ........

1. Meter you rode (brand + model):  ........................   sided: left-only / dual / not sure
2. Did the SB20 read your meter?     yes / no
3. Power match — did your training app match your meter's OWN app?
     spot on  /  off by ~__ W or __%  /  way off  /  didn't compare
4. Any hang, freeze, dropout, or reboot mid-ride?   no  /  yes →  what you were doing: ........
5. Setup/ride friction (1 = effortless, 5 = painful):  __     biggest snag: ........
6. Anything else (good or bad): ........................................................

If #2 was "no" or #3 was off:  open  http://<board-ip>/diag , Save the page, attach it here.
```

## What each answer drives (for us — not the tester)

- **build** → which firmware they're on (matches the fleet table; did our last OTA land? — see
  [RELEASE-AND-OTA.md](RELEASE-AND-OTA.md)).
- **meter + sided** → promote a 🟢 Expected meter to ✅ Verified, and confirm the **×2** setting
  ([supported-meters.md](../code/findings/supported-meters.md)).
- **power match** → the headline accuracy claim; a consistent offset is a real finding, not noise.
- **hang/reboot** → the coex-stability signal (Ride-mode WiFi-off is the mitigation;
  [perf-coex-plan.md](../code/findings/perf-coex-plan.md)).
- **friction** → onboarding fixes before the next tester.
- **`/diag`** → raw CPS frames → `parse_diag.py` → support + OTA (the loop in
  [beta-program.md](../code/findings/beta-program.md) §The tester loop).

Keep it async: they ride, they paste this, we analyse offline and come back with a fix — never
questions-during-the-ride.
