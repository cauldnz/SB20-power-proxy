#pragma once
// Pure JSON serializers for the shared web SPA's HTTP transport (web/HTTP-API.md). The ESP32 serves
// the SAME index.html the nRF build does; its `HttpTransport` reads these endpoints instead of GATT.
// /status + /workout/state already have serializers (Status.h / WorkoutEngine.h); these add the ones
// the SPA also needs — /scan (source picker), /config (identity display), /curve (calibration profile
// export). Host-tested.
#include <cstdio>
#include <string>
#include <vector>

#include "Correction.h"
#include "MeterCompare.h"  // #10 A/B compare: stats + torque bands + power×cadence grid + pairs
#include "RuntimeConfig.h"
#include "SourceCandidate.h"
#include "Status.h"  // jsonEscape

namespace sb20proxy {

// GET /scan -> the SPA's normalized Scan list: nearby meters/trainers for the source picker.
inline std::string renderScanJson(const std::vector<SourceCandidate>& srcs) {
    std::string j = "{\"devices\":[";
    for (size_t i = 0; i < srcs.size(); ++i) {
        const SourceCandidate& c = srcs[i];
        if (i) j += ",";
        j += "{\"name\":\"" + jsonEscape(c.name) + "\",\"rssi\":" + std::to_string(c.rssi);
        j += ",\"cps\":" + std::string(c.isCps ? "true" : "false");
        j += ",\"ftms\":" + std::string(c.isFtms ? "true" : "false");
        j += ",\"crank\":" + std::string(c.isStagesCrank ? "true" : "false") + "}";
    }
    j += "]}";
    return j;
}

// GET /config -> the SPA's normalized Config. NOTE the model gap: the nRF Config is scalar
// scale/offset; the ESP32's correction is a CorrectionCurve, so scale/offset are reported as the
// linear baseline (1.0/0) and `has_curve` flags whether a fitted curve is active. The mapping
// fields — single-sided, source name, advertised identity — round-trip via the existing /setup/save.
inline std::string renderConfigJson(const RuntimeConfig& c) {
    std::string j = "{\"scale\":1.0,\"offset\":0.0";
    j += ",\"single_sided\":" + std::string(c.singleSidedDouble ? "true" : "false");
    j += ",\"src_filter\":\"" + jsonEscape(c.meterNameFilter) + "\"";
    j += ",\"out_name\":\"" + jsonEscape(c.spoofName) + "\"";
    // Broadcast mode so the SPA's spoof/corrector selector reflects the device (matches /status).
    j += ",\"mode\":\"" + std::string(c.mode == ProxyMode::Corrector ? "corrector" : "spoof") + "\"";
    j += ",\"has_curve\":" + std::string(c.curve.points.empty() ? "false" : "true");
    j += "}";
    return j;
}

// GET /curve -> the device's correction curve as portable breakpoints, so the SPA can wrap it in a
// calibration profile the OTHER device (or the desk tooling) can load. Breakpoints are [power_w,
// factor] at the same precision as curveToString / the Python profile (1 dp power, 4 dp factor).
inline std::string renderCurveJson(const CorrectionCurve& curve) {
    std::string j = "{\"has_curve\":" + std::string(curve.points.empty() ? "false" : "true");
    j += ",\"curve\":[";
    char buf[48];
    for (size_t i = 0; i < curve.points.size(); ++i) {
        if (i) j += ",";
        std::snprintf(buf, sizeof(buf), "[%.1f,%.4f]", curve.points[i].power_w, curve.points[i].factor);
        j += buf;
    }
    j += "]}";
    return j;
}

// GET /compare -> the #10 A/B deep-dive payload the web Compare view renders: summary + per-torque-band
// bias + the power×cadence bias grid (the heatmap) + a downsampled pair list (for Bland-Altman). Empty
// bands/cells are `null`. Compact arrays keep it ~2-3 KB. Host-tested.
inline std::string renderCompareJson(const MeterCompare& mc, const std::string& aName,
                                     const std::string& bName) {
    const MeterCompareStats s = mc.stats();
    char buf[80];
    std::string j = "{\"valid\":" + std::string(s.valid ? "true" : "false");
    j += ",\"aName\":\"" + jsonEscape(aName) + "\",\"bName\":\"" + jsonEscape(bName) + "\"";
    std::snprintf(buf, sizeof(buf),
                  ",\"aW\":%d,\"bW\":%d,\"deltaW\":%d,\"ratio\":%.4f,\"biasPct\":%.2f,\"nPairs\":%d",
                  s.aWatts, s.bWatts, s.deltaW, (double)s.meanRatio, (double)s.meanBiasPct, s.nPairs);
    j += buf;
    // bias by torque band
    j += ",\"tqBandNm\":" + std::to_string(MeterCompare::kTorqueBandNm) + ",\"tqBias\":[";
    const std::vector<MeterBand> tb = mc.torqueBands();
    for (size_t i = 0; i < tb.size(); ++i) {
        if (i) j += ",";
        if (tb[i].nPairs > 0) { std::snprintf(buf, sizeof(buf), "%.2f", (double)tb[i].meanBiasPct); j += buf; }
        else j += "null";
    }
    j += "]";
    // power×cadence bias grid (the heatmap)
    const MeterCompare::Grid2D g = mc.grid2d();
    j += ",\"grid\":{\"pW\":" + std::to_string(g.pBinW) + ",\"cLo\":" + std::to_string(g.cBinLo) +
         ",\"cW\":" + std::to_string(g.cBinW) + ",\"P\":" + std::to_string(MeterCompare::kGridPBins) +
         ",\"C\":" + std::to_string(MeterCompare::kGridCBins) + ",\"bias\":[";
    for (int pi = 0; pi < MeterCompare::kGridPBins; ++pi) {
        if (pi) j += ",";
        j += "[";
        for (int ci = 0; ci < MeterCompare::kGridCBins; ++ci) {
            if (ci) j += ",";
            if (g.cell[pi][ci].nPairs > 0) {
                std::snprintf(buf, sizeof(buf), "%.2f", (double)g.cell[pi][ci].meanBiasPct);
                j += buf;
            } else j += "null";
        }
        j += "]";
    }
    j += "]}";
    // downsampled (a,b) pairs for the Bland-Altman scatter
    j += ",\"pairs\":[";
    const std::vector<MeterCompare::SamplePair> sp = mc.samplePairs(120);
    for (size_t i = 0; i < sp.size(); ++i) {
        if (i) j += ",";
        std::snprintf(buf, sizeof(buf), "[%d,%d]", sp[i].a, sp[i].b);
        j += buf;
    }
    j += "]}";
    return j;
}

}  // namespace sb20proxy
