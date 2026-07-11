#pragma once
// BridgeService — the Bridge GATT control/telemetry service (GATT.md) as a seam class.
//
// This owns the Bridge service's 53423230-XXXX-… UUIDs, its BLECharacteristic objects, and the
// one place that shapes the GATT table (properties / permission / length / write-callback / begin,
// in the exact registration order — which matters: each 128-bit characteristic consumes a
// SoftDevice vendor-UUID slot, see configUuid128Count in main.cpp). Extracting it converts the nRF
// main.cpp toward the ESP32's shape (a thin wiring file over seam classes). This first slice moves
// the GATT-table definition + wiring; the write-callback *bodies* stay in main.cpp (they are the
// application's command dispatcher and touch a broad set of globals — migrating them behind an
// explicit context is a follow-up). The characteristic objects are public so those callbacks and
// the notify/publish helpers reach them unchanged.
//
// Behaviour-preserving: same objects, same GATT shape, same registration order as the inline
// setup() block it replaces. Header is included by the single main.cpp TU; the file-scope UUID
// arrays keep internal linkage (and avoid the gnu++11 class-static-constexpr ODR trap — a class
// static constexpr array passed to a constructor needs an out-of-line def the header can't give).
#include <bluefruit.h>

#include "Proto.h"  // nrfbridge:: STATUS_LEN / CONFIG_LEN / RECSTATE_LEN / CURVE_MAX_POINTS /
                    // SCAN_MAX / SCAN_SLOT / BUTTONS_LEN

// 53423230-XXXX-4bd9-a4ae-1b4e2c633a1d, little-endian; bytes [10],[11] carry XXXX.
#define BRIDGE_SVC_UUID(id)                                                                   \
    {0x1d, 0x3a, 0x63, 0x2c, 0x4e, 0x1b, 0xae, 0xa4, 0xd9, 0x4b, (uint8_t)(id),               \
     (uint8_t)((id) >> 8), 0x30, 0x32, 0x42, 0x53}
static const uint8_t kUuidBridgeSvc[16] = BRIDGE_SVC_UUID(0x0000);
static const uint8_t kUuidBridgeStatus[16] = BRIDGE_SVC_UUID(0x0001);
static const uint8_t kUuidBridgeConfig[16] = BRIDGE_SVC_UUID(0x0002);
static const uint8_t kUuidBridgeRecCtl[16] = BRIDGE_SVC_UUID(0x0003);
static const uint8_t kUuidBridgeRecData[16] = BRIDGE_SVC_UUID(0x0004);
static const uint8_t kUuidBridgeCurve[16] = BRIDGE_SVC_UUID(0x0005);
static const uint8_t kUuidBridgeCal[16] = BRIDGE_SVC_UUID(0x0006);
static const uint8_t kUuidBridgeScan[16] = BRIDGE_SVC_UUID(0x0007);
static const uint8_t kUuidBridgeWk[16] = BRIDGE_SVC_UUID(0x0008);
static const uint8_t kUuidBridgeButtons[16] = BRIDGE_SVC_UUID(0x0009);
#undef BRIDGE_SVC_UUID

class BridgeService {
  public:
    BridgeService()
        : svc(kUuidBridgeSvc),
          chStatus(kUuidBridgeStatus),
          chConfig(kUuidBridgeConfig),
          chRecCtl(kUuidBridgeRecCtl),
          chRecData(kUuidBridgeRecData),
          chCurve(kUuidBridgeCurve),
          chCal(kUuidBridgeCal),
          chScan(kUuidBridgeScan),
          chWk(kUuidBridgeWk),
          chButtons(kUuidBridgeButtons) {}

    // Wire up the whole Bridge service. Order is faithful to the original inline block (do not
    // reorder — see the vendor-UUID-slot note above). The write-callback bodies live in main.cpp
    // and are passed in here.
    void begin(BLECharacteristic::write_cb_t configCb, BLECharacteristic::write_cb_t recCtlCb,
               BLECharacteristic::write_cb_t curveCb, BLECharacteristic::write_cb_t calCb,
               BLECharacteristic::write_cb_t wkCb, BLECharacteristic::write_cb_t buttonsCb) {
        using namespace nrfbridge;
        svc.begin();
        chStatus.setProperties(CHR_PROPS_NOTIFY | CHR_PROPS_READ);
        chStatus.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
        chStatus.setFixedLen(STATUS_LEN);
        chStatus.begin();
        chConfig.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE);
        chConfig.setPermission(SECMODE_OPEN, SECMODE_OPEN);
        chConfig.setFixedLen(CONFIG_LEN);
        chConfig.setWriteCallback(configCb);
        chConfig.begin();
        chRecCtl.setProperties(CHR_PROPS_WRITE | CHR_PROPS_NOTIFY);
        chRecCtl.setPermission(SECMODE_OPEN, SECMODE_OPEN);
        chRecCtl.setMaxLen(RECSTATE_LEN);  // the NOTIFY needs 12 (maxLen 4 truncated it - bench)
        chRecCtl.setWriteCallback(recCtlCb);
        chRecCtl.begin();
        chRecData.setProperties(CHR_PROPS_NOTIFY);
        chRecData.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
        chRecData.setMaxLen(180);
        chRecData.begin();
        chCurve.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE);
        chCurve.setPermission(SECMODE_OPEN, SECMODE_OPEN);
        chCurve.setMaxLen(2 + CURVE_MAX_POINTS * 4);
        chCurve.setWriteCallback(curveCb);
        chCurve.begin();
        chCal.setProperties(CHR_PROPS_WRITE | CHR_PROPS_NOTIFY);
        chCal.setPermission(SECMODE_OPEN, SECMODE_OPEN);
        chCal.setMaxLen(2 + 19);  // write: [ver, cmd, refFilter...]
        chCal.setWriteCallback(calCb);
        chCal.begin();
        chScan.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
        chScan.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
        chScan.setMaxLen(2 + SCAN_MAX * SCAN_SLOT);
        chScan.begin();
        chWk.setProperties(CHR_PROPS_WRITE | CHR_PROPS_NOTIFY);
        chWk.setPermission(SECMODE_OPEN, SECMODE_OPEN);
        chWk.setMaxLen(2 + 19);  // write: [ver, cmd, arg...]
        chWk.setWriteCallback(wkCb);
        chWk.begin();
        // Buttons (0009): the SB20-shifter -> action binding + sink enable (web-configurable).
        chButtons.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE);
        chButtons.setPermission(SECMODE_OPEN, SECMODE_OPEN);
        chButtons.setFixedLen(BUTTONS_LEN);
        chButtons.setWriteCallback(buttonsCb);
        chButtons.begin();
    }

    // Public: the Bridge write callbacks + the notify/publish helpers in main.cpp read/notify
    // these directly (this slice moves the table + wiring, not the callback bodies).
    BLEService svc;
    BLECharacteristic chStatus;    // 0001 live telemetry @2 Hz
    BLECharacteristic chConfig;    // 0002 correction + identity + routing
    BLECharacteristic chRecCtl;    // 0003 recording control + state
    BLECharacteristic chRecData;   // 0004 chunked IMU download
    BLECharacteristic chCurve;     // 0005 power->factor correction curve
    BLECharacteristic chCal;       // 0006 on-device calibration control + state
    BLECharacteristic chScan;      // 0007 nearby meters/trainers for the picker
    BLECharacteristic chWk;        // 0008 FTMS erg workout control + state
    BLECharacteristic chButtons;   // 0009 SB20-shifter -> action binding + sink enable
};
