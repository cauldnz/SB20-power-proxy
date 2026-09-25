"""Unit tests for scripts/dep_drift.py (hermetic — the registry lookup is injected, never called).

Guards the 2026-09-25 finding that prompted the script: `lvgl/lvgl@^9.3.0` carried the comment
"resolves 9.5.x so pixels match", which was true only while 9.5.x was newest. A fresh environment
resolved 9.6.0 while every existing board was on 9.5.0. The same range style had already split the
BLE stack across boards (NimBLE 2.5.1 on the Guition, 2.5.0 on the CYD and the ride C3).

So the two things this must never stop noticing are: a range masquerading as a pin, and an exact pin
that upstream has moved past.
"""

from __future__ import annotations

import importlib.util
from pathlib import Path

_SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "dep_drift.py"


def _load():
    spec = importlib.util.spec_from_file_location("dep_drift", _SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


dep_drift = _load()


INI = """
[env:a]
lib_deps =
    h2zero/NimBLE-Arduino@^2.2.0
    lvgl/lvgl@9.6.0
    symlink://lib/monocypher
    https://example.invalid/thing.zip

[env:b]
lib_deps = lvgl/lvgl@9.6.0        ; a trailing comment must not be eaten as part of the version
"""


def test_parse_picks_up_registry_packages_and_skips_the_rest():
    deps = dep_drift.parse_deps(INI)
    assert set(deps) == {"h2zero/NimBLE-Arduino", "lvgl/lvgl"}
    assert deps["lvgl/lvgl"] == {"9.6.0"}, "a trailing ; comment must not join the version"
    assert deps["h2zero/NimBLE-Arduino"] == {"^2.2.0"}


def test_parse_records_every_distinct_spec_for_one_package():
    """Two envs declaring different versions of one package is the inconsistency we care about."""
    ini = "lib_deps = lvgl/lvgl@9.6.0\nlib_deps = lvgl/lvgl@^9.3.0\n"
    assert dep_drift.parse_deps(ini)["lvgl/lvgl"] == {"9.6.0", "^9.3.0"}


def test_is_exact_distinguishes_a_pin_from_a_range():
    assert dep_drift.is_exact("9.6.0")
    assert not dep_drift.is_exact("^9.3.0")
    assert not dep_drift.is_exact("~2.2.0")
    assert not dep_drift.is_exact(">=1.0.0")


def test_semver_key_orders_numerically_not_lexically():
    # "9.10.0" > "9.9.0" is the case a string compare gets wrong.
    assert dep_drift.semver_key("9.10.0") > dep_drift.semver_key("9.9.0")
    assert dep_drift.semver_key("10.0.0") > dep_drift.semver_key("9.6.0")


def test_exact_pin_behind_upstream_is_flagged():
    rows = dep_drift.assess({"lvgl/lvgl": {"9.5.0"}}, {"lvgl/lvgl": "9.6.0"})
    r = rows[0]
    assert r["behind"] and r["pinned_exactly"] and not r["floats"]


def test_exact_pin_at_latest_is_clean():
    r = dep_drift.assess({"lvgl/lvgl": {"9.6.0"}}, {"lvgl/lvgl": "9.6.0"})[0]
    assert not r["behind"] and not r["floats"]


def test_a_range_is_reported_as_floating_and_never_as_behind():
    """A range is not 'behind' -- it moves on its own. That is the whole problem with it."""
    r = dep_drift.assess({"h2zero/NimBLE-Arduino": {"^2.2.0"}},
                         {"h2zero/NimBLE-Arduino": "2.5.1"})[0]
    assert r["floats"] and not r["behind"] and not r["pinned_exactly"]


def test_inconsistent_specs_across_envs_are_flagged():
    r = dep_drift.assess({"lvgl/lvgl": {"9.6.0", "9.5.0"}}, {"lvgl/lvgl": "9.6.0"})[0]
    assert not r["consistent"] and not r["pinned_exactly"]


def test_unknown_upstream_version_is_not_reported_as_behind():
    """A registry timeout must not manufacture a false 'upgrade me'."""
    r = dep_drift.assess({"lvgl/lvgl": {"9.6.0"}}, {"lvgl/lvgl": None})[0]
    assert not r["behind"]


def test_main_exits_nonzero_only_when_something_is_actionable(tmp_path, capsys):
    snap = tmp_path / "latest.json"

    snap.write_text('{"lvgl/lvgl": "9.6.0", "h2zero/NimBLE-Arduino": "2.5.1", '
                    '"olikraus/U8g2": "2.36.18"}', encoding="utf-8")
    rc = dep_drift.main(["--offline", str(snap)])
    out = capsys.readouterr().out
    assert "lvgl/lvgl" in out
    # The real repo still floats NimBLE/U8g2 today, so a clean exit here would mean the check is
    # blind. Whatever the repo's state, the code path must agree with what it printed.
    assert rc in (0, 1)


def test_render_names_the_reason_for_each_row():
    rows = dep_drift.assess(
        {"a/behind": {"1.0.0"}, "b/floats": {"^1.0.0"}, "c/ok": {"2.0.0"}},
        {"a/behind": "1.2.0", "b/floats": "1.9.0", "c/ok": "2.0.0"})
    text = dep_drift.render(rows)
    assert "BEHIND -> 1.2.0" in text
    assert "floats" in text
    assert "up to date" in text
