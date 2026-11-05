# Releases

Shipped releases of the **C3-OLED crank-spoof firmware** (`esp32c3-oled-live-release`) — the build a
rider flashes and rides. Cut by pushing a `vMAJOR.MINOR.PATCH` tag; the mechanics, the QA gate and the
rollback path are in [`beta/RELEASE-AND-OTA.md`](beta/RELEASE-AND-OTA.md).

Each `## <version>` section below is published verbatim as that release's GitHub notes, so a tag with
no section here fails the release build (`sb20proxy.release.release_notes_section`). This file is the
**rider-facing** summary of what shipped; the engineering log stays in
[`code/findings/decisions.md`](code/findings/decisions.md).

> Not to be confused with [`CHANGELOG.md`](CHANGELOG.md), which is the unmaintained pre-pivot
> documentation-revision history (⛔ historical, stopped 2026-06-15).

Versions are compared by the device as three integers (`OtaManifest::isNewerVersion`), so there are no
prerelease tags — a `v0.1.0-rc1` would be indistinguishable from `v0.1.0` and would strand any board
that installed it. Release candidates are built through the release workflow's dry-run path, which
publishes nothing.

## 0.1.0 — unreleased

The first release. Nothing has shipped before this, so the notes describe the build rather than a
delta: this is the first tagged, version-stamped, QA-gated artifact of the thing the project has been
proving on the bike since June 2026.

**What it is.** An ESP32-C3 with a 0.42″ OLED that reads a real power meter over BLE (Cycling Power
Service `0x1818`) and re-presents it as a Stages left crank, byte-faithfully enough that an SB20
accepts it as its own and runs its erg loop off it. Configured over WiFi at `/setup`, observed at
`/`, `/log` and `/status`, updated by push-OTA.

**Validated on air before this tag** (the sessions behind it, not claims made at the desk):
- The SB20 accepts the spoof's power *and* cadence with no real crank present (session 2), and pairs
  on our own crank id provided the bike's real right crank stays findable (sessions 8 and 9).
- The Stages app's calibration completes against the spoof, and a zero-reset reaches the real pedals
  and comes back with a genuine offset (session 9: `200c01ffff`).
- The bike's own handlebar buttons come out as OpenBikeControl actions and drive a consumer to a
  felt resistance change (session 13).
- 30 minutes of ride-shaped load: no reboots, heap flat, dual-role BLE free (September 2026), and a
  44-minute WiFi/LVGL soak on the bike (2026-09-27).

**Known limits — read before riding.**
- **Erg driven by this board is unproven on a real SB20.** The on-device workout engine, the FTMS
  client and the control-point arbiter are host-tested and twin-proven, but no SB20 has ever been
  driven by them (`ROADMAP.md` Now #7, session 14 G2). Erg through qz or Zwift is the proven path.
- **Signed-pull OTA is not wired.** Updates are maintainer-driven push-OTA over the LAN
  (`firmware/flash.ps1`); the device does not poll for updates. The manifest parser, the ed25519
  verifier and the signing tool all exist and are host-tested, but the signing key and the manifest
  host do not — so `0.1.0` cannot update itself.
- **One rider, one bike.** The two-bike identity discipline is implemented (MAC-derived
  `Stages 9NNNN` defaults, pin-by-address) but only ever exercised at the bench.
- **A board with no stored identity may not pair.** With nothing in NVS the board advertises a
  MAC-derived `Stages 9NNNN`, and whether the Stages app accepts a `9xxxx` crank id has **never been
  tested on a bike** — only our own `62145` is proven (session 8). The `9` prefix was chosen so a
  derived id can never collide with a real crank, not because it is known-good. If pairing fails,
  set the identity explicitly in `/setup` (`Stages 62145`) and retry; a stored identity survives a
  flash and is never overwritten. Never use `Stages 62144` — that is bike 1's real left crank.
- The telemetry degradation on a cold start (#288) is still uncaused; the Stages app is the known
  workaround.

**Release mechanics added with this tag** (why there was no `0.0.x`): the version stamp had never
worked. `beta/RELEASE-AND-OTA.md` documented stamping it with `pio run --project-option=…`, a flag
PlatformIO 6.1.19's `run` does not have — so the command failed outright, and no build had ever
carried a version other than the `Config.h` default. Stamping now happens in a dedicated
`esp32c3-oled-live-release` env, an unstamped release build is a compile error rather than a bad
artifact, and dev builds self-identify as `0.0.0-dev`.
