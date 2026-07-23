# Track launch control - BLE download throughput and reliability

> **Status:** research only; no firmware or application changes made. Companion to
> [`track-launch-control-research.md`](track-launch-control-research.md) and
> [`nrf52-sense.md`](nrf52-sense.md). Research date: 2026-07-20.

## Conclusion

BLE is suitable for the v1 run download, but the current recorder transport is not.

The current path is a linear, non-resumable notification stream. It does not explicitly request a larger
ATT MTU, Data Length Extension (DLE), 2M PHY or a shorter connection interval. Its data frame is capped at
14 IMU samples even when the negotiated MTU can carry 19. A new ESP32-S3 central that remains at the BLE
default ATT MTU of 23 would receive only one 12-byte IMU sample per notification.

The recommended v1 path is still GATT notifications, not a new transport:

1. explicitly negotiate and verify ATT MTU 247;
2. request DLE and benchmark 1M before making 2M PHY a dependency;
3. use sequence-numbered windows with offset resume and gap repair;
4. verify the complete run on microSD before a matching erase ACK;
5. quiet non-essential PWA traffic during download; and
6. measure the full matrix on the real nRF and Waveshare ESP32-S3 before setting a speed target.

L2CAP Connection-oriented Channels, compression and a different radio are not justified for v1 unless
the tuned GATT path fails the hardware benchmark.

## 1. Current recorder contract

### Capture

The authoritative implementation is:

- `firmware-nrf/lib/bridge/ImuCapture.h`
- `firmware-nrf/lib/bridge/Proto.h`
- `firmware-nrf/src/main.cpp`

`ImuCapture<8192>` stores a linear array of six `int16_t` values per sample: accelerometer XYZ plus
gyroscope XYZ. Each sample is therefore 12 bytes. Accepted rates are 13, 26, 52 and 104 Hz. The capture
has an implicit timeline (`startMs + sample index / rateHz`) and a CRC32 over its sample bytes.

`firmware-nrf/GATT.md` still describes a 10,240-sample, 120 KiB buffer. That is stale: code is
authoritative at 8,192 samples and 96 KiB.

### Control and data frames

The current `RecCtl` write has only:

```text
[version, command]
```

Commands are Stop, Start, Erase, Download and SetRate. Download has no run identity, byte/sample offset,
window or retry request. `main.cpp::onRecCtlWrite()` always resets the download cursor to sample zero.

`RecData` notifications are:

```text
header  [version, 0xFF, rateHz, reserved, sampleCount u32, startMs u32]
data    [version, 0xFD, sequence u16, sampleCount u8, reserved, samples...]
trailer [version, 0xFE, crc32 u32]
```

The explicit frame type is important: an earlier protocol version used the sequence low byte in that
position, so sequence 254 looked like the `0xFE` trailer and truncated the transfer. Existing host tests
cover that regression.

Each data frame has six bytes of framing plus 12 bytes per sample. `DATA_SAMPLES_PER_FRAME` is fixed at
14, making the maximum current frame 174 bytes. `main.cpp::pumpDownload()` reduces this further to fit
the smallest subscribed connection's actual MTU:

```text
samples per frame = min(14, floor((ATT_MTU - 3 - 6) / 12))
```

Consequences:

- ATT MTU 23: one sample per notification;
- ATT MTU 247: 14 samples per notification because of the hard cap;
- ATT MTU 247 without that cap: 19 samples fit in a 234-byte notification value.

The sender attempts four notifications per Arduino loop pass and backs off when `notify()` reports full
TX buffers. After the trailer is queued it returns to `Idle`; the capture bytes remain in RAM, but the
state does not distinguish complete/unacknowledged data and a new Start can overwrite them.

### Consumer reliability

The reference Web Bluetooth client in `web/index.html` stores data frames by sequence number and checks
the final CRC. It does not detect or request missing ranges. On CRC mismatch it logs `MISMATCH` and still
saves the assembled CSV. There is no reconnect/resume path.

