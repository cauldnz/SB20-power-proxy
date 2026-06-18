#pragma once
#include <cstdint>
#include <string>

#include "PerfMonitor.h"

// The /stats observability payload — the perf analogue of Status.h's ProxyStatus/renderStatusJson.
// Pure (no Arduino / ESP-IDF), so the JSON shape + the reset-reason mapping + the fragmentation
// math are host-tested; main.cpp fills the struct from the real esp_* reads (the seam).
namespace sb20proxy {

struct PerfStats {
    LoopStats loop;             // from PerfMonitor::summary()
    uint32_t windowMs = 0;      // wall-time since the last reset (for rates)
    // memory
    uint32_t freeHeap = 0;
    uint32_t minFreeHeap = 0;   // esp_get_minimum_free_heap_size()
    uint32_t largestBlock = 0;  // heap_caps_get_largest_free_block()
    // tasks / cpu
    uint32_t loopStackHwm = 0;  // bytes free (uxTaskGetStackHighWaterMark * word)
    int idlePct = -1;           // -1 = unknown
    // reboot evidence (persisted in NVS across the reboot)
    uint32_t rebootCount = 0;
    int resetReasonCode = 0;    // (int)esp_reset_reason()
    std::string swReason;       // detail for a sw-reset (e.g. "loop_stall"); "" = plain/OTA reboot
    uint32_t uptimeMs = 0;
};

// Heap fragmentation: how much of the free heap is NOT in the single largest block. Flat free-heap
// but rising frag = fragmentation creep (vs a leak, which shrinks free-heap).
inline int fragPct(uint32_t freeHeap, uint32_t largestBlock) {
    if (freeHeap == 0 || largestBlock >= freeHeap) return 0;
    return (int)(100 - (uint64_t)largestBlock * 100 / freeHeap);
}

// esp_reset_reason_t codes (stable in ESP-IDF). Mapped here as plain ints so the function stays
// pure/host-testable (the IDF enum isn't available on the host).
inline const char* resetReasonName(int code) {
    switch (code) {
        case 1:  return "poweron";
        case 2:  return "ext";
        case 3:  return "sw";        // esp_restart() — our boot-guard / OTA reboot
        case 4:  return "panic";     // crash
        case 5:  return "int_wdt";   // interrupt watchdog
        case 6:  return "task_wdt";  // TASK watchdog -> a task starved (Phase B)
        case 7:  return "other_wdt";
        case 8:  return "deepsleep";
        case 9:  return "brownout";  // power/USB sag
        case 10: return "sdio";
        default: return "unknown";
    }
}

inline std::string renderPerfJson(const PerfStats& s) {
    std::string j = "{";
    j += "\"loop_count\":" + std::to_string(s.loop.count);
    j += ",\"loop_mean_us\":" + std::to_string(s.loop.meanUs);
    j += ",\"loop_p50_us\":" + std::to_string(s.loop.p50Us);
    j += ",\"loop_p95_us\":" + std::to_string(s.loop.p95Us);
    j += ",\"loop_max_us\":" + std::to_string(s.loop.maxUs);
    j += ",\"stalls_50ms\":" + std::to_string(s.loop.stalls50);
    j += ",\"stalls_200ms\":" + std::to_string(s.loop.stalls200);
    j += ",\"window_ms\":" + std::to_string(s.windowMs);
    j += ",\"free_heap\":" + std::to_string(s.freeHeap);
    j += ",\"min_free_heap\":" + std::to_string(s.minFreeHeap);
    j += ",\"largest_block\":" + std::to_string(s.largestBlock);
    j += ",\"frag_pct\":" + std::to_string(fragPct(s.freeHeap, s.largestBlock));
    j += ",\"loop_stack_hwm\":" + std::to_string(s.loopStackHwm);
    j += ",\"idle_pct\":" + std::to_string(s.idlePct);
    j += ",\"reboot_count\":" + std::to_string(s.rebootCount);
    j += ",\"reset_reason\":\"" + std::string(resetReasonName(s.resetReasonCode)) + "\"";
    j += ",\"sw_reason\":\"" + s.swReason + "\"";
    j += ",\"uptime_ms\":" + std::to_string(s.uptimeMs);
    j += "}";
    return j;
}

}  // namespace sb20proxy
