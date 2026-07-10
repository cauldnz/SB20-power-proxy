#pragma once
// BridgeConfigStore — the nRF's LittleFS persistence seam (architecture-remediation.md R1a).
//
// The pure pack/unpack codecs live in lib/bridge (Proto.h) + lib/proxy (Correction.h) and are
// host-tested; this wraps them in the flash read/write for the four persisted blobs — the config
// packet, the correction curve, the erg trainer name, and the SB20 button map — each in its own file.
// main.cpp keeps the live globals and the "apply to running state" policy (applyCorrectionFromCfg /
// applyButtons); this owns ONLY the I/O. Mirrors the ESP32 ConfigStore seam.
//
// Header-only + board-dependent (it needs Bluefruit's InternalFS), so it's a src/ seam, never compiled
// in the native test env. The Serial breadcrumbs match the pre-extraction wording verbatim so /log
// output is unchanged.
#include <Arduino.h>
#include <cstring>

#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

#include "Correction.h"  // sb20proxy::CorrectionCurve
#include "Proto.h"       // nrfbridge:: ConfigPacket / CurvePoint / ButtonsPacket + pack/unpack

namespace bridgestore {
using namespace Adafruit_LittleFS_Namespace;  // scoped: brings File + FILE_O_* into this namespace only

// LittleFS paths — one file per persisted blob.
constexpr const char* kCfgPath = "/bridge.cfg";
constexpr const char* kCurvePath = "/curve.bin";
constexpr const char* kTrainerPath = "/trainer.txt";
constexpr const char* kButtonsPath = "/buttons.bin";

// --- config packet ---------------------------------------------------------------------------
// Returns the stored config, or a default (advertised name "SB20 Bridge") when none is on flash.
inline nrfbridge::ConfigPacket loadConfig() {
    nrfbridge::ConfigPacket c;
    strcpy(c.outName, "SB20 Bridge");
    File f(InternalFS);
    if (f.open(kCfgPath, FILE_O_READ)) {
        uint8_t buf[nrfbridge::CONFIG_LEN];
        if (f.read(buf, sizeof(buf)) == (int)sizeof(buf)) {
            nrfbridge::ConfigPacket stored;
            if (nrfbridge::unpackConfig(buf, sizeof(buf), stored)) c = stored;
        }
        f.close();
        Serial.println("[bridge] config loaded from flash");
    } else {
        Serial.println("[bridge] no stored config - defaults (dev flashes wipe LittleFS)");
    }
    return c;
}
inline void saveConfig(const nrfbridge::ConfigPacket& c) {
    uint8_t buf[nrfbridge::CONFIG_LEN];
    nrfbridge::packConfig(c, buf);
    InternalFS.remove(kCfgPath);
    File f(InternalFS);
    if (f.open(kCfgPath, FILE_O_WRITE)) {
        f.write(buf, sizeof(buf));
        f.close();
    }
}

// --- correction curve (persisted separately from the scalar config) --------------------------
// Fills `out` and returns true when a non-empty curve was stored; leaves `out` untouched otherwise.
inline bool loadCurve(sb20proxy::CorrectionCurve& out) {
    File f(InternalFS);
    if (!f.open(kCurvePath, FILE_O_READ)) return false;
    uint8_t buf[2 + nrfbridge::CURVE_MAX_POINTS * 4];
    int rd = f.read(buf, sizeof(buf));
    f.close();
    nrfbridge::CurvePoint pts[nrfbridge::CURVE_MAX_POINTS];
    int n = (rd >= 2) ? nrfbridge::unpackCurve(buf, rd, pts) : -1;
    if (n <= 0) return false;
    out = sb20proxy::CorrectionCurve{};
    for (int i = 0; i < n; ++i) out.add(pts[i].powerW, pts[i].factorMilli / 1000.0f);
    Serial.printf("[bridge] correction curve loaded (%d points)\n", n);
    return true;
}
inline void saveCurve(const sb20proxy::CorrectionCurve& curve) {
    InternalFS.remove(kCurvePath);
    if (curve.empty()) return;
    File f(InternalFS);
    if (f.open(kCurvePath, FILE_O_WRITE)) {
        const uint8_t n = (uint8_t)curve.points.size();
        uint8_t buf[2 + nrfbridge::CURVE_MAX_POINTS * 4];
        nrfbridge::CurvePoint pts[nrfbridge::CURVE_MAX_POINTS];
        for (uint8_t i = 0; i < n && i < nrfbridge::CURVE_MAX_POINTS; ++i) {
            pts[i].powerW = (uint16_t)curve.points[i].power_w;
            pts[i].factorMilli = (uint16_t)(curve.points[i].factor * 1000.0f + 0.5f);
        }
        f.write(buf, nrfbridge::packCurve(pts, n, buf));
        f.close();
    }
}

// --- erg trainer name ------------------------------------------------------------------------
inline void loadTrainer(char* out, size_t cap) {
    File f(InternalFS);
    if (!f.open(kTrainerPath, FILE_O_READ)) return;
    int n = f.read((uint8_t*)out, cap - 1);
    f.close();
    if (n > 0) {
        out[n] = 0;
        Serial.printf("[erg] trainer configured: '%s'\n", out);
    }
}
inline void saveTrainer(const char* name) {
    InternalFS.remove(kTrainerPath);
    if (!name || !name[0]) return;
    File f(InternalFS);
    if (f.open(kTrainerPath, FILE_O_WRITE)) {
        f.write((const uint8_t*)name, strlen(name));
        f.close();
    }
}

// --- SB20 button map -------------------------------------------------------------------------
// Fills `out` and returns true when a valid map was stored; the caller applies it to live state.
inline bool loadButtons(nrfbridge::ButtonsPacket& out) {
    File f(InternalFS);
    if (!f.open(kButtonsPath, FILE_O_READ)) return false;
    uint8_t buf[nrfbridge::BUTTONS_LEN];
    int n = f.read(buf, sizeof(buf));
    f.close();
    return n == (int)nrfbridge::BUTTONS_LEN && nrfbridge::unpackButtons(buf, nrfbridge::BUTTONS_LEN, out);
}
inline void saveButtons(const nrfbridge::ButtonsPacket& b) {
    InternalFS.remove(kButtonsPath);
    uint8_t buf[nrfbridge::BUTTONS_LEN];
    nrfbridge::packButtons(b, buf);
    File f(InternalFS);
    if (f.open(kButtonsPath, FILE_O_WRITE)) {
        f.write(buf, sizeof(buf));
        f.close();
    }
}

}  // namespace bridgestore
