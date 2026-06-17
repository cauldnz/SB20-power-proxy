#pragma once
#include <string>

// Which scanned BLE device should the proxy READ power FROM. Pure + host-tested so the selection
// precedence is regression-proof; the NimBLE scan callback just supplies the fields.
namespace sb20proxy {

// Precedence (fixes the bike-session-2 bug where the meter client bounced between the Assioma and
// the real Stages cranks — all of which advertise the CPS service 0x1818):
//   1. Never read a copy of the crank we impersonate (name == spoofName) — that would form a loop.
//   2. If a meter address is PINNED, match only that exact address (deterministic single source;
//      also how the single-right-crank use case targets a chosen crank).
//   3. A NAMELESS peripheral (the WinRT fake_meter rig advertises the CPS UUID, no name) is matched
//      by the CPS service flag.
//   4. A NAMED device must contain the meter-name filter — so a real "Stages NNNN" crank (named,
//      CPS-advertising) is NOT grabbed; only the configured meter (e.g. "ASSIOMA") is.
inline bool isTargetMeter(const std::string& name, bool advertisesCps, const std::string& addr,
                          const std::string& pinAddr, const std::string& spoofName,
                          const std::string& nameFilter) {
    if (!name.empty() && name == spoofName) return false;  // never read our own spoof (loop guard)
    if (!pinAddr.empty()) return addr == pinAddr;           // pinned: exactly one source
    if (name.empty()) return advertisesCps;                 // nameless WinRT rig: match by CPS UUID
    return name.find(nameFilter) != std::string::npos;      // named: must be the configured meter
}

}  // namespace sb20proxy
