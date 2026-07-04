// firmware-nrf — the XIAO nRF52840 Sense BLE bridge: read a CPS power meter (central), apply
// the correction, re-broadcast as our own CPS meter (peripheral), and expose the Bridge GATT
// control/telemetry service (GATT.md) for the Web Bluetooth app + Garmin Connect IQ — plus
// BLE-controlled IMU capture for the track bike.
//
// Mirrors the ESP32 proxies: the PURE core (Cps codec + Correction, firmware/lib/proxy) is the
// same code; only the radio seam differs (Bluefruit here, NimBLE there). ANT source/sink slots
// into the same shape once the licensed S340 SoftDevice lands (vendor/softdevice/README.md).
#include <Arduino.h>
#include <bluefruit.h>

#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

#include "LSM6DS3.h"
#include "Wire.h"

#include "Correction.h"    // pure (shared with the ESP32 builds via lib_extra_dirs)
#include "Cps.h"           // pure CPS codec — the same bytes as the ESP32 + Python twins
#include "IPowerSource.h"  // PowerReading
#include "ImuCapture.h"    // pure capture buffer (lib/bridge)
#include "Proto.h"         // pure Bridge-GATT pack/unpack (lib/bridge)

using namespace sb20proxy;
using namespace nrfbridge;
using namespace Adafruit_LittleFS_Namespace;

// ================= config (persisted to internal LittleFS) ====================================
static const char* kCfgPath = "/bridge.cfg";
static ConfigPacket g_cfg;  // defaults: scale 1.0, offset 0, any-CPS source, name below
static Correction g_corr;

static void applyCorrectionFromCfg() {
    g_corr.scale = g_cfg.scaleMilli / 1000.0f;
    g_corr.offset = g_cfg.offsetDeciW / 10.0f;
}

static void cfgLoad() {
    strcpy(g_cfg.outName, "SB20 Bridge");
    File f(InternalFS);
    if (f.open(kCfgPath, FILE_O_READ)) {
        uint8_t buf[CONFIG_LEN];
        if (f.read(buf, sizeof(buf)) == (int)sizeof(buf)) {
            ConfigPacket c;
            if (unpackConfig(buf, sizeof(buf), c)) g_cfg = c;
        }
        f.close();
        Serial.println("[bridge] config loaded from flash");
    } else {
        Serial.println("[bridge] no stored config - defaults (dev flashes wipe LittleFS)");
    }
    applyCorrectionFromCfg();
}

static void cfgSave() {
    uint8_t buf[CONFIG_LEN];
    packConfig(g_cfg, buf);
    InternalFS.remove(kCfgPath);
    File f(InternalFS);
    if (f.open(kCfgPath, FILE_O_WRITE)) {
        f.write(buf, sizeof(buf));
        f.close();
    }
}

// ================= source side: BLE central reading a CPS meter ===============================
static BLEClientService clientCps(UUID16_SVC_CYCLING_POWER);
static BLEClientCharacteristic clientMeas(UUID16_CHR_CYCLING_POWER_MEASUREMENT);

static volatile bool g_srcConnected = false;
static uint16_t g_srcConnHandle = BLE_CONN_HANDLE_INVALID;  // the CENTRAL link (not the web app's)
static char g_srcName[20] = {0};
static PowerReading g_lastSrc;   // raw from the meter
static PowerReading g_lastOut;   // corrected, as broadcast
static uint32_t g_lastSrcMs = 0;

// ================= output side: our CPS peripheral ============================================
static BLEService outCps(UUID16_SVC_CYCLING_POWER);
static BLECharacteristic outMeas(UUID16_CHR_CYCLING_POWER_MEASUREMENT);
static BLECharacteristic outFeature(UUID16_CHR_CYCLING_POWER_FEATURE);
static BLECharacteristic outSensorLoc(UUID16_CHR_SENSOR_LOCATION);
static BLECharacteristic outCp(UUID16_CHR_CYCLING_POWER_CONTROL_POINT);
static BLEDis bledis;
static uint16_t g_crankLenHalfMm = 345;  // 172.5 mm

