#pragma once
#include <string>

namespace sb20proxy {

// Pure (no-Arduino) HTTP request-origin checks for CSRF defense on the on-device web server. The
// Arduino WebServer wiring (collectHeaders / hostHeader / the 403) lives in src/net/WifiLink.cpp; the
// decision logic is here so it is host-tested (2026-06-24 security review, Vuln 2).

// Extract the authority (host[:port]) from a URL or a Host header value:
//   "http://192.168.1.165/forget"      -> "192.168.1.165"
//   "https://sb20proxy.local:8080/x"   -> "sb20proxy.local:8080"
//   "192.168.4.1"  (bare Host header)  -> "192.168.4.1"
// Lenient/never-throws: returns "" for an empty input. A scheme is optional; the authority ends at the
// first '/', '?' or '#'.
inline std::string requestAuthority(const std::string& urlOrHost) {
    std::string s = urlOrHost;
    const size_t scheme = s.find("://");
    if (scheme != std::string::npos) s = s.substr(scheme + 3);
    const size_t end = s.find_first_of("/?#");
    if (end != std::string::npos) s = s.substr(0, end);
    return s;
}

// Same-origin (CSRF) check for a state-changing request. `host` is the request's Host header (the
// authority the client used to reach us); `origin` / `referer` are those headers (either may be empty).
//
// Returns true (ALLOW) when:
//   * there is no Origin AND no Referer  — a non-browser client (curl / our tools) or a same-origin GET;
//     browsers always attach an Origin to a cross-site POST, so absence means "not a forged browser POST"; or
//   * the present Origin (preferred) or Referer authority equals our own Host authority.
// Returns false (BLOCK) only when a browser sent an Origin/Referer whose authority differs from ours —
// i.e. a cross-site (CSRF) request, e.g. a malicious page POSTing to http://sb20proxy.local/forget.
//
// Deliberately lenient: we never block tools that send no Origin, only forged cross-site browser requests.
inline bool isSameOriginRequest(const std::string& host, const std::string& origin,
                                const std::string& referer) {
    const std::string hostAuth = requestAuthority(host);
    if (hostAuth.empty()) return false;  // no Host to validate against — refuse rather than guess
    if (!origin.empty()) return requestAuthority(origin) == hostAuth;
    if (!referer.empty()) return requestAuthority(referer) == hostAuth;
    return true;  // no Origin/Referer -> not a cross-site browser POST -> allow (curl / same-origin)
}

}  // namespace sb20proxy
