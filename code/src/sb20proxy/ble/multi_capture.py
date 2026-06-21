"""Pure helpers for the multi-device BLE capture.

The bleak I/O lives in ``scripts/capture_ble_multi.py`` (the hardware seam); the
device-spec parsing, the per-kind subscription map, and the notification → JSONL
``data`` decoder are here so they're host-tested. Decoders are *reused* from the
validated codecs (:mod:`sb20proxy.ble.cps`, :mod:`sb20proxy.ble.ftms`).

A capture watches several BLE devices at once on one clock (the SB20's FTMS feed,
a Stages crank's CPS, an Assioma's CPS, …) and tags every notification with the
device ``label`` — so the SQLite analysis layer keys each meter's power stream by
label and reconciles them. See ``findings/traffic-observability.md``.
"""

from __future__ import annotations

from dataclasses import dataclass

from . import cps, ftms


def sig_uuid(short: int) -> str:
    """16-bit SIG UUID expanded to the 128-bit form bleak compares on."""
    return f"0000{short:04x}-0000-1000-8000-00805f9b34fb"


# kind -> [(characteristic uuid, our label, indicate?)] to subscribe. "all" is
# handled specially (subscribe to every notify/indicate characteristic) — use it
# for the SB20 to catch FTMS + the shifter + anything else in one go.
_SUBSCRIPTIONS: dict[str, list[tuple[str, str, bool]]] = {
    "ftms": [
        (sig_uuid(ftms.UUID_INDOOR_BIKE_DATA), "indoor_bike_data", False),
        (sig_uuid(ftms.UUID_FTMS_STATUS), "fitness_machine_status", False),
        (sig_uuid(ftms.UUID_FTMS_CONTROL_POINT), "fitness_machine_control_point", True),
    ],
    "cps": [
        (sig_uuid(cps.UUID_CP_MEASUREMENT), "cycling_power_measurement", False),
    ],
    "all": [],
}
KINDS = frozenset(_SUBSCRIPTIONS)


@dataclass(frozen=True)
class DeviceSpec:
    """One device to watch: a ``label`` (the stream key), a ``kind`` (which chars to
    subscribe), and a BLE ``address``."""

    label: str
    kind: str
    address: str


def parse_device_spec(spec: str) -> DeviceSpec:
    """Parse ``LABEL:KIND:ADDRESS`` (the address keeps its MAC colons — split is
    bounded to 2 so ``E4:AA:…`` stays intact)."""
    parts = spec.split(":", 2)
    if len(parts) != 3:
        raise ValueError(f"device spec must be LABEL:KIND:ADDRESS, got {spec!r}")
    label, kind, address = (p.strip() for p in parts)
    if not label or not address:
        raise ValueError(f"device spec needs a label and an address: {spec!r}")
    if kind not in KINDS:
        raise ValueError(f"kind must be one of {sorted(KINDS)}, got {kind!r}")
    return DeviceSpec(label=label, kind=kind, address=address)


def subscriptions_for(kind: str) -> list[tuple[str, str, bool]]:
    return _SUBSCRIPTIONS[kind]


def build_notification_data(char_label: str, raw: bytes) -> dict:
    """Decode a notification's bytes (by characteristic) into the JSONL ``data`` dict.

    Always carries ``raw_hex`` (the lossless record the SQLite importer re-decodes);
    adds human-readable decoded fields where we have a codec. Never raises — a decode
    problem is recorded as ``decode_error`` so the capture keeps logging.
    """
    out: dict = {"raw_hex": raw.hex()}
    try:
        if char_label == "indoor_bike_data":
            d = ftms.decode_indoor_bike_data(raw)
            out.update(power_w=d.power_w, cadence_rpm=d.cadence_rpm,
                       speed_kmh=d.speed_kmh, flags=d.flags)
        elif char_label == "cycling_power_measurement":
            m = cps.decode_cps_measurement(raw)
            out.update(power_w=m.power_w, pedal_balance=m.pedal_balance,
                       cumulative_crank_revs=m.cumulative_crank_revs,
                       last_crank_event_time=m.last_crank_event_time, flags=m.flags)
        elif char_label in ("fitness_machine_control_point", "control_point"):
            msg = ftms.decode_control_point(raw)
            if isinstance(msg, ftms.ControlPointResponse):
                out.update(is_response=True, request_opcode=msg.request_opcode,
                           result=msg.result, result_name=msg.result_name)
        elif char_label == "fitness_machine_status":
            st = ftms.decode_fitness_machine_status(raw)
            if st is not None:
                out["opcode"] = st.opcode
                if st.target_power_w is not None:
                    out["target_power_w"] = st.target_power_w
        else:  # unknown / exploratory char (e.g. the shifter) — keep raw + a readable view
            out["ascii"] = raw.decode("latin-1", errors="replace")
    except Exception as exc:  # noqa: BLE001 — a decode bug must never drop a record
        out["decode_error"] = str(exc)
    return out