// ================= the Bridge service (GATT.md) ================================================
// 53423230-XXXX-4bd9-a4ae-1b4e2c633a1d, little-endian; bytes [10],[11] carry XXXX.
#define BRIDGE_UUID(id)                                                                       \
    {0x1d, 0x3a, 0x63, 0x2c, 0x4e, 0x1b, 0xae, 0xa4, 0xd9, 0x4b, (uint8_t)(id), \
     (uint8_t)((id) >> 8), 0x30, 0x32, 0x42, 0x53}
static const uint8_t kUuidBridgeSvc[16] = BRIDGE_UUID(0x0000);
static const uint8_t kUuidStatus[16] = BRIDGE_UUID(0x0001);
static const uint8_t kUuidConfig[16] = BRIDGE_UUID(0x0002);
static const uint8_t kUuidRecCtl[16] = BRIDGE_UUID(0x0003);
static const uint8_t kUuidRecData[16] = BRIDGE_UUID(0x0004);

static BLEService bridgeSvc(kUuidBridgeSvc);
static BLECharacteristic chStatus(kUuidStatus);
static BLECharacteristic chConfig(kUuidConfig);
static BLECharacteristic chRecCtl(kUuidRecCtl);
static BLECharacteristic chRecData(kUuidRecData);

// ================= IMU capture =================================================================
static LSM6DS3 imu(I2C_MODE, 0x6A);
static bool g_imuOk = false;
// 8192 samples x 12 B = 96 KB — leaves comfortable heap beside SoftDevice+FreeRTOS+Bluefruit.
static ImuCapture<8192> g_cap;
static RecState g_recState = RecState::Idle;
static uint32_t g_dlNext = 0;  // next sample index to stream while Downloading
static uint16_t g_dlSeq = 0;
static bool g_dlHeaderSent = false;

// Notify every connection whose CCCD is enabled for this characteristic. Two traps found on
// the bench (2026-07-05): (1) Bluefruit's parameterless notify() resolves to the wrong handle
// once our CENTRAL link (the source meter) is also up; (2) getRole() did NOT identify the
// client link the way its name suggests — the subscribed link reported CENTRAL — so role
// filtering silently skipped the real client. notifyEnabled(h) is the ground truth: only a
// GATT client of OUR server can have set it. Returns false only on a genuine TX failure
// (buffers full — the download path retries); no-subscriber counts as delivered.
static bool notifyClients(BLECharacteristic& ch, const uint8_t* buf, uint16_t len) {
    bool ok = true;
    for (uint16_t h = 0; h < 8; ++h) {
        BLEConnection* conn = Bluefruit.Connection(h);
        // Key on the CCCD ALONE. Role-guarding broke delivery entirely: Bluefruit's per-link
        // role/handle bookkeeping is not trustworthy on this fork (the client's CCCD shows up
        // on an entry labeled CENTRAL) - notifyEnabled() is the only reliable signal, and only
        // a GATT client of our server can set it.
        if (conn && conn->connected() && ch.notifyEnabled(h)) {
            if (!ch.notify(h, buf, len)) ok = false;
        }
    }
    return ok;
}

// ================= source scan / connect ======================================================
static volatile uint32_t g_scanReports = 0;  // liveness: how many adv reports reached scanCb

