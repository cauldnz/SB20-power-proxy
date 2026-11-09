"""The browser installer stays wired to what the release workflow actually produces."""

from __future__ import annotations

from pathlib import Path

from sb20proxy.release import RELEASE_TARGETS

REPO = Path(__file__).resolve().parents[2]
PAGE = (REPO / "firmware" / "webflash" / "index.html").read_text(encoding="utf-8")
WORKFLOW = (REPO / ".github" / "workflows" / "pages.yml").read_text(encoding="utf-8")
RELEASE = (REPO / ".github" / "workflows" / "release.yml").read_text(encoding="utf-8")


def test_page_has_an_install_button_for_every_released_board():
    for target in RELEASE_TARGETS:
        assert f'manifest="{target.manifest_name}"' in PAGE, target.env


def test_workflow_serves_every_released_image():
    for target in RELEASE_TARGETS:
        assert f"--pattern {target.manifest_name}" in WORKFLOW, target.env
        assert f"--pattern {target.factory_name}" in WORKFLOW, target.env


def test_only_a_published_release_can_reach_the_page():
    assert "types: [published]" in WORKFLOW
    assert "push:" not in WORKFLOW
    assert "--draft" in RELEASE  # the release workflow leaves the publish to the owner


def test_page_gives_the_setup_address_the_firmware_serves():
    config = (REPO / "firmware" / "lib" / "proxy" / "Config.h").read_text(encoding="utf-8")
    assert 'SETUP_PORTAL_HOST = "172.29.4.1"' in config
    assert "172.29.4.1" in PAGE
    assert "192.168.4.1" not in PAGE
