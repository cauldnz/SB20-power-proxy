#pragma once
#include <string>
#include <vector>

#include "RuntimeConfig.h"
#include "Status.h"

namespace sb20proxy {

// The tester diagnostic — a plain-text report a beta tester saves from GET /diag and sends us when
// their meter isn't recognised or the power looks wrong. It carries everything we need to add their
// meter offline (the real-data-first way) without a board coming back: firmware/health, the runtime
// config, the live status, and the **raw CPS frames** the meter actually sent (the bytes a new codec
// is grounded in). Pure (no Arduino) — host-tested; the frame ring + HTTP serving are the seam.
//
// `recentFrames` are hex strings of the source meter's most recent Cycling Power Measurement (0x2A63)
// notifications, oldest→newest. Plain text (not JSON) so a tester can read it and we can grep it.
inline std::string renderDiagReport(const RuntimeConfig& cfg, const ProxyStatus& st,
                                    const std::vector<std::string>& recentFrames) {
    const char* source = st.mock ? "mock" : (st.sourceConnected ? "connected" : "searching");
    std::string r = "SB20 Proxy diagnostic\n";
    r += "====================\n";
    r += "fw=";
    r += st.fw;
    r += "  version=";
    r += st.version;
    r += "  build=";
    r += st.buildSha;
    r += " ";
    r += st.buildTime;
    r += "  uptime_ms=" + std::to_string(st.uptimeMs) + "  heap=" + std::to_string(st.freeHeap) +
         "  rssi=" + std::to_string(st.rssi) + "\n\n";

    r += "[config]\n";
    r += "  source_addr=" + cfg.meterAddress + "\n";
    r += "  source_name_filter=" + cfg.meterNameFilter + "\n";
    r += "  single_sided_double=" + std::string(cfg.singleSidedDouble ? "yes" : "no") + "\n";
    r += "  spoof_name=" + cfg.spoofName + "\n";
    r += "  spoof_serial=" + cfg.spoofSerial + "\n\n";

    r += "[status]\n";
    r += "  source=" + std::string(source) + "\n";
    r += "  source_connected_name=" + st.srcName + "\n";
    r += "  src_power_w=" + std::to_string(st.srcPowerW) +
         "  src_cadence_rpm=" + std::to_string(st.srcCadenceRpm) +
         "  src_balance_pct=" + std::to_string(st.srcBalanceHalfPct < 0 ? -1 : st.srcBalanceHalfPct / 2) +
         "\n";
    r += "  out_power_w=" + std::to_string(st.lastPowerW) +
         "  forwarded=" + std::to_string(st.forwarded) + "\n\n";

    r += "[meter frames] (CPS 0x2A63 raw hex, oldest first; " + std::to_string(recentFrames.size()) +
         " captured)\n";
    if (recentFrames.empty()) {
        r += "  (none yet — spin the meter so it sends, then reload /diag)\n";
    } else {
        for (const auto& f : recentFrames) r += "  " + f + "\n";
    }
    return r;
}

}  // namespace sb20proxy