static void scanCb(ble_gap_evt_adv_report_t* report) {
    ++g_scanReports;
    // NO Scanner.filterUuid here — the name lives in the SCAN RESPONSE while 0x1818 lives in
    // the ADV packet, and a uuid filter drops the scan-rsp reports, so a name filter could
    // never match (cost a debug loop). Instead: with a name filter set, match the name report
    // and let CPS service discovery be the validator (a non-CPS name match just disconnects
    // and rescans); with no filter, take any 0x1818 advertiser (the desk fake meter).
    bool take = false;
    if (g_cfg.srcFilter[0] != '\0') {
        uint8_t nameBuf[28] = {0};
        if (Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME,
                                                nameBuf, sizeof(nameBuf) - 1) ||
            Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_SHORT_LOCAL_NAME,
                                                nameBuf, sizeof(nameBuf) - 1)) {
            Serial.printf("[scan] name '%s'\n", nameBuf);
            take = strstr((const char*)nameBuf, g_cfg.srcFilter) != nullptr;
            if (take) Serial.printf("[bridge] source match '%s' - connecting\n", nameBuf);
        }
    } else {
        take = Bluefruit.Scanner.checkReportForService(report, clientCps);
        if (take) Serial.println("[bridge] CPS advertiser found - connecting");
    }
    if (take) Bluefruit.Central.connect(report);
    else Bluefruit.Scanner.resume();
}

static void measNotifyCb(BLEClientCharacteristic* /*chr*/, uint8_t* data, uint16_t len) {
    // Decode the meter's frame with the shared pure codec, correct it, and relay: the output
    // frame passes the source's own crank fields through unchanged (cadence is identical).
    PowerReading r;
    r.power_w = decodeCpsPower(data, len);
    const uint16_t flags = decodeCpsFlags(data, len);
    const CpsBalance bal = decodeCpsBalance(data, len);
    r.balance_half_pct = bal.present ? bal.halfPct : -1;
    r.t_ms = millis();
    g_lastSrc = r;
    g_lastSrcMs = r.t_ms;

    PowerReading out = g_corr.apply(r);
    g_lastOut = out;

    std::vector<uint8_t> frame;
    if (flags & CPM_CRANK_REV_DATA_PRESENT) {
        const CpsCrankData cd = decodeCrankData(data, len);
        frame = encodeCpsMeasurement(out.power_w, cd.cumulativeRevs, cd.lastEventTime);
    } else {
        frame = encodeCpsMeasurement(out.power_w);
    }
    notifyClients(outMeas, frame.data(), frame.size());
}

static void centralConnectCb(uint16_t connHandle) {
    if (!clientCps.discover(connHandle)) {
        Bluefruit.disconnect(connHandle);
        return;
    }
    if (!clientMeas.discover()) {
        Bluefruit.disconnect(connHandle);
        return;
    }
    clientMeas.enableNotify();
    // Remember who we latched onto (for the status surface / web app).
    BLEConnection* conn = Bluefruit.Connection(connHandle);
    if (conn) conn->getPeerName(g_srcName, sizeof(g_srcName) - 1);
    g_srcConnHandle = connHandle;
    g_srcConnected = true;
    Serial.printf("[bridge] source connected: '%s'\n", g_srcName);
    // The SoftDevice drops our advertising when the central link comes up (observed on air:
    // 'SB20 Bridge' vanished the moment the source connected) — bring it back so head units
    // and the web app can still find us. Same after a central disconnect, belt and braces.
    if (!Bluefruit.Advertising.isRunning()) Bluefruit.Advertising.start(0);
}

static void centralDisconnectCb(uint16_t /*connHandle*/, uint8_t reason) {
    g_srcConnected = false;
    g_srcConnHandle = BLE_CONN_HANDLE_INVALID;
    g_srcName[0] = 0;
    Serial.printf("[bridge] source dropped (0x%02X); rescanning\n", reason);
    Bluefruit.Scanner.start(0);
    if (!Bluefruit.Advertising.isRunning()) Bluefruit.Advertising.start(0);
}

// Keep advertising while a peripheral slot remains free (head unit + web app concurrently);
// the SoftDevice stops adv on each connect, so restart it until we're full.
static void periphConnectCb(uint16_t /*connHandle*/) {
    if (!Bluefruit.Advertising.isRunning()) Bluefruit.Advertising.start(0);
}
static void periphDisconnectCb(uint16_t /*connHandle*/, uint8_t /*reason*/) {
    if (!Bluefruit.Advertising.isRunning()) Bluefruit.Advertising.start(0);
}

