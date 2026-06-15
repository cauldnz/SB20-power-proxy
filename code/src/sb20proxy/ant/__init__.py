"""ANT+ Bike Power wire codec for the SB20 proxy.

`decode_page` / `encode_page` are an exact inverse pair over the 8-byte ANT+
Bike Power data pages (D00001086). The proxy RECEIVES with decode (sources) and
TRANSMITS with encode (the StagesAntTarget master). See `pages` for the layouts
and the real-data provenance of every byte.
"""

from __future__ import annotations

from .pages import (
    PAGE_BATTERY_STATUS,
    PAGE_CALIBRATION,
    PAGE_CRANK_TORQUE,
    PAGE_MANUFACTURER_INFO,
    PAGE_POWER_ONLY,
    PAGE_PRODUCT_INFO,
    PAGE_TORQUE_EFFECTIVENESS,
    UnknownPage,
    decode_page,
    encode_battery_status,
    encode_calibration_response,
    encode_crank_torque,
    encode_manufacturer_info,
    encode_page,
    encode_power_only,
    encode_product_info,
    encode_torque_effectiveness,
    pedal_power_byte,
)

__all__ = [
    "decode_page",
    "encode_page",
    "encode_power_only",
    "encode_crank_torque",
    "encode_torque_effectiveness",
    "encode_manufacturer_info",
    "encode_product_info",
    "encode_battery_status",
    "encode_calibration_response",
    "pedal_power_byte",
    "UnknownPage",
    "PAGE_CALIBRATION",
    "PAGE_POWER_ONLY",
    "PAGE_CRANK_TORQUE",
    "PAGE_TORQUE_EFFECTIVENESS",
    "PAGE_MANUFACTURER_INFO",
    "PAGE_PRODUCT_INFO",
    "PAGE_BATTERY_STATUS",
]
