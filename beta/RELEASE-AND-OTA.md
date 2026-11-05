# Releases & OTA — the known-good build and how a fix reaches the fleet

**Status: the pre-beta release runbook (2026-06-26).** Phase 4's "known-good default build + an OTA
channel" ([pre-beta-plan.md](../code/findings/pre-beta-plan.md)). Pairs with the OTA *security* design
[ota-update-plan.md](../code/findings/ota-update-plan.md) (this is the *operational* half) and the
pre-ship QA gate ([beta-program.md](../code/findings/beta-program.md) §Pre-ship QA).

The whole point of shipping pre-flashed boards is that **no board ever comes back**: we add meter
support / fix bugs and push them out by OTA. This doc defines the build we ship and how an update lands.

## The known-good default build

- **Env:** `esp32c3-oled-live-release` (the C3-OLED beta board — real meter, WiFi/OTA, OLED). It is
  `esp32c3-oled-live` plus the version stamp and nothing else, so the image CI publishes is the image
  the bench and the bike proved. Built from `origin/main` only (never a dirty tree; CLAUDE.md flash
  hygiene) — in practice, built by CI from a tag.
- **Version:** every shipped/OTA'd build carries a **semver** in `SB20_FIRMWARE_VERSION`, surfaced at
  `/status` (`"version"`) and on the `/diag` header (so a tester report and the dashboard both show it).
  **The stamp comes from the git tag — you never type a version.** Pushing `v0.1.0` makes
  `.github/workflows/release.yml` build the dedicated **`esp32c3-oled-live-release`** env with
  `SB20_VERSION=0.1.0`; everything else (every dev and CI build) carries `Config.h`'s `0.0.0-dev`
  default, so `/status` can never make a desk build look like a shipped one. **Bump = semver:** patch
  for a meter-support/bugfix OTA, minor for a feature, and never reuse or go backwards — the device's
  update decision is a strict `isNewerVersion` compare (`OtaManifest::shouldUpdate`), which the
  workflow enforces against the existing tags before it builds anything.

  > ⚠️ **The recipe this doc used to give never worked.** It said to stamp the version with
  > `pio run … --project-option="build_flags=-DSB20_FIRMWARE_VERSION=\"0.2.0\""`. PlatformIO 6.1.19's
  > `run` has **no `--project-option` flag** (it exists only on `pio ci` and `project init`), so the
  > command failed outright — which is why no build ever carried a version other than the default.
  > Two near misses to know about if you are tempted to hand-roll it again: `--project-option` would
  > have *replaced* the env's `build_flags` rather than extending them, and `PLATFORMIO_BUILD_FLAGS`
  > genuinely does replace them — either one silently drops `-DUSE_MOCK_METER=0`, `-DUSE_OLED=1` and
  > `-DUSE_WIFI=1` and ships a **mock-meter build under a release version**. Hence a dedicated env
  > that re-includes its parent's flags by name, a `static_assert` that turns an unstamped release
  > build into a compile error, and `code/tests/test_release.py` guarding both.

  To build a release candidate locally (stamped, publishes nothing):
  ```
  SB20_VERSION=0.1.0 pio run -e esp32c3-oled-live-release
  ```
- **Gate:** a board is only "known-good" once it passes the acceptance card —
  `python code/scripts/qa_board.py --port <COM> --env esp32c3-oled-live-release --connect` (flashes, then
  confirms off-air it advertises as the spoof crank, answers `/status` healthily, emits decodable CPS).
  QA one board at a time (two `Stages 62144` advertisers are indistinguishable to the scan).

## Cutting a release

The artifact is built by CI and the **hardware gate decides whether it ships** — so the GitHub release
is created as a **draft** and only published once a board has passed the acceptance card. CI can prove
the image is well-formed and correctly stamped; it cannot prove a bike accepts it.

1. Land everything on `main` (PR → green CI → merge), real-data-first for any new meter (its golden
   vector from a committed `/diag` capture — [beta-program.md](../code/findings/beta-program.md)).
2. **Write the release notes** — add a `## <version>` section to [`RELEASES.md`](../RELEASES.md).
   This is a precondition, not a courtesy: the workflow publishes that section as the release's
   description and **fails the build if it is missing or empty**.
3. **Build a candidate first** (`workflow_dispatch` on the release workflow, or the local command
   above) and **flash it to a board**. Nothing is tagged yet, so a candidate that fails costs only a
   re-flash.
4. **QA-gate it on hardware** — `python code/scripts/qa_board.py --port <COM> --env
   esp32c3-oled-live-release --connect`, then confirm `/status` reports the version you stamped (not
   `0.0.0-dev`). Keep the prior known-good `.bin` for rollback.
5. **Ride it.** For the C3 spoof the acceptance ride is session 14's **G1** (the bike pairs and reads
   the rider's pedals through the spoof); erg claims need **G2**. A release that has not been on a
   bike is a candidate, whatever its version says.
6. **Tag it** — `git tag v0.1.0 && git push origin v0.1.0`. The workflow re-runs the full gate, builds
   the stamped image, verifies the stamp is in the binary, and opens a **draft** release with the
   `.bin` + `SHA256SUMS`.
7. **Publish the draft** once steps 4–5 are green, and log the version + what changed in
   `decisions.md`; that version string is the fleet's new target.

## Pushing it to the fleet — two channels

### Interim (works today): authenticated push-OTA, per board

We push to each board over the tester's LAN — no backend needed. The board's ArduinoOTA is
**authenticated** (the lockdown PR); the password is `OTA_PASSWORD` in the gitignored
`firmware\ota_secret.h` (regenerate with `tools\secrets-sync-ota.ps1`):

```
firmware\flash.ps1                 # OTA: RSSI pre-flight + espota -a <password> + reboot verify
```

Caveats: needs the board reachable (same LAN / a tunnel) and RSSI > −72 dBm; on a multi-NIC host pass
the explicit host IP (`tools\doctor.ps1 -BoardIp <ip>` prints candidates). This is maintainer-driven,
one board at a time — fine for ~10 testers, but it does need each board reachable when we push.

### Future (designed; backend-blocked): signed-pull, fleet-wide

The device polls a small JSON **manifest** and updates itself — no inbound reach needed (the right
model once testers are behind home NAT). Already built and host-tested in firmware
([ota-update-plan.md](../code/findings/ota-update-plan.md)): `OtaManifest` (version/url/size/blake2b/
sig) → `shouldUpdate(currentVersion, …)` against the board's `FIRMWARE_VERSION` → `OtaVerify`
(ed25519 over a BLAKE2b digest, vendored monocypher) → apply. **Blocked on owner-side infra:**
(a) stand up the manifest+image host (GitHub release or the unRAID box), (b) `python
code/scripts/ota_sign.py keygen` for the offline signing key + bake its public key into the build,
(c) wire `OtaUpdater` into `main.cpp` (the consumer) + a local "force a check now" route. The version
stamping that this needs is already in place (`/status` `version`).

## Knowing who's on what

The fleet's state is just every board's `/status` `version`. The [ride feedback form](RIDE-FEEDBACK-FORM.md)
captures it per ride; a board still on an old version after a push means the OTA didn't land (RSSI /
not reachable) — re-push.

## Rollback

Keep the last known-good `.bin` per version. To roll a board back, push that `.bin` with `flash.ps1`
(push channel) — the signed-pull channel only moves *forward* (strict newer), so rollback is always the
push path.