// ================= output CP (zero-reset etc.) =================================================
static void cpWriteCb(uint16_t connHandle, BLECharacteristic* /*chr*/, uint8_t* data,
                      uint16_t len) {
    // The pure handler answers offset-comp / crank-length ops; we can't forward a real zero to
    // the source yet (v1) but reply correctly so head units don't drop the link.
    CpResult res = handleControlPoint(data, len, g_crankLenHalfMm, /*calOffset=*/0);
    if (res.crankLengthChanged) g_crankLenHalfMm = res.crankLengthHalfMm;
    if (!res.response.empty()) outCp.indicate(connHandle, res.response.data(), res.response.size());
}

// ================= Bridge service callbacks ====================================================
static void notifyRecState() {
    uint8_t buf[RECSTATE_LEN];
    packRecState(g_recState, g_cap.rateHz(), g_cap.count(), g_cap.capacity(), buf);
    notifyClients(chRecCtl, buf, sizeof(buf));
}

static void configWriteCb(uint16_t /*conn*/, BLECharacteristic* /*chr*/, uint8_t* data,
                          uint16_t len) {
    ConfigPacket c;
    if (!unpackConfig(data, len, c)) {
        Serial.println("[bridge] config write REJECTED (bad version/range)");
        return;
    }
    if (c.srcIsAnt || c.outIsAnt) {
        Serial.println("[bridge] config write REJECTED (ANT routing needs the S340 SoftDevice)");
        return;
    }
    const bool nameChanged = strncmp(c.outName, g_cfg.outName, CFG_NAME_LEN) != 0;
    const bool filterChanged = strncmp(c.srcFilter, g_cfg.srcFilter, CFG_NAME_LEN) != 0;
    g_cfg = c;
    applyCorrectionFromCfg();
    cfgSave();
    uint8_t buf[CONFIG_LEN];
    packConfig(g_cfg, buf);
    chConfig.write(buf, sizeof(buf));  // read-back reflects what stuck
    if (nameChanged) {
        Bluefruit.setName(g_cfg.outName);
        Bluefruit.Advertising.stop();
        Bluefruit.Advertising.start(0);
    }
    if (filterChanged) {
        // re-pick the source under the new filter (drop the CENTRAL link, not the web app's)
        if (g_srcConnected && g_srcConnHandle != BLE_CONN_HANDLE_INVALID) {
            Bluefruit.disconnect(g_srcConnHandle);
        } else if (!Bluefruit.Scanner.isRunning()) {
            Bluefruit.Scanner.start(0);
        }
    }
    Serial.printf("[bridge] config applied: scale=%.3f offset=%.1f src='%s' out='%s'\n",
                  (double)g_corr.scale, (double)g_corr.offset, g_cfg.srcFilter, g_cfg.outName);
}

static void recCtlWriteCb(uint16_t /*conn*/, BLECharacteristic* /*chr*/, uint8_t* data,
                          uint16_t len) {
    RecCtlWrite w;
    if (!unpackRecCtl(data, len, w)) return;
    switch (w.cmd) {
        case RecCmd::Start:
            if (g_imuOk && g_recState == RecState::Idle) {
                g_cap.start(millis(), g_cap.rateHz());
                g_recState = RecState::Recording;
                Serial.println("[rec] started");
            }
            break;
        case RecCmd::Stop:
            g_cap.stop();
            if (g_recState != RecState::Idle) Serial.println("[rec] stopped");
            g_recState = RecState::Idle;
            break;
        case RecCmd::Erase:
            g_cap.erase();
            g_recState = RecState::Idle;
            Serial.println("[rec] erased");
            break;
        case RecCmd::Download:
            if (g_recState == RecState::Idle && g_cap.count() > 0) {
                g_recState = RecState::Downloading;
                g_dlNext = 0;
                g_dlSeq = 0;
                g_dlHeaderSent = false;
                Serial.printf("[rec] download start (%lu samples)\n",
                              (unsigned long)g_cap.count());
            }
            break;
        case RecCmd::SetRate:
            if (g_recState == RecState::Idle) {
                g_cap.start(0, w.rateHz);  // sets the rate...
                g_cap.erase();             // ...without arming a capture
                Serial.printf("[rec] rate = %u Hz\n", w.rateHz);
            }
            break;
    }
    notifyRecState();
}

