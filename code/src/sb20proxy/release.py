"""Release helpers — turn a git tag into the version stamp a release build carries.

Cutting a release is tag-driven (see `beta/RELEASE-AND-OTA.md`): pushing `v0.1.0` makes
`.github/workflows/release.yml` build `env:esp32c3-oled-live-release` with
``SB20_VERSION=0.1.0``, which lands in the firmware as ``Config::FIRMWARE_VERSION`` and is surfaced
at ``/status`` and on the ``/diag`` header.

Why this lives in tested Python rather than in the workflow's shell: the device's update decision is
a *strict* newer-than compare against that stamp (``OtaManifest::shouldUpdate``), so a malformed,
reused or prerelease version is a shipping hazard no board would report as an error — it would
simply never update. The compare below is a faithful mirror of the firmware's
``parseVersion``/``isNewerVersion`` (``firmware/lib/proxy/OtaManifest.h``), so the release tooling
refuses what the fleet would silently ignore.
"""

from __future__ import annotations

import re
from dataclasses import dataclass

# Config.h's default stamp. Every dev and CI build carries it, so /status never makes a desk build
# look like a shipped one, and any real release (>= 0.0.1) is strictly newer than it.
DEV_VERSION = "0.0.0-dev"

# The headless C3-OLED build, published since 0.1.0. Its firmware is byte-identical to
# `esp32c3-oled-live` apart from the version stamp; the `-ota` env differs only in upload_protocol,
# so this single image serves both the USB and the push-OTA flashing paths.
RELEASE_ENV = "esp32c3-oled-live-release"

# The full-display head unit (Guition JC3248W535), published since 0.1.1.
GUITION_RELEASE_ENV = "esp32-guition-live-release"


@dataclass(frozen=True)
class ReleaseTarget:
    """One board a release publishes an image for."""

    env: str
    label: str  # shown in the installer manifest
    chip_family: str  # the ESP Web Tools chipFamily
    first_version: str  # the first release that carries this board
    factory_name: str  # the merged flash-at-0x0 image
    manifest_name: str  # the ESP Web Tools manifest for that image


# `first_version` keeps a tag honest about its own scope: v0.1.0 was scoped to the C3 alone, and it
# must stay that way even if the tag is cut from a commit that already knows about the Guition.
RELEASE_TARGETS = (
    ReleaseTarget(
        env=RELEASE_ENV,
        label="C3 OLED, live",
        chip_family="ESP32-C3",
        first_version="0.1.0",
        factory_name="firmware-factory.bin",
        manifest_name="manifest.json",
    ),
    ReleaseTarget(
        env=GUITION_RELEASE_ENV,
        label="Guition 3.5-inch head unit, live",
        chip_family="ESP32-S3",
        first_version="0.1.1",
        factory_name="firmware-factory-guition.bin",
        manifest_name="manifest-guition.json",
    ),
)

# A release tag is exactly `vMAJOR.MINOR.PATCH` — no prerelease suffix, no build metadata. See
# `version_from_tag` for why a suffix is refused rather than stripped.
_RELEASE_TAG = re.compile(r"^(?:refs/tags/)?v(\d+)\.(\d+)\.(\d+)$")

_LEADING_INT = re.compile(r"\s*([+-]?\d+)")


def _strtol(part: str) -> int:
    """Mirror C's ``strtol`` on a version part: leading digits, else 0 (``"0-rc1"`` -> 0)."""
    m = _LEADING_INT.match(part)
    return int(m.group(1)) if m else 0


def parse_version(version: str) -> tuple[int, int, int]:
    """Mirror the firmware's ``parseVersion``: up to three dotted ints, missing 0, extra ignored.

    Deliberately lossy the same way the firmware is — that lossiness is why prerelease tags
    are refused (see `version_from_tag`), so it must be reproduced rather than improved on here.
    """
    out = [0, 0, 0]
    for i, part in enumerate(version.split(".")[:3]):
        out[i] = _strtol(part)
    return out[0], out[1], out[2]


def is_newer_version(current: str, candidate: str) -> bool:
    """Mirror ``isNewerVersion``: is `candidate` strictly newer? Equal is not newer."""
    return parse_version(candidate) > parse_version(current)


def version_from_tag(tag: str) -> str:
    """``"v0.1.0"`` (or ``"refs/tags/v0.1.0"``) -> ``"0.1.0"``; ``ValueError`` on anything else.

    Prerelease / build-metadata tags (``v0.1.0-rc1``, ``v0.1.0+build3``) are refused, not stripped:
    the firmware's compare parses ``"0.1.0-rc1"`` as ``(0, 1, 0)``, identical to ``"0.1.0"``, so a
    board flashed with an rc would never accept the final release over OTA. Build release candidates
    through the workflow's dry-run path instead, which publishes nothing and stamps no version.
    """
    m = _RELEASE_TAG.match(tag.strip())
    if not m:
        raise ValueError(
            f"{tag!r} is not a release tag. Expected vMAJOR.MINOR.PATCH (e.g. v0.1.0) with no "
            "prerelease or build suffix — the firmware's OTA compare cannot tell v0.1.0-rc1 from "
            "v0.1.0, so a prerelease tag would strand every board that installed it."
        )
    return ".".join(m.groups())


def release_notes_section(text: str, version: str) -> str:
    """The ``RELEASES.md`` body for `version`, without its heading. Raise if there isn't one.

    The workflow publishes this as the GitHub release's notes, which makes a changelog entry a
    *precondition* for cutting a release rather than a courtesy: a tag with no entry fails the build
    instead of producing a release whose only description is the tag name. Matches a heading of the
    form ``## 0.1.0`` or ``## v0.1.0``, with anything after it (a date, a title) ignored.
    """
    # `.*$` consumes the rest of the heading line (a date, a title) so it cannot leak into the body.
    # The lookahead stops `0.1` from matching a `## 0.1.0` heading.
    heading = re.compile(rf"^##\s+v?{re.escape(version)}(?![\w.]).*$", re.MULTILINE)
    m = heading.search(text)
    if not m:
        raise ValueError(
            f"RELEASES.md has no '## {version}' section. Add the release notes for {version} "
            "before tagging — the GitHub release is published from them."
        )
    rest = text[m.end() :]
    nxt = re.search(r"^##\s", rest, re.MULTILINE)
    body = (rest[: nxt.start()] if nxt else rest).strip()
    if not body:
        raise ValueError(
            f"RELEASES.md's '## {version}' section is empty — write the release notes."
        )
    return body


def targets_for(version: str) -> tuple[ReleaseTarget, ...]:
    """The boards a given release publishes, oldest first."""
    return tuple(t for t in RELEASE_TARGETS if not is_newer_version(version, t.first_version))


def artifact_name(version: str, env: str = RELEASE_ENV) -> str:
    """The published firmware filename, e.g. ``sb20proxy-esp32c3-oled-live-release-0.1.0.bin``.

    Deterministic and version-bearing so a board's ``/status`` version can always be traced back to
    the exact downloadable image (and so a kept rollback image names itself).
    """
    return f"sb20proxy-{env}-{version}.bin"
