#pragma once
#include <algorithm>
#include <string>
#include <vector>

namespace sb20proxy {

// One device seen by the source scan (a BLE central discovery pass). `isStagesCrank` marks a
// "Stages …" advertiser so the picker can label a surviving crank (the crank-rescue use case) and
// so we never offer our own spoof as a source. Filled by BleMeterClient (the seam); the
// accumulation / dedup / sort below is pure + host-tested. Kept in its own light header so the
// meter client can use it without pulling in the whole ConfigPage renderer.
struct SourceCandidate {
    std::string address;   // lowercase colon-separated BLE address (what gets pinned)
    std::string name;      // advertised name ("" if nameless, e.g. a WinRT rig)
    int rssi = -100;       // dBm, closer to 0 = stronger
    bool isCps = false;    // advertises Cycling Power Service 0x1818
    bool isStagesCrank = false;  // name starts with "Stages " (a native crank)
    bool isFtms = false;   // advertises Fitness Machine Service 0x1826 (an erg-able trainer)
};

// Add/refresh one scanned device in a bounded candidate list, in place. Dedups by address (keeps
// the strongest RSSI, fills a name/flags if a later pass had them), and caps the list at `cap`
// (when full, a NEW address replaces the current weakest — so the list trends toward the closest
// devices). Pure; BleMeterClient calls this from its scan callback. Returns nothing.
inline void addCandidate(std::vector<SourceCandidate>& list, const SourceCandidate& c, size_t cap) {
    if (c.address.empty()) return;
    for (auto& e : list) {
        if (e.address == c.address) {
            if (c.rssi > e.rssi) e.rssi = c.rssi;
            if (e.name.empty() && !c.name.empty()) e.name = c.name;
            e.isCps = e.isCps || c.isCps;
            e.isStagesCrank = e.isStagesCrank || c.isStagesCrank;
            e.isFtms = e.isFtms || c.isFtms;
            return;
        }
    }
    if (list.size() < cap) {
        list.push_back(c);
        return;
    }
    // Full: replace the weakest entry only if the newcomer is stronger than it.
    auto weakest = std::min_element(list.begin(), list.end(),
                                    [](const SourceCandidate& a, const SourceCandidate& b) {
                                        return a.rssi < b.rssi;
                                    });
    if (weakest != list.end() && c.rssi > weakest->rssi) *weakest = c;
}

// Collapse a candidate list into what the picker shows: drop entries with no address, merge
// duplicates by address (keep the strongest RSSI), sort strongest-first. Pure; the renderer calls
// it so the page is correct regardless of scan order. Stable: equal-RSSI ties keep scan order.
inline std::vector<SourceCandidate> dedupeAndSortSources(const std::vector<SourceCandidate>& in) {
    std::vector<SourceCandidate> out;
    for (const auto& d : in) {
        if (d.address.empty()) continue;
        bool merged = false;
        for (auto& e : out) {
            if (e.address == d.address) {
                if (d.rssi > e.rssi) e.rssi = d.rssi;
                if (e.name.empty()) e.name = d.name;
                e.isCps = e.isCps || d.isCps;
                e.isStagesCrank = e.isStagesCrank || d.isStagesCrank;
                e.isFtms = e.isFtms || d.isFtms;
                merged = true;
                break;
            }
        }
        if (!merged) out.push_back(d);
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const SourceCandidate& a, const SourceCandidate& b) { return a.rssi > b.rssi; });
    return out;
}

}  // namespace sb20proxy