// ================= setup ======================================================================
void setup() {
    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 3000) delay(10);
    Serial.println("[bridge] XIAO nRF52840 Sense CPS bridge starting");

    InternalFS.begin();
    cfgLoad();

    // IMU (power-gated on the Sense)
#ifdef PIN_LSM6DS3TR_C_POWER
    pinMode(PIN_LSM6DS3TR_C_POWER, OUTPUT);
    digitalWrite(PIN_LSM6DS3TR_C_POWER, HIGH);
    delay(50);
#endif
    imu.settings.accelRange = 16;       // g — track hits are spiky
    imu.settings.gyroRange = 2000;      // dps
    imu.settings.accelSampleRate = 416;  // read-on-demand; our loop paces the capture rate
    imu.settings.gyroSampleRate = 416;
    g_imuOk = (imu.begin() == 0);
    Serial.printf("[bridge] IMU: %s\n", g_imuOk ? "OK" : "NOT FOUND");

    // NOTE: no configPrphConn() here — with it, Bluefruit's connection bookkeeping corrupted
    // (both links collapsed onto one tracked handle; the client's CCCD landed on the source
    // link's records — bench, 2026-07-05). The maxgerhardt/Adafruit core's defaults already
    // negotiate MTU 247 on the peripheral link; pumpDownload sizes frames per-client anyway.
    // TWO peripheral links: a head unit (reading our CPS) and the web app / Garmin (the Bridge
    // service) connect at the same time — and a stray desk client can't lock everyone out.
    Bluefruit.begin(/*peripheral*/ 2, /*central*/ 1);
    Bluefruit.setTxPower(4);
    Bluefruit.setName(g_cfg.outName);

    // --- output: standard CPS peripheral (a head unit pairs to this) ---
    bledis.setManufacturer("SB20 Proxy");
    bledis.setModel("nRF Bridge");
    bledis.begin();
    outCps.begin();
    outMeas.setProperties(CHR_PROPS_NOTIFY);
    outMeas.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    outMeas.setMaxLen(20);
    outMeas.begin();
    outFeature.setProperties(CHR_PROPS_READ);
    outFeature.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    outFeature.setFixedLen(4);
    outFeature.begin();
    const uint32_t feat = CP_FEATURE_CRANK_REV_SUPPORTED;
    outFeature.write32(feat);
    outSensorLoc.setProperties(CHR_PROPS_READ);
    outSensorLoc.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    outSensorLoc.setFixedLen(1);
    outSensorLoc.begin();
    outSensorLoc.write8(5);  // left crank
    outCp.setProperties(CHR_PROPS_WRITE | CHR_PROPS_INDICATE);
    outCp.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    outCp.setWriteCallback(cpWriteCb);
    outCp.begin();

    // --- the Bridge control/telemetry service (GATT.md) ---
    bridgeSvc.begin();
    chStatus.setProperties(CHR_PROPS_NOTIFY | CHR_PROPS_READ);
    chStatus.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    chStatus.setFixedLen(STATUS_LEN);
    chStatus.begin();
    chConfig.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE);
    chConfig.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    chConfig.setFixedLen(CONFIG_LEN);
    chConfig.setWriteCallback(configWriteCb);
    chConfig.begin();
    {
        uint8_t buf[CONFIG_LEN];
        packConfig(g_cfg, buf);
        chConfig.write(buf, sizeof(buf));
    }
    chRecCtl.setProperties(CHR_PROPS_WRITE | CHR_PROPS_NOTIFY);
    chRecCtl.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    chRecCtl.setMaxLen(RECSTATE_LEN);  // the NOTIFY needs 12 (maxLen 4 truncated it - bench)
    chRecCtl.setWriteCallback(recCtlWriteCb);
    chRecCtl.begin();
    chRecData.setProperties(CHR_PROPS_NOTIFY);
    chRecData.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    chRecData.setMaxLen(180);
    chRecData.begin();

    // --- source: central scanning for a CPS meter ---
    clientCps.begin();
    clientMeas.setNotifyCallback(measNotifyCb);
    clientMeas.begin();
    Bluefruit.Periph.setConnectCallback(periphConnectCb);
    Bluefruit.Periph.setDisconnectCallback(periphDisconnectCb);
    Bluefruit.Central.setConnectCallback(centralConnectCb);
    Bluefruit.Central.setDisconnectCallback(centralDisconnectCb);
    Bluefruit.Scanner.setRxCallback(scanCb);
    Bluefruit.Scanner.restartOnDisconnect(true);
    Bluefruit.Scanner.useActiveScan(true);  // we need names for the filter + status surface
    Bluefruit.Scanner.setInterval(160, 80);
    Bluefruit.Scanner.start(0);

    // --- advertising: a normal CPS meter + our name (the Bridge service is discovered on
    // connect; Web Bluetooth reaches it via optionalServices, no need to advertise the 128-bit)
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(outCps);
    Bluefruit.ScanResponse.addName();
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 244);
    Bluefruit.Advertising.setFastTimeout(30);
    Bluefruit.Advertising.start(0);

    Serial.printf("[bridge] up: out='%s' src-filter='%s' scale=%.3f offset=%.1f\n", g_cfg.outName,
                  g_cfg.srcFilter, (double)g_corr.scale, (double)g_corr.offset);
}

