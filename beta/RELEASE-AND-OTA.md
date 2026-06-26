# Releases & OTA — the known-good build and how a fix reaches the fleet

**Status: the pre-beta release runbook (2026-06-26).** Phase 4's "known-good default build + an OTA
channel" ([pre-beta-plan.md](../code/findings/pre-beta-plan.md)). Pairs with the OTA *security* design
[ota-update-plan.md](../code/findings/ota-update-plan.md) (this is the *operational* half) and the
pre-ship QA gate ([beta-program.md](../code/findings/beta-program.md) §Pre-ship QA).

The whole point of shipping pre-flashed boards is that **no board ever comes back**: we add meter
support / fix bugs and push them out by OTA. This doc defines the build we ship and how an update lands.

## The known-good default build

- **Env:** `esp32c3-oled-live` (the C3-OLED beta board — real meter, WiFi/OTA, OLED). Built from
  `origin/main` only (never a dirty tree; CLAUDE.md flash hygiene).
- **Version:** every shipped/OTA'd build carries a **semver** in `SB20_FIRMWARE_VERSION`, surfaced at
  `/status` (`"version"`) and on the `/diag` header (so a tester report and the dashboard both show it).
  Default `0.1.0`; bump per release:
  ```
  firmware\.venv\Scripts\platformio.exe run -e esp32c3-oled-live -d firmware ^
      --project-option="build_flags=-DSB20_FIRMWARE_VERSION=\"0.2.0\""
  ```
  (or add the flag to the env in `platformio.ini` and tag the bump). **Bump = semver:** patch for a
  meter-support/bugfix OTA, minor for a feature, and never reuse a number — the device's update
  decision is a strict `isNewerVersion` compare (`OtaManifest::shouldUpdate`).
- **Gate:** a board is only "known-good" once it passes the acceptance card —
  `python code/scripts/qa_board.py --port <COM> --env esp32c3-oled-live --connect` (flashes, then
  confirms off-air it advertises as the spoof crank, answers `/status` healthily, emits decodable CPS).
  QA one board at a time (two `Stages 62144` advertisers are indistinguishable to the scan).

## Cutting a release

1. Land the fix on `main` (PR → green CI → merge), real-data-first for any new meter (its golden
   vector from a committed `/diag` capture — [beta-program.md](../code/findings/beta-program.md)).
2. **Bump `SB20_FIRMWARE_VERSION`** (above) and build `esp32c3-oled-live`.
3. **QA-gate** the resulting `.bin` on a board (`qa_board.py`). Keep the prior known-good `.bin` for
   rollback.
4. Note the version + what changed in `decisions.md`; that version string is the fleet's new target.

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
