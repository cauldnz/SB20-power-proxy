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

// The Adafruit nRF core's Arduino.h defines abs/round/min/max as MACROS, which break the
// std::round / std::min<> inside the shared pure headers (CalibrationFit et al). Undo them
// before those includes — the pure code wants the real std functions, not the Arduino macros.
#undef abs
#undef round
#undef min
#undef max
#undef constrain
#include "CalibrationSession.h"  // pure on-device DUT->reference calibration (shared with ESP32)
#include "Correction.h"    // pure (shared with the ESP32 builds via lib_extra_dirs)
#include "Cps.h"           // pure CPS codec — the same bytes as the ESP32 + Python twins
#include "IPowerSource.h"  // PowerReading
#include "ImuCapture.h"    // pure capture buffer (lib/bridge)
#include "Proto.h"         // pure Bridge-GATT pack/unpack (lib/bridge)
#include "SourceCandidate.h"  // pure scanned-device list + dedup (shared with ESP32) (P3)

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
    // NB: g_corr.curve (if populated by a Curve write / calibration) WINS over scale+offset in
    // Correction::apply — so a fitted curve keeps applying regardless of the scalar fields.
}

// ---- correction curve (persisted separately from the scalar config) --------------------------
static const char* kCurvePath = "/curve.bin";
static void curveSave() {
    InternalFS.remove(kCurvePath);
    if (g_corr.curve.empty()) return;
    File f(InternalFS);
    if (f.open(kCurvePath, FILE_O_WRITE)) {
        const uint8_t n = (uint8_t)g_corr.curve.points.size();
        uint8_t buf[2 + CURVE_MAX_POINTS * 4];
        CurvePoint pts[CURVE_MAX_POINTS];
        for (uint8_t i = 0; i < n && i < CURVE_MAX_POINTS; ++i) {
            pts[i].powerW = (uint16_t)g_corr.curve.points[i].power_w;
            pts[i].factorMilli = (uint16_t)(g_corr.curve.points[i].factor * 1000.0f + 0.5f);
        }
        f.write(buf, packCurve(pts, n, buf));
        f.close();
    }
}
static void curveLoad() {
    File f(InternalFS);
    if (!f.open(kCurvePath, FILE_O_READ)) return;
    uint8_t buf[2 + CURVE_MAX_POINTS * 4];
    int rd = f.read(buf, sizeof(buf));
    f.close();
    CurvePoint pts[CURVE_MAX_POINTS];
    int n = (rd >= 2) ? unpackCurve(buf, rd, pts) : -1;
    if (n > 0) {
        g_corr.curve = CorrectionCurve{};
        for (int i = 0; i < n; ++i) g_corr.curve.add(pts[i].powerW, pts[i].factorMilli / 1000.0f);
        Serial.printf("[bridge] correction curve loaded (%d points)\n", n);
    }
}