The host tests in `firmware-nrf/test/test_bridge/test_main.cpp` cover framing and CRC, but not:

- missing frames;
- offset/window requests;
- reconnect during download;
- idempotent retry;
- ACK-before-overwrite; or
- power loss while the controller is staging a run.

## 2. Exact transfer sizes

The selected windows use 10 seconds of pre-roll and 10-60 seconds after GO:

| Total capture | Samples at 104 Hz | Raw IMU bytes |
|---|---:|---:|
| 20 s (10+10) | 2,080 | 24,960 |
| 40 s (10+30 default) | 4,160 | 49,920 |
| 70 s (10+60 maximum) | 7,280 | 87,360 |

At the current 14-sample frame cap, assuming a sufficiently large negotiated MTU:

| Total capture | Data frames | Notifications including header/trailer | Framed value bytes |
|---|---:|---:|---:|
| 20 s | 149 | 151 | 25,872 |
| 40 s | 298 | 300 | 51,726 |
| 70 s | 520 | 522 | 90,498 |

At ATT MTU 23, the dynamic frame sizing falls to one sample per data notification:

| Total capture | Data frames | Notifications including header/trailer | Framed value bytes |
|---|---:|---:|---:|
| 20 s | 2,080 | 2,082 | 37,458 |
| 40 s | 4,160 | 4,162 | 74,898 |
| 70 s | 7,280 | 7,282 | 131,058 |

At ATT MTU 247 with a 19-sample frame:

| Total capture | Data frames | Notifications including header/trailer |
|---|---:|---:|
| 20 s | 110 | 112 |
| 40 s | 219 | 221 |
| 70 s | 384 | 386 |

The last two tables show why explicit MTU negotiation and removal of the 14-sample cap matter. They are
wire-format arithmetic, not measured throughput.

The run also needs metadata and raw CPS events. Their final schemas are not frozen. A captured Assioma
typically notifies at about 1 Hz with a nine-byte raw value, so even a generously sized event record is
small relative to the IMU payload. It must still be included in the final benchmark payload rather than
silently omitted.

## 3. What the current radios actually configure

The nRF build uses Adafruit Bluefruit from the `maxgerhardt/platform-nordicnrf52#develop` PlatformIO
platform.

| Parameter | Current nRF behavior |
|---|---|
| ATT MTU | Bluefruit ceiling is 247, but firmware does not initiate an MTU exchange. A connection starts at the BLE default 23 until the GATT client requests a larger value. |
| PHY | No `requestPHY()` call; starts at 1M. |
| DLE | No `requestDataLengthUpdate()` call; Bluefruit's connection object starts with a 27-byte link-layer data length. |
| Connection interval | No explicit request; Bluefruit's default peripheral preference is 20-30 ms. |
| Notification queue/event length | Not explicitly configured. A previous `configPrphConn()` attempt was reverted after corrupting multi-link connection bookkeeping on hardware; this is a blocked/risky lever until isolated. |
| TX power | Fixed at +4 dBm. |
| Reconnect | Advertising restarts on disconnect; there is no download-specific bonded/whitelist reconnect path. |

The existing ESP32 BLE client in `firmware/src/ble/BleMeterClient.cpp` uses NimBLE-Arduino but does not
configure MTU, PHY, DLE or connection parameters. That code proves the repository's ESP32-S3/NimBLE
toolchain, not the future controller's transfer configuration. The controller must explicitly negotiate
and then report the actual values; defaults are not an acceptance criterion.

The nRF can also be maintaining several BLE links. Source-meter CPS traffic and other active links share
the same SoftDevice radio schedule as the bulk peripheral transfer. The benchmark must use the intended
run-time link set, not an otherwise idle nRF.

## 4. Throughput levers

### Required for the first benchmark

**Explicit ATT MTU exchange.** Notification value capacity is `ATT_MTU - 3`. The ESP32 central must
request 247 and reject or clearly report a fallback that cannot meet the selected transfer profile.

