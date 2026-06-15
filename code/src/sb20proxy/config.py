"""Proxy configuration — a TOML file instead of a wall of CLI flags.

`ProxyConfig` captures everything a live run needs (which meter to read, which crank
id to spoof, which sticks, and the correction), loads from TOML, validates, and builds
the correction transform. The `sb20proxy` console command (see `cli.py`) drives it.

See `sb20proxy.example.toml` for an annotated example.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from sb20proxy.transform import IdentityTransform, PowerTransform, ScaleOffsetTransform

try:  # Python 3.11+
    import tomllib
except ModuleNotFoundError:  # Python 3.10
    import tomli as tomllib


@dataclass
class ProxyConfig:
    """Everything the live proxy needs. Defaults match the Stages/ANT+ contract."""

    meter_id: int                       # ANT+ id of the source power meter (required)
    spoof_id: int = 62144               # ANT+ id to broadcast as (the Stages L crank)
    source_label: str = "meter"
    source_usb_index: int = 0           # which ANT+ stick listens to the meter
    target_usb_index: int = 1           # which ANT+ stick broadcasts the spoofed crank
    commons_every: int = 120            # pages between identity bursts (120 ≈ 30 s @ 4 Hz)
    stale_timeout_s: float = 5.0        # stop relaying if the meter goes silent this long
    # correction (use a fitted profile OR a linear scale/offset, not both)
    profile: str | None = None
    scale: float = 1.0
    offset: float = 0.0

    def validate(self) -> list[str]:
        """Return a list of problems (empty = valid)."""
        errors: list[str] = []
        if not (0 < self.meter_id <= 0xFFFF):
            errors.append(f"meter_id {self.meter_id} out of range 1..65535")
        if not (0 < self.spoof_id <= 0xFFFF):
            errors.append(f"spoof_id {self.spoof_id} out of range 1..65535")
        if self.source_usb_index == self.target_usb_index:
            errors.append("source_usb_index and target_usb_index must differ (two sticks)")
        if self.commons_every < 0:
            errors.append("commons_every must be >= 0")
        if self.stale_timeout_s <= 0:
            errors.append("stale_timeout_s must be > 0")
        if self.profile and (self.scale != 1.0 or self.offset != 0.0):
            errors.append("set either [correction] profile OR scale/offset, not both")
        if self.profile and not Path(self.profile).exists():
            errors.append(f"correction profile not found: {self.profile}")
        return errors

    def build_transform(self) -> PowerTransform:
        if self.profile:
            from sb20proxy.calibration import load_transform
            return load_transform(self.profile)
        if self.scale != 1.0 or self.offset != 0.0:
            return ScaleOffsetTransform(scale=self.scale, offset=self.offset)
        return IdentityTransform()


def load_config(path: str | Path) -> ProxyConfig:
    """Parse a proxy TOML into a ProxyConfig (does not validate — call .validate())."""
    with open(path, "rb") as fh:
        raw = tomllib.load(fh)
    proxy = raw.get("proxy", {})
    radio = raw.get("radio", {})
    corr = raw.get("correction", {})
    if "meter_id" not in proxy:
        raise ValueError("config [proxy] must set meter_id")
    return ProxyConfig(
        meter_id=int(proxy["meter_id"]),
        spoof_id=int(proxy.get("spoof_id", 62144)),
        source_label=str(proxy.get("source_label", "meter")),
        stale_timeout_s=float(proxy.get("stale_timeout_s", 5.0)),
        source_usb_index=int(radio.get("source_usb_index", 0)),
        target_usb_index=int(radio.get("target_usb_index", 1)),
        commons_every=int(radio.get("commons_every", 120)),
        profile=corr.get("profile"),
        scale=float(corr.get("scale", 1.0)),
        offset=float(corr.get("offset", 0.0)),
    )