// ================= loop =======================================================================
static void pumpDownload() {
    if (!g_dlHeaderSent) {
        uint8_t hdr[12];
        packRecHeader(g_cap.rateHz(), g_cap.count(), g_cap.startMs(), hdr);
        if (!notifyClients(chRecData, hdr, sizeof(hdr))) return;  // buffers full: retry next loop
        g_dlHeaderSent = true;
    }
    // Size frames to the smallest subscriber MTU (notify payload = MTU-3; 4-byte frame header;
    // 12 bytes/sample) — a CIQ client at MTU 23 gets 1-sample frames, a DLE client gets 14.
    uint16_t mtu = 247;
    for (uint16_t h = 0; h < 8; ++h) {
        BLEConnection* conn = Bluefruit.Connection(h);
        if (conn && conn->connected() && chRecData.notifyEnabled(h)) {
            mtu = min(mtu, conn->getMtu());
        }
    }
    const size_t perFrame = max((size_t)1,
        min((size_t)DATA_SAMPLES_PER_FRAME, (size_t)((mtu - 3 - DATA_FRAME_OVERHEAD) / SAMPLE_LEN)));
    // A few frames per loop pass; notify() returning false = TX buffers full, back off.
    for (int burst = 0; burst < 4 && g_dlNext < g_cap.count(); ++burst) {
        const size_t n = min(perFrame, (size_t)(g_cap.count() - g_dlNext));
        uint8_t frame[DATA_FRAME_OVERHEAD + DATA_SAMPLES_PER_FRAME * SAMPLE_LEN];
        const size_t len = packRecDataFrame(g_dlSeq, g_cap.sample(g_dlNext), n, frame);
        if (!notifyClients(chRecData, frame, len)) return;
        g_dlNext += n;
        ++g_dlSeq;
    }
    if (g_dlNext >= g_cap.count()) {
        uint8_t tail[6];
        packRecTrailer(g_cap.crc32(), tail);
        if (!notifyClients(chRecData, tail, sizeof(tail))) return;
        g_recState = RecState::Idle;
        notifyRecState();
        Serial.println("[rec] download complete");
    }
}

