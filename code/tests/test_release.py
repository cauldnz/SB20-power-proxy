"""The release mechanics: tag -> version stamp -> the one published artifact.

Two things are guarded here, both of which would otherwise fail silently *after* a release shipped:

1. `sb20proxy.release`'s version logic is a faithful mirror of the firmware's OTA compare
   (`firmware/lib/proxy/OtaManifest.h`), so the release tooling refuses versions the fleet would
   quietly ignore rather than reject.
2. `firmware/platformio.ini`'s release env really does inherit the ride env's build flags. It has to
   redefine `build_flags` to append the version define, which shadows `extends`; if a future edit to
   `esp32c3-oled-live` stops reaching it, the build loses `-DUSE_MOCK_METER=0` / `-DUSE_OLED`
   / `-DUSE_WIFI` and we publish a mock-meter build under a release version.
"""

from __future__ import annotations

import re
from pathlib import Path

import pytest

from sb20proxy.release import (
    DEV_VERSION,
    GUITION_RELEASE_ENV,
    RELEASE_ENV,
    RELEASE_TARGETS,
    artifact_name,
    is_newer_version,
    parse_version,
    release_notes_section,
    targets_for,
    version_from_tag,
)

_REPO = Path(__file__).resolve().parents[2]
_PIO_INI = _REPO / "firmware" / "platformio.ini"
_CONFIG_H = _REPO / "firmware" / "lib" / "proxy" / "Config.h"
_RELEASES = _REPO / "RELEASES.md"
_RELEASE_YML = _REPO / ".github" / "workflows" / "release.yml"

# The flags that make the shipped build the *real* spoof: a live meter, the OLED and WiFi/OTA.
# Losing any of them to a build_flags shadowing bug is the failure this module exists to prevent.
_RIDE_FLAGS = ("-DUSE_MOCK_METER=0", "-DUSE_OLED=1", "-DUSE_WIFI=1")


# --- tag -> version ---------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("tag", "expected"),
    [
        ("v0.1.0", "0.1.0"),
        ("refs/tags/v0.1.0", "0.1.0"),  # what GITHUB_REF carries
        ("  v1.2.3  ", "1.2.3"),
        ("v10.20.30", "10.20.30"),
        ("v0.0.1", "0.0.1"),
    ],
)
def test_version_from_tag_accepts_release_tags(tag: str, expected: str) -> None:
    assert version_from_tag(tag) == expected


@pytest.mark.parametrize(
    "tag",
    [
        "0.1.0",  # no v
        "v0.1",  # not three parts
        "v0.1.0.1",  # four parts
        "v0.1.x",
        "",
        "main",
        "v0.1.0-rc1",  # prerelease: the firmware cannot tell it from v0.1.0
        "v0.1.0+build3",  # build metadata: likewise
        "V0.1.0",  # capital V is not the project's tag form
    ],
)
def test_version_from_tag_refuses_everything_else(tag: str) -> None:
    with pytest.raises(ValueError):
        version_from_tag(tag)


def test_prerelease_rejection_has_a_real_reason() -> None:
    """The justification for refusing `v0.1.0-rc1`, asserted rather than left in prose.

    The firmware parses both to (0, 1, 0), so a board on the rc would never accept the final release
    over OTA. That is why `version_from_tag` refuses the tag instead of stripping the suffix.
    """
    assert parse_version("0.1.0-rc1") == parse_version("0.1.0")
    assert not is_newer_version("0.1.0-rc1", "0.1.0")


# --- the version compare, mirroring OtaManifest.h ---------------------------------------------


@pytest.mark.parametrize(
    ("version", "expected"),
    [
        ("0.1.0", (0, 1, 0)),
        ("1.2.3", (1, 2, 3)),
        ("2", (2, 0, 0)),  # missing parts are 0
        ("2.5", (2, 5, 0)),
        ("1.2.3.4", (1, 2, 3)),  # extra parts ignored
        ("0.0.0-dev", (0, 0, 0)),  # strtol stops at the non-digit
        ("", (0, 0, 0)),
    ],
)
def test_parse_version_mirrors_the_firmware(version: str, expected: tuple[int, int, int]) -> None:
    assert parse_version(version) == expected


@pytest.mark.parametrize(
    ("current", "candidate", "newer"),
    [
        ("0.1.0", "0.2.0", True),
        ("0.1.0", "0.1.1", True),
        ("0.1.0", "1.0.0", True),
        ("0.1.0", "0.1.0", False),  # equal is not newer — never reuse a version
        ("0.2.0", "0.1.0", False),  # the pull channel only moves forward
        ("0.9.0", "0.10.0", True),  # numeric, not lexical
    ],
)
def test_is_newer_version(current: str, candidate: str, newer: bool) -> None:
    assert is_newer_version(current, candidate) is newer


def test_any_release_is_newer_than_a_dev_build() -> None:
    """A board on a desk build must accept the first real release; a release must outrank dev."""
    assert is_newer_version(DEV_VERSION, "0.0.1")
    assert is_newer_version(DEV_VERSION, "0.1.0")
    assert not is_newer_version("0.1.0", DEV_VERSION)


