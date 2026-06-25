"""Non-blocking OTLP emitter -> the POS shared observability stack (wtrmax.local).

SIGNALS, never content: emit event_type + counts + pointers (capture filename, PR #, board id)
— never raw frames / secrets / PII (shared-services rule). Every emit is best-effort and swallows
all errors, so a down / off-LAN NAS never affects the tool. See shared-services-adoption.md.
"""
from __future__ import annotations

import json
import os
import time
import urllib.request

# Home-LAN endpoint — not a secret. Override via env; set SB20_OTEL_ENABLED=0 to disable.
OTLP_LOGS_ENDPOINT = os.environ.get("SB20_OTLP_ENDPOINT", "http://wtrmax.local:4318/v1/logs")
SERVICE_NAME = os.environ.get("SB20_OTEL_SERVICE", "sb20proxy")
PLANE = "personal"
_ENABLED = os.environ.get("SB20_OTEL_ENABLED", "1") not in ("0", "false", "False", "")
_TIMEOUT_S = 2.0


def _attr(key: str, value: object) -> dict:
    return {"key": key, "value": {"stringValue": str(value)}}


def build_log_record(event_type: str, message: str, attributes: dict | None = None) -> dict:
    """Build one OTLP/HTTP log payload (pure — no network, so it's unit-testable)."""
    attrs = [_attr("event_type", event_type), _attr("plane", PLANE)]
    for k, v in (attributes or {}).items():
        attrs.append(_attr(k, v))
    return {
        "resourceLogs": [
            {
                "resource": {"attributes": [_attr("service.name", SERVICE_NAME)]},
                "scopeLogs": [
                    {
                        "logRecords": [
                            {
                                "timeUnixNano": str(time.time_ns()),
                                "severityText": "INFO",
                                "body": {"stringValue": message},
                                "attributes": attrs,
                            }
                        ]
                    }
                ],
            }
        ]
    }


def emit(
    event_type: str,
    message: str,
    attributes: dict | None = None,
    *,
    endpoint: str | None = None,
    timeout: float = _TIMEOUT_S,
) -> bool:
    """Emit a content-free signal. Best-effort + non-blocking: True on a 2xx, False otherwise, and
    NEVER raises (a down / off-LAN NAS must not affect the tool). Pass only signals — no raw
    data / secrets / PII.
    """
    if not _ENABLED:
        return False
    payload = json.dumps(build_log_record(event_type, message, attributes)).encode()
    req = urllib.request.Request(
        endpoint or OTLP_LOGS_ENDPOINT,
        data=payload,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return 200 <= resp.status < 300
    except Exception:
        return False
