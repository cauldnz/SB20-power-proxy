# Suggested Claude Code Prompt

Drop this in as the opening message of a Claude Code session. It primes the model with project context and a concrete first task.

---

```
We're working on the SB20 Power Proxy project. Read these files in order before doing anything:

1. The parent `Research_Content` document in the project files — at minimum the
   "Protocols Primer", "Python Libraries", and "Target Devices → Stages SB20"
   sections. This is the broader fitness-sensor research the SB20 project sits
   inside; the package below builds on top of it.
2. README.md (project orientation)
3. HANDOFF.md (your role and the first task)
4. 01-project-brief.md (goals and success criteria)
5. 03-central-hypothesis-and-phase-zero.md (most important — why we don't write proxy code yet)
6. 02-technical-context.md (background)

Then read these as needed:

- 04-architecture.md
- 05-implementation-phases.md
- 06-prior-art-and-references.md
- 07-hardware-and-environment.md
- 08-risks-and-gotchas.md
- 09-exploring-captures.md (the capture analysis pipeline — relevant immediately)
- 10-relationship-to-QZ.md (whether this should eventually upstream into QZ — read once, decision deferred to post-Phase-2)
- code/README.md
- code/findings/README.md

Also fetch and skim https://github.com/cagnulein/qdomyos-zwift/blob/master/CLAUDE.md before starting. QZ is a much larger, mature, GPL-3.0 sister project (cross-platform fitness-equipment bridge). Their CLAUDE.md documents conventions and verification steps that are well-suited to this kind of project. We won't copy their code (license incompatibility) but their patterns and discipline are worth borrowing.

After reading, summarise back to me:
- What the project is in two sentences
- How it relates to the broader Research_Content effort
- What Phase 0 is and why we can't skip it
- What the immediate first concrete coding task is
- The role of the analysis pipeline (`03_ingest_jsonl_to_influx.py`, `04_summarize_capture.py`, `05_diff_captures.py`) — particularly which outputs are designed for me to read versus for the owner's eyes only
- Two or three patterns from QZ's CLAUDE.md that would be worth adopting in a CLAUDE.md for this project (we don't have one yet — a possible early action is to create one based on what you've read)

Then propose how you'd improve the existing capture script (code/scripts/01_capture_stages.py) before we start running it. I'm particularly interested in:

(a) Whether the openant API I assumed (Channel.Type.BIDIRECTIONAL_RECEIVE, set_id, set_period, set_rf_freq, the openant.devices.ANTPLUS_NETWORK_KEY import) actually exists in the current openant release — verify against `pip show openant` output and the openant source on disk.

(b) Whether extended-message capture (to see ACK traffic from the SB20 back to the crank) actually works as I've written it, or whether we need to drop down to openant.base to enable it.

(c) Anything in the page-decoding logic that looks wrong against the ANT+ Bike Power profile spec.

(d) Whether the Phase 0 capture script should optionally use openant.devices.power_meter.PowerMeter as a high-level "sanity check" alongside the raw capture, to cross-check our decode logic against openant's own.

(e) Whether the JSONL schema written by 01_capture_stages.py and consumed by 03_ingest_jsonl_to_influx.py / 04_summarize_capture.py / 05_diff_captures.py is consistent — particularly check that the field names produced by `decode_page()` in 01 match what the analysis scripts expect. A schema mismatch here would silently break analysis after we've already collected captures.

Don't run anything yet. Just read, summarise, and critique.
```

---

## Why this prompt

- It forces a read-first, code-after pattern — which is the single biggest project risk per `03-central-hypothesis-and-phase-zero.md`
- It points at the parent `Research_Content` document explicitly so it doesn't get skipped (the package's value depends on that context being absorbed)
- It points at QZ's CLAUDE.md as a model — borrowing conventions from a large mature project in the same domain saves us from reinventing them
- It asks for a summary back, which surfaces misunderstandings before they become bugs
- It picks specific things to critique that are genuinely uncertain, rather than asking for generic review (which produces generic feedback)
- It explicitly says "don't run anything" — preventing the model from skipping ahead to "let me just try it and see"

## Follow-up prompts (for later sessions)

After Session A is captured by the owner (often before Claude Code is deeply involved):

```
I just ran Session A. Here's the validator output:

[paste output from 00_validate_capture.py --markdown]

Does this look right? Should I continue with sessions B–F, or is something
wrong I need to fix first?
```

After Phase 0 captures are committed:

```
Read code/findings/phase-0-report.md and code/findings/captures/*.jsonl.

Update 04-architecture.md to reflect what we now know vs what we assumed. Specifically:
- Channel parameters table (what we need to spoof)
- Whether single-sided mode worked
- Calibration handshake details
- The "Open architectural questions" section at the bottom — close out questions Phase 0 answered, raise new ones

Don't change any other files. After the update, summarise the architectural deltas in 5-10 bullet points.
```

After a Phase 1 attempt fails (which it will):

```
The Phase 1 replay failed. Symptoms:
[paste exact symptoms]

Captures from the failure are in:
code/findings/captures/[paste paths]

Form 3 hypotheses for what went wrong, in order of likelihood. For each:
- What in the capture would confirm or refute it?
- What's the smallest change to test it?

Reference the prior-art repos in 06-prior-art-and-references.md (especially dhague/vpower, OpenRowingMonitor, raralabs/pm5-emulator, qdomyos-zwift) — they may have hit the same issue.

Don't change any code yet. Propose; we discuss; then we change one thing and capture again.
```
