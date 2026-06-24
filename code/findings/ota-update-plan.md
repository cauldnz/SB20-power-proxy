# OTA / firmware-update security plan — push → signed HTTPS pull

**Status: P0–P2 + SoftAP PIN SHIPPED; P3/P4 gated on the back end + a signing key (2026-06-24).** Owner-driven
from the 2026-06-24 security review (`Vuln 1`): the device exposed **unauthenticated, unsigned** firmware
flashing on the LAN. The lockdown (P0), CSRF guard (Vuln 2), CI gate (P0.5), the host-tested signed-OTA core
+ tooling (P1), the fetch/verify/apply updater (P2), and the WPA2 setup-AP PIN have all landed. Remaining:
wire `POST /ota/check` (P3) + log upload (P4), both blocked on standing up the unRAID back end + generating
the offline signing key. Owner decisions folded in below.

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
                            verify  BLAKE2b(image)==manifest.blake2b
                                 && ed25519(sig, blake2b, EMBEDDED_PUBKEY)
                                              ▼  (fail → Update.abort, stay on current image)
                                        Update.end(true) → reboot
```
- **Manifest** (`manifest.json`): `{ "version": "...", "url": "https://.../firmware.bin", "size": …,
  "blake2b": "<128 hex>", "sig": "<128 hex ed25519 over the digest>" }`. Small, cacheable, the only thing
  polled. (Implemented — `sb20proxy.ota.sign.build_manifest`; parsed by `OtaManifest.h`.)
- **Source-configurable:** a base manifest URL in NVS (set via `/setup`), so the **same firmware** updates
  from **GitHub Releases** *or* the **unRAID** box — just a different URL. Default: GitHub Releases.
- **Signed images are the core security property.** Authenticity comes from the **signature**, not the
  transport — so even a public GitHub asset (or a plain-HTTP unRAID URL) can't be tampered into the device.
  TLS adds confidentiality + stops downgrade/eavesdrop and is still used (see TLS below).
- **Between rides only.** WiFi shares the C3's single radio with BLE (our coex sore spot), and ride-mode
  already powers WiFi off — so polling/downloading happens when *not* riding. Never fetch mid-ride.

### Decisions locked (owner, 2026-06-24)
- **TLS trust = BACKEND-ONLY.** The device pins **only our back end's root** (one cert). All firmware +
  log traffic goes through the back end; **no direct GitHub download** (a GitHub fetch would fail TLS
  validation against a back-end-only trust store — that's the tradeoff, accepted). The back end mirrors/
  serves the signed firmware (it can pull from GitHub server-side). Tightest device trust surface.
- **eFuse hardening (Flash + NVS Encryption, Secure Boot) = DEFERRED PAST BETA.** Honest constraint:
  **there is no eFuse-free version** of any of them — Flash Encryption, NVS Encryption, and Secure Boot
  all root their keys in one-time eFuse burns; that *is* the mechanism. They defend against a **physical**
  attacker (board in hand: dump flash for the PSK, or USB-flash unsigned code). The realistic **beta**
  threat is the **network**, already covered by the signed-pull (image authenticity, verified in software,
  **zero eFuses**) + the Vuln 1/2 lockdowns. So beta ships **without** eFuse work; the only residual is the
  WiFi PSK readable if someone *physically* dumps a board — accepted for beta. Revisit for production units.
- **SoftAP password = 8-digit numeric PIN, derived from the chip ID**, shown on the OLED during onboarding.
  8 digits is the **WPA2-PSK minimum** (can't go shorter on a WPA2 AP); digits-only by request. Derivation:
  `PIN = decimal(HMAC-SHA256(firmware-baked secret, chip MAC))[:8]` — deterministic, no storage, re-derivable
  from the chip ID, unique per device while the baked secret stays secret. Reusable across the owner's ESP
  projects. (A separate small PR; closes the cleartext-PSK window by WPA2-protecting the provisioning AP.)
- **Staged board reflash:** AFTER session 8 (not before the ride). It's on the owner's trusted home LAN
  meanwhile.

### Back end + hosting (unRAID, free certs) — to design alongside P2/P4
Because the device trusts only our back end, that endpoint must exist with a **publicly-trusted TLS cert**:
- **Free cert:** **Let's Encrypt** via a reverse proxy on unRAID — **Nginx Proxy Manager** or **Caddy**
  (auto-provisions + auto-renews; Caddy is the least config). Needs a **domain name** (a cheap/free DNS
  name pointed at the box; DNS-01 challenge avoids exposing port 80 if preferred).
- **Routing:** the reverse proxy terminates TLS and routes by hostname/path into specific **back-end
  containers** — e.g. `ota.<domain>` → the firmware/manifest container, `logs.<domain>` → the log-ingest
  container. The device only ever speaks to `<domain>` over 443.
- **Device trust:** pin **ISRG Root X1** (Let's Encrypt's root) in the firmware — one small root, not the
  full CA bundle. Rotate-aware: bundle ISRG Root X1 **and** X2 to survive Let's Encrypt's root transition.
- Firmware binaries can still be *built/released* on GitHub; the back end **mirrors** them so the device
  never has to trust GitHub directly.

### Signing (decided + SHIPPED: app-layer ed25519 over a BLAKE2b digest, via vendored monocypher)
- **App-layer verification**, not ESP-IDF Secure Boot: compute the **BLAKE2b-512** digest of the streamed
  image on-device and verify a **detached ed25519 signature** over that digest against a **public key
  compiled into the firmware**, *before* `Update.end(true)`. **(Implemented in P1 — `OtaVerify.h` +
  `OtaUpdater.h`; signing in `sb20proxy.ota.sign`.)**
- **Why ed25519 + monocypher, not mbedTLS/ECDSA** (the owner's call, 2026-06-24): mbedTLS's ed25519 isn't
  reliably enabled in the Arduino build, and — decisively — vendoring a small portable lib (**monocypher**,
  public-domain, `firmware/lib/monocypher`) means the *same* verify code runs in the host tests and on the
  device. That gives the security-critical check real **golden-vector coverage** (Python signs → C verifies,
  byte-for-byte), which an mbedTLS-only path can't have until the bench. ed25519 is also simpler/harder to
  misuse than ECDSA. (TLS for the *transport* still uses the bundled mbedTLS — only the image *signature*
  moved to monocypher.)
- **Why app-layer signing is enough for beta:** it gives *authenticity of the image* (the device only
  applies our-signed firmware) verified **in software, with zero eFuses** — so OTA can't be tampered into
  the device. It does **not** stop a physical attacker from USB-flashing unsigned code; that needs Secure
  Boot, which (with Flash + NVS Encryption) is **deferred past beta** per the decision above. Note for later:
  Flash Encryption alone does **not** encrypt NVS — the WiFi PSK at rest needs *NVS Encryption*, which builds
  on it; and once Secure Boot v2 (RSA-3072 on the C3) is enabled it can subsume this app-layer signature.
- **Private key lives offline** (the release script signs locally); the **public** key is the only key in
  the firmware. Later: move signing into a GitHub Actions secret for automated signed releases. **Losing or
  leaking the private key breaks the trust model** — it is the crown jewel; back it up offline, never commit it.

### TLS as a client (cost: affordable for our use)
- The C3 has HW SHA/AES + an RSA accelerator and 400 KB SRAM. One TLS connection at a time (manifest, then
  image, then log upload) costs ~**35–45 KB RAM** during the handshake (tunable via mbedTLS buffer sizes).
- **Do not ship the full Mozilla CA bundle (~150 KB+).** Per the backend-only decision, **pin only our
  back end's root** — ISRG Root X1 (+ X2) for the Let's-Encrypt cert on the unRAID reverse proxy. One small
  root, no GitHub root (the device never talks to GitHub directly). Smaller *and* safer (no rogue-CA risk).
- Client↔device (the on-device web UI) stays **plain HTTP** (owner-accepted): on-device TLS is heavy and the
  self-signed-cert UX is bad; the LAN control surface is instead protected by auth/CSRF fixes (see `Vuln 2`).

## Phases (each = a PR; pure cores host-tested in-commit)
- **P0 — lockdown + this doc — ✅ DONE.** Removed `POST /update` + fail-closed authenticated ArduinoOTA
  (PR #125); **Vuln 2** CSRF/same-origin guard + `/forget` POST-only (host-tested `HttpSecurity.h`, on main).
- **P0.5 — CI gate — ✅ DONE (PR #127).** Added a `USE_WIFI=1` (`esp32c3-wifi`) build to CI so the OTA/web
  code is compiled on every push (was `esp32c3-supermini`/`USE_WIFI=0` only).
- **P1 — pure OTA core + signing tooling — ✅ DONE (PR #128).** `OtaManifest.h` (parse + semver + decision),
  `OtaVerify.h` (ed25519 + streaming BLAKE2b via monocypher) with **golden vectors** (Python signs →
  C verifies, + tamper rejection); `sb20proxy.ota.sign` + `scripts/ota_sign.py` (keygen / pubkey / sign →
  manifest). Host-tested.
- **P2 — OTA updater + HTTPS fetch/apply seam — ✅ DONE (PR #129).** `OtaUpdater.h` (pure orchestration:
  fetch → verify → apply, every abort path host-tested with fakes) + `src/net/OtaPull.{h,cpp}`
  (`WiFiClientSecure` pinned to our root, fail-closed; Arduino `Update`). Inert until P3 + the back end + a key.
- **SoftAP PIN — ✅ DONE (PR #130).** WPA2-protect the setup AP — per-device 8-digit PIN on OLED, default
  passphrase on screenless boards.
- **P3 — local force route + config + surfacing (NEXT — needs the back end + key).** `POST /ota/check` (force
  a pull; **CSRF-guarded** like the other mutations), back-end base-URL + embedded pubkey + pinned CA,
  current-version + last-check on `/status` + OLED, slow between-rides auto-poll (never during ride mode).
- **P4 — log upload over HTTPS (needs the back end).** Testers' `/diag`/`/log` → our back end, same
  pinned-TLS client; opt-in, between rides.
- **(post-beta) production hardware hardening.** Secure Boot v2 + Flash + NVS Encryption on production
  boards; provision keys; prove on a spare board first. **Out of beta scope** per the decision above.

## Decisions — resolved (owner, 2026-06-24)
1. **Signing key location:** offline on the owner's machine now → GitHub Actions secret later. ✅
2. **Trust/host:** back-end-only (no direct GitHub); the back end mirrors GitHub releases. ✅
3. **CA:** pin our back end's Let's-Encrypt root (ISRG X1 + X2), not a broad bundle. ✅
4. **Cadence:** on-demand `/ota/check` first; daily between-rides check added later. ✅

## Related (tracked separately)
- **`Vuln 2` (web auth/CSRF):** ✅ Origin/Referer guard + POST-only mutations done (option B). A **setup PIN**
  before sale is still open (option C, post-beta). The new `/ota/check` (P3) inherits the same guard.
- **SoftAP per-device PIN:** decided — an **8-digit numeric PIN derived from the chip ID**, shown on the
  OLED, WPA2-protecting the provisioning AP (closes the cleartext-PSK window). Its own small PR.
- **Identifiers:** advise testers that `/diag` includes their meter hardware IDs (onboarding copy).
