# Frequently asked questions

We add to this as riders ask. For setup, see the [user guide](README.md).

## Can I still use this if my Stages cranks have died?

**Yes, if at least one crank still works. If both have died, not yet.**

**One crank has died.** This is an ideal use case. Your power meter pedals replace the Stages cranks as
the bike's power source, and the bike keeps working as normal, including erg.

The surviving crank has to be awake so the bike can find it. The bike looks for both of the numbers set
in the Stages app, but it takes its power from the board. In the app:

- **Left:** your board's number.
- **Right:** your surviving crank's number.

We've tested this with the right crank as the survivor. If only your left crank works, try its number
in the **Right** box, and please [tell us](https://github.com/cauldnz/SB20-power-proxy/issues) how it goes.

**Both cranks have died.** The board can only stand in for one crank today, and if the bike can't find
both numbers, the app says pairing failed. Supporting a bike with no working cranks is tracked in
[issue 412](https://github.com/cauldnz/SB20-power-proxy/issues/412).

Before you write off a crank, fit a fresh CR2032. A flat battery looks just like a dead crank.