**DLE.** MTU and DLE are different. Without DLE, a large ATT value is fragmented across multiple
27-byte link-layer packets. Bluefruit exposes `requestDataLengthUpdate()` but the firmware does not call
it. Requesting DLE is a high-value benchmark candidate.

**Frame size.** With MTU 247, 19 samples fit in the current six-byte frame envelope. Raising the cap from
14 to 19 reduces data-notification count by about 26 percent. This requires a protocol/test update, but
not a new transport.

**Application flow control and resume.** Notifications are appropriate for bulk transfer, but they are
unacknowledged at ATT level. Use sequence completeness and window ACK/NACK at application level rather
than changing every frame to an indication.

### Measure before requiring

**2M PHY.** Both chips support it and Bluefruit exposes `requestPHY()`. The raw symbol rate is higher, but
application gain depends on notification queue depth, event length, coexistence and range. Benchmark it
after MTU/DLE and framing are correct.

**Shorter connection interval.** The BLE floor is 7.5 ms; the current requested range is 20-30 ms.
Shortening it can increase event opportunities but consumes more shared radio time and may interact with
the nRF's other links and ESP32 WiFi coexistence.

**Notification queue/event length.** This can allow several packets per connection event, but the
Bluefruit configuration API previously caused a real multi-link regression in this firmware. It needs a
focused reproduction and packet-level measurement before reuse.

### Defer

**L2CAP CoC.** The underlying stacks contain support at lower levels, but neither Arduino-level API used
by this repository offers a complete portable CoC path. It would be a large cross-stack change.

**Compression.** No real standing-start IMU dataset has been used to establish a safe ratio, CPU cost or
random-access behavior. Do not make v1 timing depend on an assumed compression gain.

## 5. ESP32-S3 WiFi/BLE coexistence

The controller serves a WiFi SoftAP/PWA and acts as a BLE central using one 2.4 GHz radio. Espressif's
official ESP32-S3 coexistence guide states that in the WiFi-connected/BLE-connected scenario the
coexistence period gives WiFi and BLE fixed time slices, each accounting for 50 percent.

Dual CPU cores can avoid application-loop stalls, but they do not remove RF arbitration.

V1 policy:

- keep the SoftAP available so the coach does not lose the app;
- suppress non-essential PWA polling, chart refresh and bulk HTTP traffic while BLE download is active;
- stream BLE data directly to a run-ID staging file on microSD;
- show progress using low-rate updates;
- benchmark WiFi idle, connected-idle and active-PWA cases; and
- only consider a full temporary WiFi pause if measurements show it is necessary.

## 6. Recommended resumable protocol

Preserve the existing Bridge conventions: version byte, explicit frame type, little-endian fields, whole
run CRC and host golden vectors.

### Manifest

Before data, send:

- protocol/schema version;
- opaque run ID;
- IMU rate and sample count;
- byte counts for every stream;
- whole-run CRC or per-stream CRCs;
- capture status (complete, aborted, degraded);
- available offsets/window geometry; and
- immutable timing metadata needed to match a staging file.

The controller verifies free microSD capacity before accepting the transfer.

### Windowed data

- Request a stream plus start offset/window.
- Keep sequence or offset in every data frame.
- Receive into a `run_id` staging file at deterministic offsets.
- Track completeness with ranges/bitmap.
- ACK complete windows and request only missing ranges.
- On disconnect, preserve the staging file and resume from its manifest/completeness state.
- Treat duplicate frames and repeated commands as idempotent.

The whole-run CRC remains the final integrity gate. Per-window CRC is optional if frame completeness and
the whole CRC provide acceptable retry granularity; decide from benchmark fault injection rather than
adding fields by instinct.

### Durable completion

1. Receive all streams.
2. Verify frame/range completeness.
3. Re-read and CRC-check the staged microSD object.
4. Atomically rename/commit it to the final run name.
5. Send an erase ACK containing the matching run ID and CRC.
6. nRF erases or permits overwrite only when both match.

Any disconnect, CRC mismatch, controller reset or failed microSD write leaves the nRF in
`COMPLETE_UNACKNOWLEDGED` and the controller staging file resumable. Never turn a CRC failure into a
success-shaped run.

