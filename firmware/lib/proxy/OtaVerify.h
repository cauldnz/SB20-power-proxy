#pragma once
#include <stddef.h>
#include <stdint.h>

// monocypher (vendored, firmware/lib/monocypher) — included at FILE scope so its C decls stay global,
// not pulled into namespace sb20proxy. crypto_ed25519_* is RFC 8032 (Curve25519 + SHA-512), so it
// verifies signatures made by the Python signer (sb20proxy.ota.sign / cryptography); crypto_blake2b is
// standard BLAKE2b, matching hashlib.blake2b. See code/findings/ota-update-plan.md.
#include "monocypher.h"
#include "monocypher-ed25519.h"

namespace sb20proxy {

// The signed-OTA image-authenticity primitives. The device streams the firmware download through
// ImageHasher (so it never holds the whole image in RAM), then checks the digest against the manifest
// and ed25519-verifies the manifest's signature over that digest. Any failure must abort the update and
// keep the running image. Pure/host-tested against golden vectors produced by the Python signer.

static constexpr size_t kImageDigestSize = 64;    // BLAKE2b-512
static constexpr size_t kEd25519SigSize = 64;
static constexpr size_t kEd25519PubKeySize = 32;

// Streaming BLAKE2b-512 over the firmware image, fed chunk-by-chunk as it downloads. Mirrors the
// signing side's hashlib.blake2b(data, digest_size=64).
class ImageHasher {
public:
    ImageHasher() { crypto_blake2b_init(&ctx_, kImageDigestSize); }
    void update(const uint8_t* data, size_t len) { crypto_blake2b_update(&ctx_, data, len); }
    void finish(uint8_t out[kImageDigestSize]) { crypto_blake2b_final(&ctx_, out); }

private:
    crypto_blake2b_ctx ctx_;
};

// One-shot digest of an in-memory buffer (tests / small payloads).
inline void imageDigest(const uint8_t* data, size_t len, uint8_t out[kImageDigestSize]) {
    crypto_blake2b(out, kImageDigestSize, data, len);
}

// Verify a detached ed25519 signature over a 64-byte image digest against a 32-byte public key.
// Returns true iff valid (monocypher's crypto_ed25519_check returns 0 on success).
inline bool verifyImageSignature(const uint8_t digest[kImageDigestSize],
                                 const uint8_t signature[kEd25519SigSize],
                                 const uint8_t publicKey[kEd25519PubKeySize]) {
    return crypto_ed25519_check(signature, publicKey, digest, kImageDigestSize) == 0;
}

// Constant-time compare of two digests (computed vs the manifest's) — no early-exit timing leak.
inline bool digestsEqual(const uint8_t a[kImageDigestSize], const uint8_t b[kImageDigestSize]) {
    uint8_t diff = 0;
    for (size_t i = 0; i < kImageDigestSize; ++i) diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0;
}

}  // namespace sb20proxy
