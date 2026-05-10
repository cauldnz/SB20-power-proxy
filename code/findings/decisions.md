# Decisions Log

Append entries chronologically. Don't edit old entries — write new ones to supersede. Date stamps in YYYY-MM-DD format. The point is that future-you (or another implementer) can read this top to bottom and understand how thinking evolved.

Format per entry:

```
## YYYY-MM-DD — Short title

Decision: <one sentence>

Context: <why this came up>

Options considered: <list>

Why this option: <reasoning>

Will revisit if: <what would change our minds>
```

---

## 2026-05-10 — Initial project scoping (placeholder, replace with date of actual start)

Decision: Pursue an ANT+ man-in-the-middle proxy approach rather than pure-app-side bridging (TR PowerMatch / QZ).

Context: The goal is for the SB20 *itself* to use Assioma data — not just for connected apps to see Assioma numbers. App-side bridges leave the bike's internal control loop using its own Stages cranks, which become a single point of failure when (not if) those cranks die.

Options considered:
1. App-side bridge (TR PowerMatch / QZ) — works today but doesn't address the bike-level use case
2. ANT+ MITM proxy presenting as a Stages crank — addresses the use case if technically feasible
3. Modify SB20 firmware — out of reach without serious reverse engineering, also brittle
4. BLE-side spoof — alternative to ANT+ MITM, kept as fallback

Why option 2: Cleanly addresses use cases 1 and 2 from the brief. Independent of any specific training app. Builds on the bike's existing protocol contract.

Will revisit if: Phase 0 captures reveal the SB20 has a bespoke handshake we cannot replicate without proprietary keys / firmware reverse engineering. In that case, BLE-side or app-side approaches become the next options.

## 2026-05-10 — Project sits inside parent research effort

Decision: Treat the owner's parent `Research_Content` document as foundational context rather than duplicating its content into this package.

Context: The owner has a broader fitness-sensor research effort surveying ANT+/BLE protocols, Python and Rust libraries, Pi hardware, browser delivery, and target devices including SB20, Tacx Neo, and Concept2 PM5. This SB20 project is one specific application of that broader work.

Why this matters for the package:
- Foundational protocol knowledge (ANT+ vs ANT-FS, GATT services, profile mappings) lives in the parent doc — we reference rather than re-explain.
- Library recommendations (openant, pycycling, bleak; eventually ant-rs, btleplug, bluer) come from there.
- Long-term architectural direction (Python today, Rust gateway later via openant→MQTT→Rust pattern) is articulated there.

Will revisit if: the parent research is updated in a way that changes the recommended toolchain.

## 2026-05-10 — Python today, Rust later

Decision: v1 implementation in pure Python (openant). Defer any Rust port until Phases 0–3 are working.

Context: The parent research notes that Rust ANT+ support (ant-rs / ant-plus) is improving but not yet at openant's maturity, particularly for the master/transmit side that this project depends on. Python's openant has been used successfully for ANT+ master-broadcast in projects like dhague/vpower.

Will revisit if: ant-rs reaches feature parity for master-mode Bike Power broadcasts (the 2026 roadmap suggests this might happen). At that point the openant→MQTT→Rust pattern becomes worth considering, especially for Pi deployment where Rust's resource efficiency would help on a Pi 4 or smaller.

## 2026-05-10 — QZ as architectural validator and Peloton reference

Decision: Treat `cagnulein/qdomyos-zwift` (QZ) as the primary external reference for fitness-device protocol bridging, but do not depend on or copy from it.

Context: QZ is a mature C++/Qt cross-platform application that bridges dozens of proprietary fitness bikes/treadmills/ergs into standard BLE FTMS/CPS broadcasts. Its hierarchical device architecture (a `bluetoothdevice` abstract base class with virtual-device targets) is a C++/Qt analogue of our `PowerSource`/`PowerTarget` design — independent convergence on the same shape from a much larger codebase is encouraging. QZ also has working bidirectional Peloton support, which is the closest reference for the future Peloton project the owner has flagged.

Why we don't copy: QZ is GPL-3.0. Our project will be MIT (or similar permissive) so other SB20 owners can adopt it without copyleft constraints. We can study and reimplement, but not transcribe.

Will revisit if: the project's planned permissive license becomes a non-issue (e.g. the owner decides GPL-3.0 is fine after all), in which case selective porting from QZ becomes legitimate.

## 2026-05-10 — Defer creation of project-level CLAUDE.md

Decision: Don't create a `CLAUDE.md` at the project root yet; do so as one of the first concrete actions after Phase 0 captures begin to land.

Context: QZ has a substantial `CLAUDE.md` documenting build commands, architecture, and verification steps for adding new device patterns. We could mirror that, but right now we don't have enough concrete project surface (no real device patterns yet, no captured protocol details) to write a useful equivalent. Creating one too early would be aspirational rather than load-bearing.

Will revisit when: Phase 0 captures are committed and Phase 1 implementation begins. At that point a `CLAUDE.md` describing capture-then-decode workflow, the source/target ABC contract, and how to add a new `PowerSource` becomes worth writing.