# --- the published artifact -------------------------------------------------------------------


def test_artifact_name_is_deterministic_and_version_bearing() -> None:
    assert artifact_name("0.1.0") == "sb20proxy-esp32c3-oled-live-release-0.1.0.bin"
    assert version_from_tag("v0.1.0") in artifact_name(version_from_tag("v0.1.0"))


# --- the firmware side: the release env must really inherit the ride flags ---------------------


def _release_env_block() -> str:
    """The `[env:esp32c3-oled-live-release]` section body from platformio.ini."""
    text = _PIO_INI.read_text(encoding="utf-8")
    m = re.search(
        rf"^\[env:{re.escape(RELEASE_ENV)}\]\n(.*?)(?=^\[|\Z)",
        text,
        re.MULTILINE | re.DOTALL,
    )
    assert m, f"platformio.ini has no [env:{RELEASE_ENV}] section — release.yml builds it"
    return m.group(1)


def test_release_env_exists_and_extends_the_proven_ride_env() -> None:
    assert "extends = env:esp32c3-oled-live" in _release_env_block()


def test_release_env_reinherits_the_parent_build_flags() -> None:
    """Redefining build_flags shadows `extends`, so the parent's flags need re-including."""
    assert "${env:esp32c3-oled-live.build_flags}" in _release_env_block()


def test_release_env_stamps_the_version_from_the_environment() -> None:
    block = _release_env_block()
    assert "-DSB20_FIRMWARE_VERSION=" in block
    assert "${sysenv.SB20_VERSION}" in block, "release.yml passes the tag's version as SB20_VERSION"


def test_the_ride_env_still_carries_the_flags_the_release_inherits() -> None:
    """If a flag is renamed in the parent, this fails here rather than on a rider's bike."""
    text = _PIO_INI.read_text(encoding="utf-8")
    m = re.search(r"^\[env:esp32c3-oled-live\]\n(.*?)(?=^\[|\Z)", text, re.MULTILINE | re.DOTALL)
    assert m, "platformio.ini has no [env:esp32c3-oled-live] — the shipped build's parent"
    for flag in _RIDE_FLAGS:
        assert flag in m.group(1), f"the shipped C3 ride build lost {flag}"


def test_config_h_dev_default_matches_the_python_constant() -> None:
    """`/status` must distinguish a desk build from a release; both sides agree on the sentinel."""
    text = _CONFIG_H.read_text(encoding="utf-8")
    assert f'#define SB20_FIRMWARE_VERSION "{DEV_VERSION}"' in text


def test_config_h_refuses_an_unstamped_release_build() -> None:
    """The static_assert makes a mis-invoked release build a compile error, not a bad artifact."""
    text = _CONFIG_H.read_text(encoding="utf-8")
    assert "static_assert(SB20_FIRMWARE_VERSION[0] >= '0'" in text


# --- release notes are a precondition for publishing ------------------------------------------

_RELEASES_SAMPLE = """# Changelog

preamble that belongs to no version

## 0.2.0 — 2026-11-20

the newer one

## v0.1.0 — 2026-11-06

the first one
- a bullet

## 0.0.9

ancient
"""


def test_release_notes_section_extracts_one_version() -> None:
    assert release_notes_section(_RELEASES_SAMPLE, "0.2.0") == "the newer one"
    assert release_notes_section(_RELEASES_SAMPLE, "0.1.0") == "the first one\n- a bullet"
    assert release_notes_section(_RELEASES_SAMPLE, "0.0.9") == "ancient"


def test_release_notes_section_does_not_bleed_into_the_next_version() -> None:
    """The nearest failure mode: publishing 0.2.0's notes with the whole file appended."""
    assert "the first one" not in release_notes_section(_RELEASES_SAMPLE, "0.2.0")
    assert "preamble" not in release_notes_section(_RELEASES_SAMPLE, "0.1.0")


@pytest.mark.parametrize("version", ["0.3.0", "1.0.0", "0.1"])
def test_release_notes_section_refuses_a_missing_entry(version: str) -> None:
    with pytest.raises(ValueError, match="RELEASES"):
        release_notes_section(_RELEASES_SAMPLE, version)


def test_release_notes_section_refuses_an_empty_entry() -> None:
    with pytest.raises(ValueError, match="empty"):
        release_notes_section("# Changelog\n\n## 0.1.0\n\n## 0.0.1\n\nold\n", "0.1.0")


def test_the_real_changelog_has_notes_for_the_first_release() -> None:
    """The workflow publishes these; a tag without them fails the build, so prove it exists."""
    body = release_notes_section(_RELEASES.read_text(encoding="utf-8"), "0.1.0")
    assert len(body) > 200, "0.1.0's notes are too thin to publish as a release description"


# --- the workflow wiring ----------------------------------------------------------------------


def test_release_workflow_builds_the_release_env_and_nothing_else() -> None:
    """A release must not be cut from the unstamped ride env; that was the whole v0.1.0 bug."""
    text = _RELEASE_YML.read_text(encoding="utf-8")
    assert f"pio run -e {RELEASE_ENV}" in text
    assert "tags:" in text, "the release trigger is a tag push"