// ---- RGB status LED (active-low; pins from the probe). Track use: glanceable link state. ------
static void setLed(bool r, bool g, bool b) {
    digitalWrite(LED_RED, r ? LOW : HIGH);
    digitalWrite(LED_GREEN, g ? LOW : HIGH);
    digitalWrite(LED_BLUE, b ? LOW : HIGH);
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
static BLEClientCharacteristic clientSrcCp(UUID16_CHR_CYCLING_POWER_CONTROL_POINT);  // for zero-fwd

// Second central: the REFERENCE meter, read alongside the source (DUT) during calibration only.
// Separate BLEClientService/Characteristic instances so each binds to its own connection handle
// (Bluefruit's discover() is per-connection); the notify callback tells them apart by char ptr.
static BLEClientService refCps(UUID16_SVC_CYCLING_POWER);
static BLEClientCharacteristic refMeas(UUID16_CHR_CYCLING_POWER_MEASUREMENT);

// ---- on-device calibration (pure CalibrationSession, shared with the ESP32) ------------------
static CalibrationSession g_cal;
static volatile bool g_calibrating = false;       // a calibration session is collecting
static char g_refFilter[20] = {0};                // reference meter name filter (calibration)
static uint16_t g_refConnHandle = BLE_CONN_HANDLE_INVALID;
static volatile bool g_refConnected = false;

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
static BLEDfu bledfu;   // BLE OTA (Adafruit buttonless DFU) — flash over Bluetooth, no USB (P3)
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
static const uint8_t kUuidCurve[16] = BRIDGE_UUID(0x0005);
static const uint8_t kUuidCal[16] = BRIDGE_UUID(0x0006);
static const uint8_t kUuidScan[16] = BRIDGE_UUID(0x0007);

static BLEService bridgeSvc(kUuidBridgeSvc);
static BLECharacteristic chStatus(kUuidStatus);
static BLECharacteristic chConfig(kUuidConfig);
static BLECharacteristic chRecCtl(kUuidRecCtl);
static BLECharacteristic chRecData(kUuidRecData);
static BLECharacteristic chCurve(kUuidCurve);   // correction-curve write/read (P1)
static BLECharacteristic chCal(kUuidCal);       // calibration control + state (P2)
static BLECharacteristic chScan(kUuidScan);     // scanned-source list for the web picker (P3)

// Discovered nearby CPS/FTMS devices (the web source picker). The scan callback records every
// advertiser; the list is deduped/capped by the pure addCandidate.
static std::vector<SourceCandidate> g_candidates;
static volatile bool g_candDirty = false;

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
    uint8_t nameBuf[28] = {0};
    const bool haveName =
        Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME, nameBuf,
                                            sizeof(nameBuf) - 1) ||
        Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_SHORT_LOCAL_NAME, nameBuf,
                                            sizeof(nameBuf) - 1);

    // Record every named advertiser into the picker list (P3): the pure addCandidate dedups by
    // address + keeps the strongest RSSI. checkReportForService tells us CPS/FTMS from the UUIDs.
    if (haveName) {
        SourceCandidate c;
        char addr[20];
        snprintf(addr, sizeof(addr), "%02x:%02x:%02x:%02x:%02x:%02x", report->peer_addr.addr[5],
                 report->peer_addr.addr[4], report->peer_addr.addr[3], report->peer_addr.addr[2],
                 report->peer_addr.addr[1], report->peer_addr.addr[0]);
        c.address = addr;
        c.name = (const char*)nameBuf;
        c.rssi = report->rssi;
        c.isCps = Bluefruit.Scanner.checkReportForService(report, clientCps);
        c.isFtms = Bluefruit.Scanner.checkReportForUuid(report, UUID16_SVC_FITNESS_MACHINE);
        c.isStagesCrank = (c.name.rfind("Stages ", 0) == 0);
        if (c.isCps || c.isFtms) {  // only meters/trainers matter to the picker
            addCandidate(g_candidates, c, SCAN_MAX);
            g_candDirty = true;
        }
    }
    // During calibration, also grab the REFERENCE meter (2nd central) when it isn't yet connected.
    if (g_calibrating && !g_refConnected && g_refFilter[0] && haveName &&
        strstr((const char*)nameBuf, g_refFilter) != nullptr &&
        strstr((const char*)nameBuf, g_cfg.srcFilter) == nullptr) {
        Serial.printf("[cal] reference match '%s' - connecting\n", nameBuf);
        Bluefruit.Central.connect(report);
        return;
    }
    if (g_srcConnected) { Bluefruit.Scanner.resume(); return; }  // source already up; keep scanning for ref
    if (g_cfg.srcFilter[0] != '\0') {
        if (haveName) {
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
    // single-sided x2: a left/right-only crank reports half of total; double it BEFORE the
    // correction so the correction scale/curve operates on total power (ESP32 semantics).
    if (g_cfg.singleSided) r.power_w = (int16_t)(r.power_w * 2);
    g_lastSrc = r;
    g_lastSrcMs = r.t_ms;

    // Calibration: the SOURCE we repeat IS the DUT (the meter to correct). Feed its raw reading
    // to the session; the reference stream arrives via refMeasNotifyCb.
    if (g_calibrating) g_cal.onDut((float)r.power_w, r.t_ms);

    PowerReading out = g_corr.apply(r);  // curve wins over scale/offset when populated
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

// Reference-meter notify (calibration only): feed the session's reference stream.
static void refMeasNotifyCb(BLEClientCharacteristic* /*chr*/, uint8_t* data, uint16_t len) {
    if (!g_calibrating) return;
    const int16_t p = decodeCpsPower(data, len);
    g_cal.onRef((float)p, millis());
}

static void centralConnectCb(uint16_t connHandle) {
    // Which meter is this? During calibration the REFERENCE (its name matches g_refFilter) uses
    // the 2nd central + its own client instances; everything else is the source/DUT.
    char peer[24] = {0};
    BLEConnection* conn = Bluefruit.Connection(connHandle);
    if (conn) conn->getPeerName(peer, sizeof(peer) - 1);
    const bool isRef = g_calibrating && g_refFilter[0] && strstr(peer, g_refFilter) != nullptr &&
                       connHandle != g_srcConnHandle;
    if (isRef) {
        if (!refCps.discover(connHandle) || !refMeas.discover()) {
            Bluefruit.disconnect(connHandle);
            return;
        }
        refMeas.enableNotify();
        g_refConnHandle = connHandle;
        g_refConnected = true;
        Serial.printf("[cal] reference connected: '%s'\n", peer);
        if (!Bluefruit.Advertising.isRunning()) Bluefruit.Advertising.start(0);
        return;
    }

    if (!clientCps.discover(connHandle)) {
        Bluefruit.disconnect(connHandle);
        return;
    }
    if (!clientMeas.discover()) {
        Bluefruit.disconnect(connHandle);
        return;
    }
    clientMeas.enableNotify();
    clientSrcCp.discover();  // the source's Cycling Power Control Point (0x2A66), for zero-forward
                             // — optional; not every meter exposes it (Assioma does)
    // Remember who we latched onto (for the status surface / web app).
    strncpy(g_srcName, peer, sizeof(g_srcName) - 1);
    g_srcConnHandle = connHandle;
    g_srcConnected = true;
    Serial.printf("[bridge] source connected: '%s'\n", g_srcName);
    // The SoftDevice drops our advertising when the central link comes up (observed on air:
    // 'SB20 Bridge' vanished the moment the source connected) — bring it back so head units
    // and the web app can still find us. Same after a central disconnect, belt and braces.
    if (!Bluefruit.Advertising.isRunning()) Bluefruit.Advertising.start(0);
}

static void centralDisconnectCb(uint16_t connHandle, uint8_t reason) {
    if (connHandle == g_refConnHandle) {  // the reference meter dropped
        g_refConnected = false;
        g_refConnHandle = BLE_CONN_HANDLE_INVALID;
        Serial.printf("[cal] reference dropped (0x%02X)\n", reason);
    } else {
        g_srcConnected = false;
        g_srcConnHandle = BLE_CONN_HANDLE_INVALID;
        g_srcName[0] = 0;
        Serial.printf("[bridge] source dropped (0x%02X); rescanning\n", reason);
    }
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
static volatile bool g_pendSourceZero = false;  // a head unit asked us to zero -> forward to source

static void cpWriteCb(uint16_t connHandle, BLECharacteristic* /*chr*/, uint8_t* data,
                      uint16_t len) {
    // The pure handler answers offset-comp / crank-length ops; when the head unit asks for a
    // zero-reset (0x0C/0x10) we ALSO forward a real zero to the source meter — flagged here,
    // written from loop() (never a re-entrant central op inside this callback).
    CpResult res = handleControlPoint(data, len, g_crankLenHalfMm, /*calOffset=*/0);
    if (res.crankLengthChanged) g_crankLenHalfMm = res.crankLengthHalfMm;
    if (res.requestSourceZero) g_pendSourceZero = true;
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
    const bool wasSingle = g_cfg.singleSided;
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
    Serial.printf("[bridge] config applied: scale=%.3f offset=%.1f single2x=%d src='%s' out='%s'\n",
                  (double)g_corr.scale, (double)g_corr.offset, g_cfg.singleSided, g_cfg.srcFilter,
                  g_cfg.outName);
    (void)wasSingle;
}

// Publish the scanned-source list to the ScanList characteristic (read-back + notify).
static void publishScanList() {
    std::vector<SourceCandidate> sorted = dedupeAndSortSources(g_candidates);
    ScanEntry e[SCAN_MAX];
    uint8_t n = 0;
    for (const auto& c : sorted) {
        if (n >= SCAN_MAX) break;
        strncpy(e[n].name, c.name.c_str(), SCAN_NAME);
        e[n].name[SCAN_NAME] = 0;
        e[n].rssi = (int8_t)c.rssi;
        e[n].flags = (uint8_t)((c.isCps ? 1 : 0) | (c.isFtms ? 2 : 0) | (c.isStagesCrank ? 4 : 0));
        ++n;
    }
    uint8_t buf[2 + SCAN_MAX * SCAN_SLOT];
    size_t len = packScanList(e, n, buf);
    chScan.write(buf, len);
    notifyClients(chScan, buf, len);
}

// Publish the active curve to the Curve characteristic for read-back.
static void publishCurve() {
    uint8_t buf[2 + CURVE_MAX_POINTS * 4];
    CurvePoint pts[CURVE_MAX_POINTS];
    const uint8_t n = (uint8_t)g_corr.curve.points.size();
    for (uint8_t i = 0; i < n && i < CURVE_MAX_POINTS; ++i) {
        pts[i].powerW = (uint16_t)g_corr.curve.points[i].power_w;
        pts[i].factorMilli = (uint16_t)(g_corr.curve.points[i].factor * 1000.0f + 0.5f);
    }
    chCurve.write(buf, packCurve(pts, n, buf));
}

// Curve write: replace the correction curve (empty clears it -> back to scale/offset). Persisted.
static void curveWriteCb(uint16_t /*conn*/, BLECharacteristic* /*chr*/, uint8_t* data,
                         uint16_t len) {
    CurvePoint pts[CURVE_MAX_POINTS];
    int n = unpackCurve(data, len, pts);
    if (n < 0) {
        Serial.println("[bridge] curve write REJECTED (bad payload)");
        return;
    }
    g_corr.curve = CorrectionCurve{};
    for (int i = 0; i < n; ++i) g_corr.curve.add(pts[i].powerW, pts[i].factorMilli / 1000.0f);
    curveSave();
    publishCurve();
    Serial.printf("[bridge] correction curve set (%d points; curve %s scale/offset)\n", n,
                  n > 0 ? "overrides" : "cleared, using");
}

// ---- calibration control + state -------------------------------------------------------------
static void notifyCalState() {
    CalWireState st = g_cal.fitted()      ? CalWireState::Fitted
                      : g_cal.collecting() ? CalWireState::Collecting
                                           : CalWireState::Idle;
    std::vector<int> cov = g_cal.coverage();
    int cov6[6] = {0};
    for (size_t i = 0; i < cov.size() && i < 6; ++i) cov6[i] = cov[i];
    uint8_t buf[CALSTATE_LEN];
    packCalState(st, (uint16_t)g_cal.pairCount(), (uint16_t)g_cal.minPairs(),
                 (int16_t)(g_cal.residualW() * 10.0f), cov6, g_cal.enoughToFit(), buf);
    notifyClients(chCal, buf, sizeof(buf));
}

// Apply a fitted Correction (curve or linear) as the live correction + persist it.
static void applyFit(const Correction& fit) {
    g_corr.curve = fit.curve;
    if (fit.curve.empty()) {  // linear fit -> store as scale/offset
        g_cfg.scaleMilli = (uint16_t)(fit.scale * 1000.0f + 0.5f);
        g_cfg.offsetDeciW = (int16_t)(fit.offset * 10.0f);
        applyCorrectionFromCfg();
        cfgSave();
    }
    curveSave();
    publishCurve();
}

static void calWriteCb(uint16_t /*conn*/, BLECharacteristic* /*chr*/, uint8_t* data,
                       uint16_t len) {
    if (len < 2 || data[0] != PROTO_VER) return;
    switch ((CalCmd)data[1]) {
        case CalCmd::Start: {
            // [ver,1, refFilter[19]] — the reference meter to read alongside the source (DUT).
            memset(g_refFilter, 0, sizeof(g_refFilter));
            if (len > 2) {
                size_t k = len - 2;
                if (k > sizeof(g_refFilter) - 1) k = sizeof(g_refFilter) - 1;
                memcpy(g_refFilter, data + 2, k);
            }
            g_cal.start();
            g_calibrating = true;
            if (!Bluefruit.Scanner.isRunning()) Bluefruit.Scanner.start(0);  // find the reference
            Serial.printf("[cal] started; DUT=source, reference='%s'\n", g_refFilter);
            break;
        }
        case CalCmd::Cancel:
            g_cal.cancel();
            g_calibrating = false;
            if (g_refConnected && g_refConnHandle != BLE_CONN_HANDLE_INVALID)
                Bluefruit.disconnect(g_refConnHandle);
            Serial.println("[cal] cancelled");
            break;
        case CalCmd::Save:
            // Finish (fit) if still collecting, then apply the fitted correction live + persist.
            if (g_cal.collecting()) g_cal.finish();
            if (g_cal.fitted()) {
                applyFit(g_cal.fit());
                Serial.printf("[cal] SAVED; residual %.1f W; %u pairs -> correction applied\n",
                              (double)g_cal.residualW(), (unsigned)g_cal.pairCount());
            } else {
                Serial.println("[cal] save rejected (not enough pairs to fit)");
            }
            g_calibrating = false;
            if (g_refConnected && g_refConnHandle != BLE_CONN_HANDLE_INVALID)
                Bluefruit.disconnect(g_refConnHandle);
            break;
        case CalCmd::Discard:
            g_cal.cancel();
            g_calibrating = false;
            if (g_refConnected && g_refConnHandle != BLE_CONN_HANDLE_INVALID)
                Bluefruit.disconnect(g_refConnHandle);
            Serial.println("[cal] fit discarded");
            break;
    }
    notifyCalState();
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
    curveLoad();

    // RGB status LED (active-low)
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    setLed(false, false, false);

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
    // TWO peripheral links (head unit + web app/Garmin concurrently) and TWO central links (the
    // source/DUT + the reference meter during calibration). 4 links total; the S140 default is 20.
    Bluefruit.begin(/*peripheral*/ 2, /*central*/ 2);
    Bluefruit.setTxPower(4);
    Bluefruit.setName(g_cfg.outName);

    // --- output: standard CPS peripheral (a head unit pairs to this) ---
    bledis.setManufacturer("SB20 Proxy");
    bledis.setModel("nRF Bridge");
    bledis.begin();
    bledfu.begin();  // buttonless DFU: a DFU write reboots into the bootloader's BLE-OTA mode so
                     // `adafruit-nrfutil dfu ble` can push new firmware — no USB reflash needed
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
    chCurve.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE);
    chCurve.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    chCurve.setMaxLen(2 + CURVE_MAX_POINTS * 4);
    chCurve.setWriteCallback(curveWriteCb);
    chCurve.begin();
    publishCurve();  // read-back reflects the loaded curve
    chCal.setProperties(CHR_PROPS_WRITE | CHR_PROPS_NOTIFY);
    chCal.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    chCal.setMaxLen(2 + 19);  // write: [ver, cmd, refFilter...]
    chCal.setWriteCallback(calWriteCb);
    chCal.begin();
    chScan.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
    chScan.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    chScan.setMaxLen(2 + SCAN_MAX * SCAN_SLOT);
    chScan.begin();

    // --- source: central scanning for a CPS meter (+ the reference during calibration) ---
    clientCps.begin();
    clientMeas.setNotifyCallback(measNotifyCb);
    clientMeas.begin();
    clientSrcCp.begin();  // the source's control point (zero-forward target)
    refCps.begin();
    refMeas.setNotifyCallback(refMeasNotifyCb);
    refMeas.begin();
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

// Serial self-test: capture N samples via the PRODUCTION path (imu.readRaw* -> g_cap.add) and
// print stats over USB CDC — proves the sensor->buffer chain without touching BLE (the desktop
// BLE stack + shared airspace were fighting the bench, 2026-07-05). Trigger: send "IMUTEST\n".
static void imuSelfTest() {
    if (!g_imuOk) { Serial.println("[imutest] IMU NOT PRESENT"); return; }
    const uint8_t rate = 52;
    const uint16_t want = 260;  // ~5 s
    g_cap.start(millis(), rate);
    uint32_t nextUs = micros();
    const uint32_t periodUs = 1000000UL / rate;
    Serial.printf("[imutest] capturing %u samples @%uHz via the production path...\n", want, rate);
    while (g_cap.count() < want) {
        const uint32_t nowUs = micros();
        if ((int32_t)(nowUs - nextUs) >= 0) {
            nextUs += periodUs;
            int16_t s[6] = {imu.readRawAccelX(), imu.readRawAccelY(), imu.readRawAccelZ(),
                            imu.readRawGyroX(),  imu.readRawGyroY(),  imu.readRawGyroZ()};
            if (!g_cap.add(s)) break;
        }
    }
    // Analyse the captured buffer: gravity magnitude, per-axis mean+noise, distinct-sample count.
    const uint32_t n = g_cap.count();
    double sm = 0;     // sum of |a| in g
    double axMean = 0, ayMean = 0, azMean = 0;
    int16_t axMin = 32767, axMax = -32768;
    uint32_t distinct = 0, dupes = 0;
    const int16_t* prev = nullptr;
    // two passes (n is small): means first
    for (uint32_t i = 0; i < n; ++i) {
        const int16_t* p = g_cap.sample(i);
        axMean += p[0]; ayMean += p[1]; azMean += p[2];
        const double g = sqrt((double)p[0]*p[0] + (double)p[1]*p[1] + (double)p[2]*p[2]) * 0.000488;
        sm += g;
        if (p[0] < axMin) axMin = p[0];
        if (p[0] > axMax) axMax = p[0];
        if (prev && p[0]==prev[0] && p[1]==prev[1] && p[2]==prev[2] &&
            p[3]==prev[3] && p[4]==prev[4] && p[5]==prev[5]) dupes++;
        prev = p;
    }
    axMean /= n; ayMean /= n; azMean /= n;
    // noise: stdev of ax as a liveness proxy (a frozen read => 0)
    double axVar = 0;
    for (uint32_t i = 0; i < n; ++i) { double d = g_cap.sample(i)[0] - axMean; axVar += d*d; }
    const double axSd = sqrt(axVar / n);
    Serial.printf("[imutest] n=%lu  gravity|a| mean=%.3fg\n", (unsigned long)n, sm / n);
    Serial.printf("[imutest] accel LSB means: ax=%.0f ay=%.0f az=%.0f  (=%.2f,%.2f,%.2f g)\n",
                  axMean, ayMean, azMean, axMean*0.000488, ayMean*0.000488, azMean*0.000488);
    Serial.printf("[imutest] ax noise stdev=%.1f LSB  range=[%d,%d]  consec-dupes=%lu/%lu\n",
                  axSd, axMin, axMax, (unsigned long)dupes, (unsigned long)n);
    const bool live = (n >= want - rate) && (sm/n > 0.9 && sm/n < 1.1) && axSd > 0.5 && dupes < n/2;
    Serial.printf("[imutest] VERDICT: %s\n", live
        ? "PASS - live sensor (real 1g gravity + real per-sample ADC noise + no frozen repeats)"
        : "FAIL - check gravity/noise above");
    g_cap.erase();
}

void loop() {
    const uint32_t now = millis();

    // Serial self-test commands (USB CDC) — desk diagnostics, bench-verify the correction logic
    // without a BLE client (the desktop Windows GATT cache fights repeated reflashes):
    //   IMUTEST · SINGLE1/SINGLE0 (single-sided x2) · CURVE (200W->1.25 test curve) · LINEAR
    //   (clear curve) · ZERO (trigger source zero-forward) · SHOW (print correction state).
    static char cmd[16];
    static uint8_t ci = 0;
    while (Serial.available()) {
        char ch = Serial.read();
        if (ch == '\n' || ch == '\r') {
            cmd[ci] = 0;
            if (strcmp(cmd, "IMUTEST") == 0) imuSelfTest();
            else if (strcmp(cmd, "SINGLE1") == 0) { g_cfg.singleSided = true; cfgSave(); Serial.println("[test] single-sided x2 ON"); }
            else if (strcmp(cmd, "SINGLE0") == 0) { g_cfg.singleSided = false; cfgSave(); Serial.println("[test] single-sided OFF"); }
            else if (strcmp(cmd, "CURVE") == 0) {
                g_corr.curve = CorrectionCurve{};
                g_corr.curve.add(100, 1.0f); g_corr.curve.add(200, 1.25f); g_corr.curve.add(300, 1.25f);
                curveSave(); Serial.println("[test] curve set: 100W:1.00 200W:1.25 300W:1.25");
            }
            else if (strcmp(cmd, "LINEAR") == 0) { g_corr.curve = CorrectionCurve{}; curveSave(); Serial.println("[test] curve cleared -> scale/offset"); }
            else if (strcmp(cmd, "ZERO") == 0) { g_pendSourceZero = true; Serial.println("[test] source zero-forward queued"); }
            else if (strcmp(cmd, "CALTEST") == 0) {
                // Prove the fit math on-device: feed synthetic pairs where the DUT reads 10% LOW
                // (dut = ref*0.9) across a power sweep; the fit should produce a curve that brings
                // the DUT up ~1.111x. Uses the exact pure CalibrationSession.
                CalibrationSession t(20);
                t.start();
                uint32_t tm = 1000;
                for (int rep = 0; rep < 8; ++rep)
                    for (int refW = 100; refW <= 300; refW += 50) {
                        t.onRef((float)refW, tm);
                        t.onDut(refW * 0.9f, tm);  // DUT reads 10% low
                        tm += 100;
                    }
                bool ok = t.finish();
                Serial.printf("[caltest] pairs=%u fit=%s residual=%.2fW\n", (unsigned)t.pairCount(),
                              ok ? "OK" : "FAIL", (double)t.residualW());
                const Correction& f = t.fit();
                if (!f.curve.empty()) {
                    for (auto& pt : f.curve.points)
                        Serial.printf("[caltest]   %.0fW -> x%.3f\n", (double)pt.power_w, (double)pt.factor);
                    // apply to a 200W reading: should recover ~200 (dut 180 -> corrected ~200)
                    PowerReading r; r.power_w = 180;
                    Serial.printf("[caltest] apply: dut 180W -> %dW (expect ~200)\n", f.apply(r).power_w);
                } else {
                    Serial.printf("[caltest] linear fit: scale=%.3f offset=%.1f\n", (double)f.scale, (double)f.offset);
                }
            }
            else if (strcmp(cmd, "SHOW") == 0)
                Serial.printf("[test] scale=%.3f offset=%.1f single2x=%d curve=%upts src=%dW->out=%dW\n",
                    (double)g_corr.scale, (double)g_corr.offset, g_cfg.singleSided,
                    (unsigned)g_corr.curve.points.size(), (int)g_lastSrc.power_w, (int)g_lastOut.power_w);
            else if (strcmp(cmd, "SCANLIST") == 0) {
                std::vector<SourceCandidate> s = dedupeAndSortSources(g_candidates);
                Serial.printf("[scanlist] %u nearby meters/trainers:\n", (unsigned)s.size());
                for (auto& c : s)
                    Serial.printf("[scanlist]   %-20s %ddBm %s%s%s\n", c.name.c_str(), c.rssi,
                                  c.isCps ? "CPS " : "", c.isFtms ? "FTMS " : "",
                                  c.isStagesCrank ? "StagesCrank" : "");
            }
            ci = 0;
        } else if (ci < sizeof(cmd) - 1) {
            cmd[ci++] = ch;
        }
    }

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

    // Zero-offset forwarding: a head unit hit "calibrate" -> write CP 0x0C to the source meter's
    // control point (from loop context, never inside the CP callback). Fire-and-forget, like the
    // ESP32 seam; the meter's zero result comes back async (we already replied to the head unit).
    if (g_pendSourceZero) {
        g_pendSourceZero = false;
        if (g_srcConnected && clientSrcCp.discovered()) {
            const uint8_t z[1] = {CP_OP_START_OFFSET_COMP};  // 0x0C
            clientSrcCp.write(z, sizeof(z));
            Serial.println("[bridge] forwarded zero-offset (0x0C) to the source meter");
        } else {
            Serial.println("[bridge] zero requested but source has no control point");
        }
    }

    // Publish the scanned-source list to the picker, throttled to 2 s when new devices appeared.
    static uint32_t scanPubAt = 0;
    if (g_candDirty && now - scanPubAt >= 2000) {
        scanPubAt = now;
        g_candDirty = false;
        publishScanList();
    }

    // Calibration state notify @1 Hz while collecting (pair count + coverage climb for the wizard).
    static uint32_t calAt = 0;
    if (g_calibrating && now - calAt >= 1000) {
        calAt = now;
        notifyCalState();
    }

    // RGB status LED (glanceable on the track): calibrating=magenta · recording=red · source
    // linked=green · else blue-pulse (searching).
    static uint32_t ledAt = 0;
    static bool pulse = false;
    if (now - ledAt >= 500) {
        ledAt = now;
        if (g_calibrating) { pulse = !pulse; setLed(pulse, false, pulse); }  // magenta pulse
        else if (g_recState == RecState::Recording) setLed(true, false, false);
        else if (g_srcConnected) setLed(false, true, false);
        else { pulse = !pulse; setLed(false, false, pulse); }
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
