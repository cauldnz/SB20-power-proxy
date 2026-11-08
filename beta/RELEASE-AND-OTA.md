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
3. **Tag it** — `git tag v0.1.0 && git push origin v0.1.0`. The workflow re-runs the full gate, builds
   the stamped image, verifies the stamp is really in the binary, and opens a **draft** release with
   the `.bin` + `SHA256SUMS`. Nothing is public yet, so a tag that fails its hardware gate costs only
   a deleted draft and a patch bump.
4. **Flash the *published* artifact — not a local rebuild.** This is the step that keeps a release
   honest: the image that gets ridden must be the image that gets downloaded. A local rebuild of the
   same commit is *not* byte-identical (`build_version.py` stamps a fresh `SB20_BUILD_TIME`), so
   QA-ing a local build and shipping CI's is testing one thing and releasing another.

   ```
   gh release download v0.1.0 -p '*.bin' -D .
   sha256sum -c SHA256SUMS                      # or: gh release download v0.1.0 -p SHA256SUMS
   mkdir -p firmware/.pio/build/esp32c3-oled-live-release
   cp sb20proxy-esp32c3-oled-live-release-0.1.0.bin \
      firmware/.pio/build/esp32c3-oled-live-release/firmware.bin
   cd firmware && .\flash.ps1 -Env esp32c3-oled-live-release -NoBuild
   ```

   `flash.ps1` takes its image from `.pio/build/<Env>/firmware.bin` and `-NoBuild` skips compiling, so
   this pushes the downloaded bytes verbatim — and the ride guard passes the release env, so no
   `-Force`. **USB caveat:** a fresh or bricked board also needs `bootloader.bin`, `partitions.bin`
   and `boot_app0.bin` in that directory, which only a local `pio run` produces — build the env once
   (`SB20_VERSION=0.1.0 pio run -e esp32c3-oled-live-release`) and then overwrite `firmware.bin`
   with the download.
5. **QA-gate it on hardware**, asserting the version rather than eyeballing it:

   ```
   python code/scripts/qa_board.py --port <COM> --env esp32c3-oled-live-release --connect        --expect-version 0.1.0
   ```

   Exit 0 = shippable. `--expect-version` makes the stamp a **critical** check on the acceptance
   card: a board on `0.0.0-dev` is reported as a desk build, a board on any other version as a stale
   image, and asking for a version without a reachable `/status` **fails** rather than skipping. Keep
   the prior known-good `.bin` for rollback.

   > Why that flag matters: `qa_board.py` and `flash_c3.py` **flash, never build** — they push
   > whatever is already in `.pio/build/<env>`. Without it, a stale or unstamped image is accepted
   > silently and the whole acceptance ride becomes evidence for a version the board is not running.
6. **Ride it.** For the C3 spoof the acceptance ride is session 14's **G1** (the bike pairs and reads
   the rider's pedals through the spoof); erg claims need **G2**. A release that has not been on a
   bike is a candidate, whatever its version says.
7. **Publish the draft** once steps 5–6 are green, and log the version + what changed in
   `decisions.md`; that version string is the fleet's new target. If the gate fails, **delete the
   draft and the tag**, fix, and cut the next patch version — never re-point a tag, because a board
   that already took the old bytes would never update (the compare is strictly-newer).

**Release candidates**, if you want a board on the build before any tag exists: run the release
workflow via `workflow_dispatch` with a version, download its workflow artifact, and flash it the
same way as step 4. It publishes nothing and creates no tag.

## Public releases — a tagged release anyone can install (added 2026-11-05)

The section above was written for **pre-flashed boards we hand to testers**. The repo is public, and
an outside user has already built from a raw commit SHA because there was no release to install
(#382 → `decisions.md` 2026-11-05). So a release is now also a **public artefact**, not just an
internal known-good marker.

- **Env:** `esp32c3-oled-live-release` — the one published env, same as the section above. There is no
  separate public build: it is `esp32c3-oled-live` plus the version stamp, so the bytes a stranger
  downloads are the bytes the bench and the bike proved.

  > Worth knowing, because it reads like an env choice and is not: **`esp32c3-oled-live-ota` compiles
  > to the same image as `esp32c3-oled-live`.** The only difference between those envs is
  > `upload_protocol = espota`, which tells PlatformIO how to *upload* and is not a build flag. Push
  > OTA is gated in the source by `#if __has_include("../../ota_secret.h")` → `#ifdef OTA_PASSWORD`
  > (`WifiLink.cpp`), i.e. by the presence of a **gitignored file**. So a public build — any env —
  > carries no usable inbound update path, because nobody outside this checkout has that secret. That
  > is fail-closed and the right default; it just is not something an env selects.
- **The Guition head unit, from `0.1.1`:** `esp32-guition-live-release` publishes alongside the C3,
  under its own names (`sb20proxy-esp32-guition-live-release-<version>.bin`,
  `firmware-factory-guition.bin`, `manifest-guition.json`). `sb20proxy.release.targets_for` decides
  which boards a version carries, so a `v0.1.0` tag stays C3-only whatever commit it is cut from.
- **Version:** the tag, stamped at build time — **not** a number baked into `Config.h`. `Config.h`'s
  default is `0.0.0-dev` precisely so an untagged build cannot pose as a release. Bump per the rules
  above; never reuse or go backwards (the workflow refuses both).
- **The attached images:** the release workflow builds and attaches **both**, so there is nothing to
  merge by hand:
  - `sb20proxy-esp32c3-oled-live-release-<version>.bin` — the app image, for push-OTA and
    `flash.ps1` / `flash_c3.py`.
  - `firmware-factory.bin` — bootloader + partitions + boot_app0 + app merged into one image written
    at offset `0x0` (`code/scripts/build_factory_bin.py`, ~1.4 MB). This is the only image a user can
    flash in one step and the only one the browser installer accepts, so it is what the README points
    a visitor at.
  - `manifest.json` — the ESP Web Tools manifest, regenerated with the release version. The copy
    committed under `firmware/webflash/` carries whatever version was last written there and is not
    the release's; take the manifest from the release.
  - `SHA256SUMS` — covering all of the above.
- **Gate:** `qa_board.py --expect-version <version>` on a real board before the release is announced,
  same as a tester board. A public release that does not pair is worse than no release — and #384 is
  exactly that failure: it advertised, and the bike silently never found it.

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
