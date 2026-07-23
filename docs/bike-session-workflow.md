# How we run an AI-directed bike session

Our bike is the expensive test environment. The scarce resource is not compute; it is the rider's
time, attention, and patience. The goal is therefore to use the bike only for questions that genuinely
require the real hardware, and to make every visit shorter and better prepared than the last.

> This is a short walkthrough, not a second run book. The canonical operating detail is in the
> [physical-session playbook](../sessions/PLAYBOOK.md), with every session tracked in the
> [Plan + Actual ledger](../sessions/README.md). Its companion disciplines are the
> [desk-development playbook](../DEV-PLAYBOOK.md) and [users playbook](../USERS-PLAYBOOK.md).

## The operating model

**The AI agent directs; the human is the hands, legs, and eyes.** Before the session, the agent does
the desk work. During the session, it gives one physical instruction at a time, says what should
happen, watches the telemetry and captures, and records the actual result. The rider pedals, pairs
devices, presses buttons, swaps batteries, and narrates what they see.

```text
PLAN AT THE DESK  ->  RUN ONE GATE AT A TIME  ->  PRESERVE THE EVIDENCE
        ^                                                   |
        |                                                   v
        +----------- IMPROVE TOOLS AND PLAYBOOK <--- RETRO AND LEARN
```

## 1. Plan: spend compute before spending rider time

We follow the [desk-development loop](../DEV-PLAYBOOK.md) first: compile, test, replay captures, flash
the board, verify the tools, and prepare exact commands before the rider goes near the bike. Every
session has:

- a small number of must-do gates, ordered by information value;
- an honest time budget, with investigation distinguished from simple verification;
- an explicit action, expected result, pass/fail rule, and evidence to capture for each gate;
- a fallback for likely failures; and
- a written bring-list and restore-list so the bike is returned to its original state.

The highest-value go/no-go runs first. If it fails, we capture enough evidence to debug at the desk
instead of consuming the rest of the rider's patience.

## 2. Execute: one observable step at a time

The agent waits until the rider explicitly says they are at the bike. It then gives exactly one
instruction and states the expected observation before the rider acts:

> Pedal for ten seconds. You should see the source connect and power track near 200 W. Tell me what the
> bike shows; I will confirm it against the live log before we continue.

The agent runs the capture and diagnostic tools itself. Each step is recorded in that session's
[Plan + Actual document](../sessions/README.md) as `PASS`, `FAIL`, or `PARTIAL` against telemetry,
logs, and radio captures--not optimism. Actual start times and any improvised actions are written down
while they happen.

If a gate becomes stuck, we do not repeat it blindly. We collect the diagnostic, stop, and move the
problem back to the desk. The bike is for producing new information, not for debugging by repetition.

## 3. Document: make the trip replayable

The plan and the actual result live in the same session document. At close-out we:

1. record the outcome and update the session ledger;
2. preserve the raw captures so later questions can be answered without another ride;
3. promote durable findings into the project's decision record;
4. restore and verify the bike's normal configuration; and
5. state the next gate and the desk work required before it.

This is important because the best discovery often happens days later while querying a capture. A
well-recorded ten-minute ride can support many hours of desk analysis.

## 4. Retro: turn friction into leverage

Every session ends with four questions:

- What went well and should be repeated?
- What was slow, confusing, or failed--and what was the root cause?
- How did planned time compare with actual time?
- What concrete change will prevent the same cost next time?

A retro item is not complete until it changes the process, run sheet, tooling, or pre-flight checks.
For example, unreliable over-the-air flashing became a flash helper with signal-strength checks and
automatic retry. Capture setup delays became a machine-readable pre-flight gate.

That creates the compounding loop: **a problem on today's ride becomes automation or guidance before
the next ride**. Success is not merely completing a test; it is reducing the number and duration of
future trips to the bike.
