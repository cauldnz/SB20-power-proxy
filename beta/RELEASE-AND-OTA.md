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

## Public releases — a tagged release anyone can install (added 2026-11-05)

The section above was written for **pre-flashed boards we hand to testers**. The repo is public, and
an outside user has already built from a raw commit SHA because there was no release to install
(#382 → `decisions.md` 2026-11-05). So a release is now also a **public artefact**, not just an
internal known-good marker.

- **Env:** `esp32c3-oled-live-ota` — the shipping C3 build (CLAUDE.md §Commands). It is a superset of
  the `esp32c3-oled-live` named above: push-OTA compiles in but **self-disables** when
  `firmware/ota_secret.h` is absent, which it always is for anyone outside this checkout (the gitignored
  secret). A public build therefore carries no usable inbound update path, which is the right default.
- **Version:** the tag is the `SB20_FIRMWARE_VERSION` in `Config.h` (`v0.1.0` = the 0.1.0 already baked
  in, never yet released). Bump per the rules above; never reuse.
- **The attached image:** `python code/scripts/build_factory_bin.py --env esp32c3-oled-live-ota
  --version <tag>` merges bootloader + partitions + boot_app0 + app into one
  `firmware/webflash/firmware-factory.bin` (~1.4 MB) written at offset `0x0`. Attach *that* to the
  release — it is the only image a user can flash in one step, and the only one the browser installer
  accepts. Build it from **merged `origin/main`**, never a feature branch (CLAUDE.md flash hygiene).
- **Gate:** `qa_board.py` on a real board before the release is announced, same as a tester board.
  A public release that does not pair is worse than no release.

### Hosting the browser installer

ESP Web Tools needs **HTTPS**, so `firmware/webflash/index.html` has to be served, not opened from
disk. Publish **all three files together** — `index.html`, `manifest.json` and
`firmware-factory.bin` — from one origin on GitHub Pages (a `gh-pages` branch; the bin is gitignored
on `main` on purpose and lives only there). Same-origin keeps the image fetch simple.

Do **not** point `manifest.json`'s `path` at a release-asset URL instead: asset downloads redirect to
`objects.githubusercontent.com`, whose CORS headers are not something we control, so the installer can
fail to fetch the image for reasons invisible to the user. The release asset is the thing people
download and flash with `esptool`; Pages is the thing the Install button reads.

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
