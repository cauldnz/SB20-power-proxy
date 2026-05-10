#!/usr/bin/env python3
"""Phase 0 capture script — Assioma ANT+ traffic.

Functionally identical to 01_capture_stages.py — both subscribe to a standard
ANT+ Bike Power profile device. Kept as a separate script for clarity in
session naming and to allow Assioma-specific tweaks later if the captures
reveal differences (e.g. higher-rate fast modes, Cycling Dynamics pages).

Usage:
    python 02_capture_assioma.py --device-id 67890 --duration 900 \\
        --output ../findings/captures/D-assioma-steady-20260511-0830.jsonl
"""

from __future__ import annotations

import sys
from pathlib import Path

# Reuse the implementation from 01.
sys.path.insert(0, str(Path(__file__).parent))
from importlib.util import spec_from_file_location, module_from_spec

_spec = spec_from_file_location("capture_stages", Path(__file__).parent / "01_capture_stages.py")
assert _spec and _spec.loader
_capture_stages = module_from_spec(_spec)
_spec.loader.exec_module(_capture_stages)

main = _capture_stages.main

if __name__ == "__main__":
    raise SystemExit(main())
