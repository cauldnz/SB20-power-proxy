# OTA / firmware-update security plan — push → signed HTTPS pull

**Status: design + phase 0 in progress (2026-06-24).** Owner-driven from the 2026-06-24 security review
(`Vuln 1`): the device exposed **unauthenticated, unsigned** firmware flashing on the LAN. This doc is the
plan to fix it properly and to support the **internet** update + log-upload paths the pre-beta needs.

> **One-line goal:** the device only ever runs firmware **we signed**, fetched by the **device itself**
> over a trusted channel — and there is **no inbound flash surface** for a LAN/CSRF attacker to abuse.

## Why pull, not push (decided)
- **NAT reality:** once a board is on a tester's home WiFi behind their router, our server *cannot* reach
  it to push. Push only works on the same LAN. Internet updates **must** be device-initiated.
- **Security:** a device that only makes *outbound* connections has **no listener to attack**. The entire
  "anyone on the LAN can flash it" surface (ArduinoOTA :3232, `POST /update`) disappears.
- **Keep one local lever:** a LAN route (`POST /ota/check`) to **force a pull on demand** during dev/support —
  it triggers the same signed-pull flow, it does *not* accept an uploaded image.

## Threat model
On a tester's home/club WiFi the device is reachable by any other host (a compromised IoT gadget, a guest
laptop, malware on a PC) **and** by any website the user browses on that network (CSRF). The asset worth
protecting is **code execution on the device** (→ it holds the WiFi PSK and is a network foothold). We are
*not* defending against a determined attacker with physical possession (that's the Secure-Boot tier, later).

## Architecture (target)
```
   release build ──sign(offline key)──▶ manifest.json + firmware.bin + .sig
                                              │  (GitHub Releases  OR  unRAID URL)
                                              ▼
   device (between rides) ──HTTPS GET manifest──▶ newer version?
        │                                              │ yes
        │ POST /ota/check (LAN, force)                 ▼
        └────────────────────────────▶ HTTPS GET firmware.bin (stream → Update.write,
                                          hashing as it goes)
                                              ▼
                            verify  SHA-256(image)==manifest.sha256
                                 && ECDSA-P256(sig, sha256, EMBEDDED_PUBKEY)
                                              ▼  (fail → Update.abort, stay on current image)
                                        Update.end(true) → reboot
```
- **Manifest** (`manifest.json`): `{ "version": "...", "url": "https://.../firmware.bin", "sha256": "...",
  "sig": "<base64 ECDSA-P256 over the sha256>" }`. Small, cacheable, the only thing polled.
- **Source-configurable:** a base manifest URL in NVS (set via `/setup`), so the **same firmware** updates
  from **GitHub Releases** *or* the **unRAID** box — just a different URL. Default: GitHub Releases.
- **Signed images are the core security property.** Authenticity comes from the **signature**, not the
  transport — so even a public GitHub asset (or a plain-HTTP unRAID URL) can't be tampered into the device.
  TLS adds confidentiality + stops downgrade/eavesdrop and is still used (see TLS below).
- **Between rides only.** WiFi shares the C3's single radio with BLE (our coex sore spot), and ride-mode
  already powers WiFi off — so polling/downloading happens when *not* riding. Never fetch mid-ride.

### Signing (decided: app-layer signature, ECDSA-P256)
- **App-layer verification**, not ESP-IDF Secure Boot: compute SHA-256 of the streamed image on-device and
  verify a **detached ECDSA-P256 signature** over that hash against a **public key compiled into the
  firmware**, *before* `Update.end(true)`. ECDSA-P256 (or RSA-3072) is in the bundled **mbedTLS** (no new
  dependency; SHA/RSA are HW-accelerated on the C3). Chosen over Ed25519 only because mbedTLS ships P256/RSA
  enabled under Arduino by default; revisit if we add libsodium.
- **Why not Secure Boot v2 now:** it burns **one-time eFuses** (irreversible; a mis-provisioned board can
  brick) and changes the flashing workflow permanently. App-layer signing gives us *authenticity of the
  image* without that risk. Secure Boot v2 + Flash Encryption + **NVS Encryption** (note: Flash Encryption
  alone does **not** encrypt NVS — the WiFi PSK at rest needs *NVS Encryption*, which builds on it) are a
  **separate, deliberate production-hardware hardening** (Phase 5), done on a spare board first.
- **Private key lives offline** (the release script signs locally); the **public** key is the only key in
  the firmware. Later: move signing into a GitHub Actions secret for automated signed releases. **Losing or
  leaking the private key breaks the trust model** — it is the crown jewel; back it up offline, never commit it.

### TLS as a client (cost: affordable for our use)
- The C3 has HW SHA/AES + an RSA accelerator and 400 KB SRAM. One TLS connection at a time (manifest, then
  image, then log upload) costs ~**35–45 KB RAM** during the handshake (tunable via mbedTLS buffer sizes).
- **Do not ship the full Mozilla CA bundle (~150 KB+).** Since we control both endpoints, **pin our own
  roots**: ISRG Root X1 (Let's Encrypt → unRAID behind its reverse proxy) + GitHub's root. Smaller *and*
  safer (no rogue-CA risk).
- Client↔device (the on-device web UI) stays **plain HTTP** (owner-accepted): on-device TLS is heavy and the
  self-signed-cert UX is bad; the LAN control surface is instead protected by auth/CSRF fixes (see `Vuln 2`).

## Phases (each = a PR; pure cores host-tested in-commit)
- **P0 — lockdown + this doc (in progress).** Remove `POST /update` (the browser-reachable RCE/CSRF
  vector); **password-gate ArduinoOTA, fail-closed** (no `ota_secret.h` ⇒ push OTA off, USB-only) as a dev
  bridge until pull lands. **First task of P1: add a `USE_WIFI=1` build to CI** — today CI compiles only
  `esp32c3-supermini` (`USE_WIFI=0`), so the OTA/web code isn't built by CI; OTA work must be CI-gated.
- **P1 — pure OTA core (host-tested).** Manifest parse + semver compare + the update decision; **signature
  verification** with golden vectors (sign a known blob offline → assert verify accepts it and rejects a
  flipped byte / wrong key). Release tooling: keygen + a `build_signed_release.py` (build → sha256 → sign →
  emit manifest + bin). No hardware.
- **P2 — firmware HTTPS fetch + apply seam.** `WiFiClientSecure` (pinned CA) → stream manifest + image into
  `Update`, verify, apply; abort + stay on current image on any mismatch. Bench-gated on the board.
- **P3 — local force route + config + surfacing.** `POST /ota/check` (force a pull), manifest base-URL in
  NVS via `/setup`, current-version + last-check on `/status` + OLED, slow between-rides auto-poll.
- **P4 — log upload over HTTPS.** The other internet need (testers' `/diag`/`/log` → our server), same
  pinned-TLS client; opt-in, between rides.
- **P5 — production hardware hardening (later, separate, irreversible).** Secure Boot v2 + Flash Encryption
  + NVS Encryption on production boards; provision keys; test on a spare board before any tester unit.

## Decisions needed (to start P1)
1. **Signing key location:** offline on the owner's machine now (recommended) → GitHub Actions secret later.
2. **First host target:** GitHub Releases (recommended — public, HTTPS, trivial to stand up) vs unRAID first.
   Firmware is URL-agnostic either way; this only picks what we test against first.
3. **CA strategy:** pin ISRG Root X1 + GitHub root (recommended, small/secure) vs `esp_crt_bundle` (bigger,
   any host).
4. **Auto-poll cadence:** on-demand (`/ota/check`) only to start, or add a daily between-rides check?

## Related (tracked separately)
- **`Vuln 2` (web auth/CSRF):** the on-device control surface (incl. the new `/ota/check`) needs at least an
  Origin/Referer check + POST-only mutations, and a setup PIN before sale. Decision pending with the owner.
- **SoftAP:** option to WPA2-protect the provisioning AP (closes the cleartext-PSK window) — owner deciding.
- **Identifiers:** advise testers that `/diag` includes their meter hardware IDs (onboarding copy).