def test_release_workflow_verifies_the_stamp_landed() -> None:
    """The published image must contain the version it is named for, not the default."""
    text = _RELEASE_YML.read_text(encoding="utf-8")
    assert DEV_VERSION in text, "the workflow must assert the dev default is not in the image"


def test_release_workflow_attaches_the_browser_installable_image() -> None:
    """The README points a visitor at firmware-factory.bin, so the release must actually carry it.

    The app image alone cannot be flashed in one step and the browser installer will not accept it
    (beta/RELEASE-AND-OTA.md "Public releases"), so a release missing this asset is a release whose
    own install instructions do not work.
    """
    text = _RELEASE_YML.read_text(encoding="utf-8")
    assert "build_factory_bin.py" in text, "the factory image is never built"
    assert "firmware-factory.bin" in text, "the factory image is never attached"
    assert "manifest.json" in text, "the ESP Web Tools manifest is never attached"
    # Both publish paths: the tagged release and the dry-run candidate upload.
    assert text.count("firmware-factory.bin") >= 3


def test_release_workflow_checksums_every_published_asset() -> None:
    """A published binary nobody can verify is worse than no checksum at all."""
    text = _RELEASE_YML.read_text(encoding="utf-8")
    start = text.index("List and checksum everything")
    step = text[start : text.index("Upload the release candidate")]
    assert "sha256sum $ASSETS" in step
    for asset in (
        "steps.ver.outputs.artifact",
        "firmware-factory.bin",
        "manifest.json",
        "steps.ver.outputs.guition_artifact",
        "firmware-factory-guition.bin",
        "manifest-guition.json",
    ):
        assert asset in step, f"{asset} is published but not in SHA256SUMS"


def test_release_workflow_builds_the_guition_on_its_own_toolchain() -> None:
    """Sharing a core dir with the C3's platform breaks the build (FRAMEWORK_DIR None)."""
    text = _RELEASE_YML.read_text(encoding="utf-8")
    core_dir = 'PLATFORMIO_CORE_DIR="$HOME/.platformio-pioarduino"'
    assert f"{core_dir} pio run -e {GUITION_RELEASE_ENV}" in text


def test_release_publishes_the_list_it_checksummed() -> None:
    """One list drives both, so an asset cannot be published without appearing in SHA256SUMS."""
    assert "${{ steps.assets.outputs.list }}" in _RELEASE_YML.read_text(encoding="utf-8")


# --- the Guition head unit: a second published board, from 0.1.1 ------------------------------

# What makes the Guition image the real head unit. Each one is a silent failure if it is lost to a
# shadowed build_flags: a mock meter under a release version, a blank panel, or the status LED
# driving this board's touch SCL.
_GUITION_RIDE_FLAGS = (
    "-DUSE_MOCK_METER=0",
    "-DLCD_DRIVER_GUITION=1",
    "-DLCD_LVGL_FULL_REFRESH=1",
    "-DSB20_STATUS_LED_PIN=2",
)


def _env_block(name: str) -> str:
    text = _PIO_INI.read_text(encoding="utf-8")
    m = re.search(rf"^\[env:{re.escape(name)}\]\n(.*?)(?=^\[|\Z)", text, re.MULTILINE | re.DOTALL)
    assert m, f"platformio.ini has no [env:{name}]"
    return m.group(1)


def test_v0_1_0_stays_c3_only_whatever_commit_it_is_cut_from() -> None:
    """The owner scoped 0.1.0 to the C3; the Guition must not ride along on a later commit's tag."""
    assert [t.env for t in targets_for("0.1.0")] == [RELEASE_ENV]


@pytest.mark.parametrize("version", ["0.1.1", "0.2.0", "1.0.0"])
def test_releases_from_0_1_1_publish_both_boards(version: str) -> None:
    assert [t.env for t in targets_for(version)] == [RELEASE_ENV, GUITION_RELEASE_ENV]


def test_every_target_publishes_under_its_own_names() -> None:
    """Two boards sharing a filename would overwrite one image with the other in the release."""
    assert len({t.factory_name for t in RELEASE_TARGETS}) == len(RELEASE_TARGETS)
    assert len({t.manifest_name for t in RELEASE_TARGETS}) == len(RELEASE_TARGETS)


def test_guition_release_env_is_the_ride_env_plus_the_stamp() -> None:
    block = _env_block(GUITION_RELEASE_ENV)
    assert "extends = env:esp32-guition-live" in block
    assert "${env:esp32-guition-live.build_flags}" in block
    assert "${sysenv.SB20_VERSION}" in block


def test_guition_image_header_says_16mb() -> None:
    """board_build.flash_size does not set the header; an 8 MB one rejects the 16 MB partitions."""
    assert "board_upload.flash_size = 16MB" in _env_block("esp32-guition")


def test_guition_ride_flags_reach_the_release() -> None:
    combined = _env_block("esp32-guition") + _env_block("esp32-guition-live")
    for flag in _GUITION_RIDE_FLAGS:
        assert flag in combined, f"the Guition ride build lost {flag}"