void loop() {
    const uint32_t now = millis();

    // IMU capture pacing
    if (g_recState == RecState::Recording && g_imuOk) {
        static uint32_t nextSampleUs = 0;
        const uint32_t nowUs = micros();
        const uint32_t periodUs = 1000000UL / g_cap.rateHz();
        if (nextSampleUs == 0 || (int32_t)(nowUs - nextSampleUs) >= 0) {
            nextSampleUs = (nextSampleUs == 0 ? nowUs : nextSampleUs) + periodUs;
            int16_t s[6] = {imu.readRawAccelX(), imu.readRawAccelY(), imu.readRawAccelZ(),
                            imu.readRawGyroX(),  imu.readRawGyroY(),  imu.readRawGyroZ()};
            if (!g_cap.add(s)) {
                g_recState = RecState::Idle;  // full
                notifyRecState();
                Serial.println("[rec] buffer full - stopped");
            }
        }
    } else if (g_recState == RecState::Downloading) {
        pumpDownload();
    }

    // Liveness heartbeat (5 s): scanner state — the scan path failed silently once already
    static uint32_t hbAt = 0;
    if (now - hbAt >= 5000) {
        hbAt = now;
        Serial.printf("[hb] scanning=%d reports=%lu srcConn=%d filter='%s' src=%dW out=%dW\n",
                      Bluefruit.Scanner.isRunning(), (unsigned long)g_scanReports,
                      (int)g_srcConnected, g_cfg.srcFilter, (int)g_lastSrc.power_w,
                      (int)g_lastOut.power_w);
        for (uint16_t h = 0; h < 20; ++h) {
            BLEConnection* conn = Bluefruit.Connection(h);
            if (conn) {
                Serial.printf("[hb]   link h=%u conn=%d role=%d statusSub=%d recSub=%d mtu=%u\n",
                              h, (int)conn->connected(), (int)conn->getRole(),
                              (int)chStatus.notifyEnabled(h), (int)chRecData.notifyEnabled(h),
                              conn->getMtu());
            }
        }
    }

    // Status notify @2 Hz (+ recstate @1 Hz while recording)
    static uint32_t statusAt = 0;
    if (now - statusAt >= 500) {
        statusAt = now;
        // stale-source scrub, like the ESP32 proxy: 6 s without a frame = show disconnected data
        const bool fresh = g_srcConnected && (now - g_lastSrcMs) < 6000;
        StatusPacket st;
        st.srcConnected = g_srcConnected;
        st.outAdvertising = true;
        st.recording = (g_recState == RecState::Recording);
        st.srcPowerW = fresh ? g_lastSrc.power_w : -1;
        st.outPowerW = fresh ? g_lastOut.power_w : -1;
        st.cadenceRpm = -1;  // v1: cadence passes through the CPS frames; not re-derived here
        st.balancePct = fresh ? (int8_t)(g_lastSrc.balance_half_pct >= 0
                                             ? g_lastSrc.balance_half_pct / 2
                                             : -1)
                              : -1;
        st.scaleMilli = g_cfg.scaleMilli;
        st.offsetDeciW = g_cfg.offsetDeciW;
        st.recSamples = g_cap.count();
        st.uptimeS = (uint16_t)(now / 1000);
        uint8_t buf[STATUS_LEN];
        packStatus(st, buf);
        chStatus.write(buf, STATUS_LEN);  // readable snapshot
        notifyClients(chStatus, buf, STATUS_LEN);
        static uint8_t recTick = 0;
        if (st.recording && (++recTick & 1)) notifyRecState();
    }

    delay(2);
}