## 7. Hardware benchmark

No BLE bulk-transfer result exists in `perf-results.md`, so no wall-clock acceptance number is yet
grounded.

Benchmark the following on the actual bike nRF and chosen Waveshare ESP32-S3 controller:

| Axis | Cases |
|---|---|
| Payload | 20 s, 40 s and 70 s at 104 Hz, with representative CPS events and metadata |
| Framing | MTU-23 fallback, current 14-sample frame, 19-sample frame |
| Link | 1M and 2M PHY; DLE off/on; default and shorter connection interval |
| Queue | current safe defaults; any revalidated queue/event-length configuration |
| ESP32 WiFi | SoftAP idle, app connected/idle, normal status polling, deliberately heavy HTTP traffic |
| nRF load | intended meter links active; reduced-link diagnostic case |
| RF | near bench, 5 m line of sight, realistic RSSI/body/track obstruction |
| Faults | disconnect at 10/50/90 percent, walk out of range, ESP32 reset, microSD write failure, bad frame/CRC |
| Repetition | at least 10 transfers per selected configuration |

Record:

- negotiated MTU, PHY, DLE data length and connection interval;
- bytes and notifications sent/retried;
- wall-clock duration and effective application bytes/s;
- disconnects, gaps, CRC failures and resume bytes;
- PWA responsiveness;
- nRF/ESP32 heap and watchdog/reset state; and
- microSD verification and erase-ACK outcome.

Correctness gates can be fixed now:

- every interrupted transfer completes by clean resume;
- the final microSD object is byte-complete and CRC-valid;
- no mismatch is presented as a valid run;
- no run is erased or made overwritable before a matching durable ACK; and
- the controller can still abort/control safely while download is active.

Choose the wall-clock target after the first baseline. As an illustration only, if exactly one
notification were accepted per 20-30 ms connection event, the current 14-sample path would take roughly
6-9 seconds for the default run and 10-16 seconds for the maximum before coexistence and retry effects.
This is not a prediction: real event packet count and WiFi contention are unmeasured.

## 8. V1 recommendation

The current transport is not acceptable unchanged. V1 should implement and prove, in order:

1. protocol parity/golden vectors across nRF C++, ESP32 controller and PWA;
2. run manifest plus complete/unacknowledged lifecycle;
3. offset/window resume, gap repair and durable run-ID staging;
4. explicit MTU 247 negotiation and observed-value telemetry;
5. DLE plus 19-sample frames;
6. WiFi-quiet download policy;
7. baseline benchmark and fault matrix;
8. 2M PHY/connection/queue tuning only where the baseline identifies a bottleneck; and
9. a measured wall-clock acceptance target before the first velodrome session.

This work is a core v1 gate because the nRF retains only one run. A slow transfer is inconvenient; a
non-resumable or falsely successful transfer blocks the next rider or loses the only copy.

## Primary sources

Repository:

- `firmware-nrf/lib/bridge/ImuCapture.h`
- `firmware-nrf/lib/bridge/Proto.h`
- `firmware-nrf/src/main.cpp`
- `firmware-nrf/GATT.md`
- `firmware-nrf/test/test_bridge/test_main.cpp`
- `firmware-nrf/platformio.ini`
- `firmware/src/ble/BleMeterClient.cpp`
- `firmware/platformio.ini`
- `web/index.html`
- [`perf-coex-plan.md`](perf-coex-plan.md)
- [`perf-results.md`](perf-results.md)
- [`architecture-remediation.md`](architecture-remediation.md)

External first-party sources:

- [Adafruit nRF52 Arduino / Bluefruit52Lib source](https://github.com/adafruit/Adafruit_nRF52_Arduino/tree/master/libraries/Bluefruit52Lib/src)
- [NimBLE-Arduino source](https://github.com/h2zero/NimBLE-Arduino/tree/release/2.2/src)
- [Espressif ESP32-S3 RF coexistence guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/coexist.html)
