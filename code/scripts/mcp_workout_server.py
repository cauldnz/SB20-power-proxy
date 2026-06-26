#!/usr/bin/env python3
"""mcp_workout_server.py — run the SB20 workout MCP server (forward-plan §13).

Exposes the SB20 erg as agent-drivable tools (build_workout / start / set_target / start_drive
/ status / ...) over the Model Context Protocol so Claude (or any MCP client) can compose a
structured workout and drive it live on the bike over FTMS. Productizes the session-9 ad-hoc
`ftms_workout.py` driver.

Requires the MCP SDK (the [mcp] extra): from `code/`,
    pip install -e ".[mcp,ble]"

Run (stdio transport — the default an MCP client launches):
    python code/scripts/mcp_workout_server.py --ftp 250

Register it with an MCP client (e.g. Claude Desktop / Claude Code) by pointing the client at
this command. Then: "build a 6x90s @ 430W workout with 3 min recovery, start it, and start_drive."

SAFETY: stop_drive — and any drive error or disconnect — sends FTMS Reset, returning the bike's
resistance to neutral. Always stop_drive when finished.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# Make the package importable when run straight from the repo (mirrors ftms_workout.py).
HERE = Path(__file__).resolve().parent
SRC = HERE.parent / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(
        description="Run the SB20 workout MCP server (stdio).",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument("--ftp", type=int, default=250,
                   help="rider FTP (watts) for percent-FTP / zone targets")
    p.add_argument("--scale", default="stages", help="meter scale the FTP is measured on")
    p.add_argument("--name", default="sb20-workout", help="MCP server name")
    args = p.parse_args(argv)

    try:
        from sb20proxy.mcp.server import build_server
        from sb20proxy.workout.session import WorkoutSession
    except ModuleNotFoundError as e:  # the MCP SDK isn't installed
        print(f"ERROR: {e}. Install the MCP extra:  pip install -e '.[mcp,ble]'", file=sys.stderr)
        return 2

    session = WorkoutSession(ftp_w=args.ftp, scale=args.scale)
    server = build_server(session, name=args.name)
    server.run()  # stdio transport; blocks until the client disconnects
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
