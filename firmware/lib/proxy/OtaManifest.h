#pragma once
#include <stddef.h>
#include <stdint.h>

#include <cstdlib>
#include <string>
#include <vector>

namespace sb20proxy {

// The pure (no-Arduino) OTA manifest model + parse + version logic. The device fetches a small JSON
// manifest from the back end, decides whether it's newer than the running firmware, and (after the
// download is signature-verified, OtaVerify.h) applies it. Host-tested. The HTTPS fetch + Update apply
// are the Arduino seam (P2). See code/findings/ota-update-plan.md.
//
// Manifest shape (compact JSON, emitted by sb20proxy.ota.sign.build_manifest):
//   {"version":"0.2.0","url":"https://ota.example/fw-0.2.0.bin","size":1094042,
//    "blake2b":"<128 hex>","sig":"<128 hex>"}

// Decode a hex string into bytes. Returns true on success; false (and clears `out`) on odd length or
// any non-hex char. Used for the manifest's blake2b digest + ed25519 signature fields.
inline bool hexDecode(const std::string& hex, std::vector<uint8_t>& out) {
    out.clear();
    if (hex.size() % 2 != 0) return false;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        const int hi = nibble(hex[i]);
        const int lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) { out.clear(); return false; }
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return true;
}

// True iff the whole string is hex of exactly `bytes`*2 chars (a digest/sig length check).
inline bool isHexOfLen(const std::string& s, size_t bytes) {
    if (s.size() != bytes * 2) return false;
    std::vector<uint8_t> tmp;
    return hexDecode(s, tmp);
}

// Extract a JSON string field: the value of "key":"value". Minimal — our manifest is flat, unescaped,
// single-line — but tolerant of spaces around the colon. Returns "" if the key/quoted value is absent.
inline std::string jsonString(const std::string& body, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t p = body.find(needle);
    if (p == std::string::npos) return std::string();
    p = body.find(':', p + needle.size());
    if (p == std::string::npos) return std::string();
    ++p;
    while (p < body.size() && (body[p] == ' ' || body[p] == '\t')) ++p;
    if (p >= body.size() || body[p] != '"') return std::string();
    ++p;
    const size_t end = body.find('"', p);
    if (end == std::string::npos) return std::string();
    return body.substr(p, end - p);
}

// Extract a JSON integer field (e.g. "size":123). Returns `dflt` if absent or not a non-negative int.
inline long jsonNumber(const std::string& body, const std::string& key, long dflt = -1) {
    const std::string needle = "\"" + key + "\"";
    size_t p = body.find(needle);
    if (p == std::string::npos) return dflt;
    p = body.find(':', p + needle.size());
    if (p == std::string::npos) return dflt;
    ++p;
    while (p < body.size() && (body[p] == ' ' || body[p] == '\t')) ++p;
    size_t start = p;
    while (p < body.size() && body[p] >= '0' && body[p] <= '9') ++p;
    if (p == start) return dflt;
    return std::strtol(body.substr(start, p - start).c_str(), nullptr, 10);
}

struct OtaManifest {
    std::string version;
    std::string url;
    long size = -1;
    std::string blake2bHex;  // 128 hex chars (BLAKE2b-512)
    std::string sigHex;      // 128 hex chars (ed25519)
    bool valid = false;      // all fields present + well-formed
};

// Parse + validate a manifest body. `valid` is set only when every field is present and the digest +
// signature are exactly the right hex length — so a truncated/garbage manifest can never trigger an update.
inline OtaManifest parseManifest(const std::string& body) {
    OtaManifest m;
    m.version = jsonString(body, "version");
    m.url = jsonString(body, "url");
    m.size = jsonNumber(body, "size");
    m.blake2bHex = jsonString(body, "blake2b");
    m.sigHex = jsonString(body, "sig");
    m.valid = !m.version.empty() && !m.url.empty() && m.size > 0 &&
              isHexOfLen(m.blake2bHex, 64) && isHexOfLen(m.sigHex, 64);
    return m;
}

// Parse up to three dotted integers ("1.2.3" -> {1,2,3}); missing parts are 0, extra parts ignored.
inline void parseVersion(const std::string& v, long out[3]) {
    out[0] = out[1] = out[2] = 0;
    size_t pos = 0;
    for (int i = 0; i < 3 && pos <= v.size(); ++i) {
        size_t dot = v.find('.', pos);
        const std::string part = v.substr(pos, dot == std::string::npos ? std::string::npos : dot - pos);
        if (!part.empty()) out[i] = std::strtol(part.c_str(), nullptr, 10);
        if (dot == std::string::npos) break;
        pos = dot + 1;
    }
}

// Is `candidate` strictly newer than `current`? (semver major.minor.patch, numeric compare).
inline bool isNewerVersion(const std::string& current, const std::string& candidate) {
    long c[3], n[3];
    parseVersion(current, c);
    parseVersion(candidate, n);
    for (int i = 0; i < 3; ++i) {
        if (n[i] != c[i]) return n[i] > c[i];
    }
    return false;  // equal -> not newer
}

// The update decision: a well-formed manifest advertising a strictly-newer version. (Signature
// verification of the downloaded image is separate — OtaVerify.h — and gates the actual apply.)
inline bool shouldUpdate(const std::string& currentVersion, const OtaManifest& m) {
    return m.valid && isNewerVersion(currentVersion, m.version);
}

}  // namespace sb20proxy
