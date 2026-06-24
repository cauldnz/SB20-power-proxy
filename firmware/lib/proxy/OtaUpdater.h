#pragma once
#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <string>
#include <vector>

#include "OtaManifest.h"
#include "OtaVerify.h"

namespace sb20proxy {

// The pure (no-Arduino) signed-pull OTA orchestrator. It fetches a manifest, decides whether to
// update, then streams the image through BLAKE2b + the flash writer, and applies it ONLY if the
// digest matches the manifest AND the ed25519 signature verifies against the embedded public key.
// Network + flash are behind two tiny interfaces so the whole decision flow — including every
// abort-on-failure path — is host-tested with fakes; the real WiFiClientSecure fetch + Arduino Update
// writer are thin seams in src/ (compile-gated, bench-validated). See code/findings/ota-update-plan.md.

// Fetch the manifest text and stream the image. Implemented for real by a pinned-TLS HTTPS client.
struct IFirmwareFetcher {
    virtual ~IFirmwareFetcher() = default;
    // GET a small text resource (the manifest). Returns false on any transport/TLS error.
    virtual bool fetchText(const std::string& url, std::string& out) = 0;
    // GET a large resource (the image), delivering it in chunks. `sink` returns false to abort the
    // stream (e.g. the writer failed). Returns false on transport error or if a sink call aborted.
    virtual bool fetchStream(const std::string& url,
                             const std::function<bool(const uint8_t*, size_t)>& sink) = 0;
};

// The flash target. Implemented for real by the Arduino Update API (writes the inactive OTA slot).
struct IFirmwareWriter {
    virtual ~IFirmwareWriter() = default;
    virtual bool begin(size_t totalSize) = 0;
    virtual bool write(const uint8_t* data, size_t len) = 0;
    virtual bool end() = 0;   // finalize + mark the new image to boot
    virtual void abort() = 0;  // discard a partial/failed write; keep the running image
};

enum class OtaResult {
    UpToDate,       // manifest valid but not newer than what we run
    NoManifest,     // couldn't fetch the manifest
    BadManifest,    // manifest missing/garbage/short digest or sig
    FetchFailed,    // image download failed mid-stream
    SizeMismatch,   // downloaded byte count != manifest.size
    HashMismatch,   // BLAKE2b of the image != manifest.blake2b (corrupt/tampered)
    BadSignature,   // ed25519 signature didn't verify (not our key / tampered digest)
    WriteFailed,    // the flash writer rejected begin/write/end
    Applied,        // verified + flashed; caller should reboot
};

inline const char* otaResultName(OtaResult r) {
    switch (r) {
        case OtaResult::UpToDate: return "up-to-date";
        case OtaResult::NoManifest: return "no-manifest";
        case OtaResult::BadManifest: return "bad-manifest";
        case OtaResult::FetchFailed: return "fetch-failed";
        case OtaResult::SizeMismatch: return "size-mismatch";
        case OtaResult::HashMismatch: return "hash-mismatch";
        case OtaResult::BadSignature: return "bad-signature";
        case OtaResult::WriteFailed: return "write-failed";
        case OtaResult::Applied: return "applied";
    }
    return "?";
}

class OtaUpdater {
public:
    // `pubKey` is the 32-byte ed25519 public key baked into the firmware (the verifier's trust anchor).
    OtaUpdater(const uint8_t pubKey[kEd25519PubKeySize], IFirmwareFetcher& fetcher,
               IFirmwareWriter& writer)
        : fetcher_(fetcher), writer_(writer) {
        for (size_t i = 0; i < kEd25519PubKeySize; ++i) pubKey_[i] = pubKey[i];
    }

    // Check `manifestUrl` and, if it advertises a strictly-newer version, download + verify + flash it.
    // Never flashes an image whose digest or signature doesn't check out. On success returns Applied
    // (the caller reboots). The optional `out` receives the parsed manifest (for surfacing the version).
    OtaResult checkAndApply(const std::string& manifestUrl, const std::string& currentVersion,
                            OtaManifest* out = nullptr) {
        std::string body;
        if (!fetcher_.fetchText(manifestUrl, body)) return OtaResult::NoManifest;
        const OtaManifest m = parseManifest(body);
        if (out) *out = m;
        if (!m.valid) return OtaResult::BadManifest;
        if (!isNewerVersion(currentVersion, m.version)) return OtaResult::UpToDate;

        std::vector<uint8_t> expectedDigest, sig;
        if (!hexDecode(m.blake2bHex, expectedDigest) || expectedDigest.size() != kImageDigestSize ||
            !hexDecode(m.sigHex, sig) || sig.size() != kEd25519SigSize) {
            return OtaResult::BadManifest;
        }

        if (!writer_.begin(static_cast<size_t>(m.size))) return OtaResult::WriteFailed;

        ImageHasher hasher;
        size_t received = 0;
        bool writeOk = true;
        const bool streamed = fetcher_.fetchStream(m.url, [&](const uint8_t* data, size_t len) {
            hasher.update(data, len);
            received += len;
            if (!writer_.write(data, len)) { writeOk = false; return false; }
            return true;
        });
        if (!writeOk) { writer_.abort(); return OtaResult::WriteFailed; }
        if (!streamed) { writer_.abort(); return OtaResult::FetchFailed; }
        if (received != static_cast<size_t>(m.size)) { writer_.abort(); return OtaResult::SizeMismatch; }

        uint8_t computed[kImageDigestSize];
        hasher.finish(computed);
        if (!digestsEqual(computed, expectedDigest.data())) {
            writer_.abort();
            return OtaResult::HashMismatch;
        }
        if (!verifyImageSignature(computed, sig.data(), pubKey_)) {
            writer_.abort();
            return OtaResult::BadSignature;
        }
        if (!writer_.end()) { writer_.abort(); return OtaResult::WriteFailed; }
        return OtaResult::Applied;
    }

private:
    IFirmwareFetcher& fetcher_;
    IFirmwareWriter& writer_;
    uint8_t pubKey_[kEd25519PubKeySize];
};

}  // namespace sb20proxy
