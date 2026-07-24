"""PlatformIO pre-build hook: stamp the git short SHA + build time into the firmware.

The semver in Config.h (SB20_FIRMWARE_VERSION) is bumped by hand only at release time, so
every dev build reports the same "0.1.0" and you can't tell which build is on a board. This
injects -DSB20_BUILD_SHA / -DSB20_BUILD_TIME from git at compile time, so /status, /diag and the
screen can show a UNIQUE build id (e.g. "0.1.0 9a3f2c1d0+dirty 2026-07-25T14:30").

Wired in via `extra_scripts = pre:scripts/build_version.py` in platformio.ini's [env].
"""
import subprocess
from datetime import datetime

Import("env")  # noqa: F821 — injected by PlatformIO/SCons

_PROJ = env.subst("$PROJECT_DIR")  # noqa: F821


def _git(args, default):
    try:
        return (
            subprocess.check_output(["git"] + args, cwd=_PROJ, stderr=subprocess.DEVNULL)
            .decode()
            .strip()
        )
    except Exception:  # git missing / not a repo / detached — never fail the build
        return default


_sha = _git(["rev-parse", "--short=9", "HEAD"], "nogit")
try:
    _dirty = subprocess.call(["git", "diff", "--quiet"], cwd=_PROJ) != 0
except Exception:
    _dirty = False
if _dirty:
    _sha += "+dirty"

# No space in the timestamp: keeps the -D value a single token, no shell/compiler quoting surprises.
_build_time = datetime.now().strftime("%Y-%m-%dT%H:%M")

env.Append(  # noqa: F821
    CPPDEFINES=[
        ("SB20_BUILD_SHA", env.StringifyMacro(_sha)),  # noqa: F821
        ("SB20_BUILD_TIME", env.StringifyMacro(_build_time)),  # noqa: F821
    ]
)
print("[build_version] SB20_BUILD_SHA=%s SB20_BUILD_TIME=%s" % (_sha, _build_time))
