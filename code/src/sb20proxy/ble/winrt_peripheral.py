"""A BLE Cycling Power Service *peripheral* on Windows, via the WinRT GATT-server API.

bleak is central-only and `bless` is incompatible with Python 3.13 (it pins obsolete
`winrt-*==2.0.0b1` / imports the removed `bleak_winrt`), so the test harness advertises a
spoofed power meter using the WinRT projection directly — the same `winrt-windows-*`
packages bleak 3.x already depends on. Windows 10+ only.

This is the over-the-air counterpart of the in-process `LoopbackGatt`: where LoopbackGatt
carries CPS Measurement notifications in-process for CI, this carries them over the radio so
the ESP32 BLE *central* (firmware BleMeterClient) can connect and receive them — the hardware
half of goal #1. Import is deliberately lazy/standalone (NOT re-exported from
``sb20proxy.ble``) so the package's default import graph and the hermetic test suite never
pull in WinRT.
"""

from __future__ import annotations

import asyncio
import uuid

import winrt.windows.devices.bluetooth.genericattributeprofile as gatt
from winrt.windows.storage.streams import DataReader, DataWriter


def _uuid16(short: int) -> uuid.UUID:
    """A 16-bit assigned-number UUID expanded to its full 128-bit Bluetooth base form."""
    return uuid.UUID(f"0000{short:04x}-0000-1000-8000-00805f9b34fb")


CPS = _uuid16(0x1818)
CP_MEASUREMENT = _uuid16(0x2A63)      # notify
CP_FEATURE = _uuid16(0x2A65)          # read
CP_SENSOR_LOCATION = _uuid16(0x2A5D)  # read
CP_CONTROL_POINT = _uuid16(0x2A66)    # write + indicate (the SB20's handshake lands here)


def _buffer(data: bytes):
    w = DataWriter()
    w.write_bytes(bytes(data))
    return w.detach_buffer()


class WinrtCpsPeripheral:
    """A minimal Cycling Power Service peripheral: Measurement (notify) + Feature and
    Sensor Location (static reads). Stand it up with ``await start()``, push frames with
    ``await notify(frame)``, and ``stop()`` to stop advertising. Sized for the bench: no
    pairing/bonding (a real Assioma streams power un-bonded, and so do we)."""

    def __init__(self, *, feature_bits: int = 0x08, sensor_location: int = 5, on_write=None):
        # feature_bits 0x08 = Crank Revolution Data Supported. For a FAITHFUL Stages crank pass
        # feature_bits=0x0008030B and sensor_location=0 (the captured values). on_write(bytes) is
        # an optional per-write callback — how we capture the SB20's handshake (also in .writes).
        self._feature_bits = feature_bits
        self._sensor_location = sensor_location
        self._on_write = on_write
        self._provider = None
        self._meas = None
        self._cp = None
        self._loop = None
        self.subscriber_count = 0
        self.writes: list[bytes] = []  # every control-point write a central sent us

    async def start(self) -> None:
        self._loop = asyncio.get_running_loop()  # write-requested fires on a WinRT thread
        result = await gatt.GattServiceProvider.create_async(CPS)
        if int(result.error) != 0:
            raise RuntimeError(f"GattServiceProvider create failed: error={int(result.error)}")
        self._provider = result.service_provider
        service = self._provider.service

        meas_params = gatt.GattLocalCharacteristicParameters()
        meas_params.characteristic_properties = gatt.GattCharacteristicProperties.NOTIFY
        meas_res = await service.create_characteristic_async(CP_MEASUREMENT, meas_params)
        if int(meas_res.error) != 0:
            raise RuntimeError(f"create measurement char failed: error={int(meas_res.error)}")
        self._meas = meas_res.characteristic
        self._meas.add_subscribed_clients_changed(self._on_subscribers_changed)

        feat_params = gatt.GattLocalCharacteristicParameters()
        feat_params.characteristic_properties = gatt.GattCharacteristicProperties.READ
        feat_params.static_value = _buffer(int(self._feature_bits).to_bytes(4, "little"))
        await service.create_characteristic_async(CP_FEATURE, feat_params)

        loc_params = gatt.GattLocalCharacteristicParameters()
        loc_params.characteristic_properties = gatt.GattCharacteristicProperties.READ
        loc_params.static_value = _buffer(bytes([self._sensor_location & 0xFF]))
        await service.create_characteristic_async(CP_SENSOR_LOCATION, loc_params)

        # Control Point (write + indicate): every write a central makes is logged to .writes (and
        # the on_write callback). This is how we capture the SB20's calibration/erg handshake to
        # iterate against in Python — the whole point of the PC rig.
        cp_params = gatt.GattLocalCharacteristicParameters()
        cp_params.characteristic_properties = (
            gatt.GattCharacteristicProperties.WRITE | gatt.GattCharacteristicProperties.INDICATE
        )
        cp_res = await service.create_characteristic_async(CP_CONTROL_POINT, cp_params)
        if int(cp_res.error) != 0:
            raise RuntimeError(f"create control-point char failed: error={int(cp_res.error)}")
        self._cp = cp_res.characteristic
        self._cp.add_write_requested(self._on_cp_write_requested)

        adv = gatt.GattServiceProviderAdvertisingParameters()
        adv.is_connectable = True   # a central can connect (the ESP32 will)
        adv.is_discoverable = True  # include the CPS service UUID in the advertisement
        self._provider.start_advertising_with_parameters(adv)

    def _on_subscribers_changed(self, sender, _args) -> None:
        try:
            self.subscriber_count = len(sender.subscribed_clients)
        except Exception:
            pass

    def _on_cp_write_requested(self, _sender, args) -> None:
        # Fires on a WinRT thread; the request is fetched async, so hop onto our event loop.
        if self._loop is not None:
            asyncio.run_coroutine_threadsafe(self._handle_cp_write(args), self._loop)

    async def _handle_cp_write(self, args) -> None:
        deferral = args.get_deferral()
        try:
            request = await args.get_request_async()
            reader = DataReader.from_buffer(request.value)
            n = int(reader.unconsumed_buffer_length)
            data = bytes(reader.read_byte() for _ in range(n))  # read_byte avoids array-marshalling
            self.writes.append(data)
            if self._on_write is not None:
                self._on_write(data)
            # Acknowledge a write-with-response so the central isn't left waiting.
            if int(request.option) == int(gatt.GattWriteOption.WRITE_WITH_RESPONSE):
                request.respond()
        finally:
            deferral.complete()

    async def notify(self, frame: bytes) -> None:
        """Send one CPS Measurement notification to every subscribed central."""
        if self._meas is not None:
            await self._meas.notify_value_async(_buffer(frame))

    @property
    def advertising(self) -> bool:
        # GattServiceProviderAdvertisementStatus.STARTED == 2
        return self._provider is not None and int(self._provider.advertisement_status) == 2

    def stop(self) -> None:
        if self._provider is not None:
            self._provider.stop_advertising()
