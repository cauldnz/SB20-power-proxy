#pragma once
// Pure JSON serializers for the shared web SPA's HTTP transport (web/HTTP-API.md). The ESP32 serves
// the SAME index.html the nRF build does; its `HttpTransport` reads these endpoints instead of GATT.
// /status + /workout/state already have serializers (Status.h / WorkoutEngine.h); these add the two
// the SPA also needs — /scan (source picker) and /config (correction + identity display). Host-tested.
#include <string>
#include <vector>

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
    j += ",\"has_curve\":" + std::string(c.curve.points.empty() ? "false" : "true");
    j += "}";
    return j;
}

}  // namespace sb20proxy
