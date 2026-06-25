# Shared-services adoption (SB20 proxy → cauldnz-pos home infra)

How this project uses the **POS shared services** instead of standing up its own. Platform spec (source of
truth): [`cauldnz/cauldnz-pos` → `SHARED-SERVICES.md`](https://github.com/cauldnz/cauldnz-pos/blob/main/SHARED-SERVICES.md).
Services run on the home NAS **`wtrmax.local`** (`192.168.1.146`, docker net `pos-net`); reachable on the
home LAN (the dev/bike machine is on `192.168.1.x`). **Personal-plane only** — SB20 is a hobby project, so it
qualifies; keep it content-free in anything shared (signals + pointers, never ride content / secrets / PII).

Verified reachable 2026-06-25: Ollama `:11434` (models `llama3.2:3b`, `nomic-embed-text`), Infisical `:8222`
(200), Grafana `:3000` (200), OTLP `:4318` (up).

| Service | SB20 use | Status |
|---|---|---|
| **Secrets** (Infisical `:8222`) | SB20 build pulls `OTA_PASSWORD` + the ed25519 signing key via a **read-only app identity** at build/sign time | 🔵 read-only identity **created + verified** (cauldnz-pos#1); next: retrieve creds, seed `OTA_PASSWORD`, wire build |
| **Observability** (OTLP `:4318` → Loki/Grafana `:3000`) | Non-blocking, content-free **signals** from the desk tooling (capture / build / flash / OTA / ride) | 🔵 wiring |
| **Local models** (Ollama `:11434/v1`) | Token-heavy **routine** work (capture summarisation, status drafting, triage) + embeddings | 🔵 client helper |
| **Surface into POS** (overlay) | Register SB20 (repo map, goals, health) so the day-driver sees it; home for weekly status + backlog | 🕓 coming soon |

## 1. Secrets — Infisical (retires gitignored `ota_secret.h`)

> **Platform model + provisioning landed in [cauldnz-pos#1](https://github.com/cauldnz/cauldnz-pos/issues/1)**
> (commit `c363a9d`): `infra/identities/new-machine-identity.sh` + `infra/identities/README.md` + the
> `SHARED-SERVICES.md` two-pattern model. **SB20 is already onboarded** (project + read-only identity created
> + verified). This section is the SB20-side view of consuming it.

**Two identity patterns (cauldnz-pos, least-privilege):**
- **Per-app / service → READ-ONLY**, project-scoped (deployed runtime). **This is what the SB20 build uses**
  to pull `OTA_PASSWORD` + the signing key at build time — the script's default.
- **Per-machine → read+write** (dev boxes, e.g. the Lenovo P1) — general dev / seeding secrets; a *separate*
  `--write` identity, **NOT** what the build runs as (a deployed build shouldn't carry write access).

**Already provisioned + verified (cauldnz-pos):** project **`sb20-power-proxy`** (envs `dev`/`staging`/`prod`)
+ a **read-only** machine identity (logged in + test-read confirmed); its `clientId`/`clientSecret` are
NAS-side at `/mnt/user/appdata/pos-infisical/identities/sb20-power-proxy.creds` (chmod 600).

- **⚠️ Firmware nuance:** the ESP32 can't run the Infisical SDK / pull at runtime — secrets are baked at
  **build/sign** time, so Infisical serves the **desk / build / backend** side:
  - **Build** regenerates the gitignored `firmware/ota_secret.h` from the vault via
    [`tools/secrets-sync-ota.ps1`](../../tools/secrets-sync-ota.ps1) (raw API, as the **read-only** SB20
    identity) before a compile/flash — the vault is the source of truth; the header is a build artifact.
  - **Signed-pull backend** (P3/P4) holds the **ed25519 signing key**; the device verifies with the embedded
    public key (vendored monocypher) — only the *private* key needs the vault.
- **SB20 secrets onboarding — ✅ DONE (live, 2026-06-25):**
  1. **Read-only build creds in Windows Credential Manager** — `tools/secrets-pull.ps1` SSH-fetched
     `sb20-power-proxy.creds` and stored it at Cred Manager target `SB20/infisical/sb20-power-proxy`.
  2. **`OTA_PASSWORD` seeded** into `sb20-power-proxy`/`dev` (raw API, admin token — the org admin reaches
     the project; HTTP 200). **Verified readable by the read-only build identity** (len 32, matches the
     local `ota_secret.h`).
  3. **Build wiring:** [`tools/secrets-sync-ota.ps1`](../../tools/secrets-sync-ota.ps1) regenerates the
     gitignored `firmware/ota_secret.h` from the vault (`secrets-get.ps1` → UA login → raw GET); `-Check`
     reports drift. Run it before a build/flash; **rotate** = change in Infisical → re-run → reflash.
  - Still future: seed `staging`/`prod` when those builds exist; the **ed25519 signing key** (P3/P4).
- **P1 dev-box identity (read+write) — ✅ DONE (2026-06-25).** The `chris-p1` identity (which the provisioner
  had scoped to a standalone vanity `chris-p1` project) was **repurposed**: granted **read+write** on
  `sb20-power-proxy` **and** `pos-test` (verified — read + a throwaway write/delete both succeeded), its
  `.creds` repointed to `sb20-power-proxy`, and stored in Windows Credential Manager (target
  `SB20/infisical/chris-p1`). The P1 can now self-serve seed/rotate `sb20-power-proxy` secrets without the
  admin token. **Residuals (non-blocking):** the empty vanity `chris-p1` project still exists — deleting it
  was blocked by the local safety classifier; drop via the Infisical UI or admin
  `DELETE /api/v1/workspace/93fe5483-…`. A stray `ONBOARD_TEST` key sits in `sb20-power-proxy/dev` (an
  onboarding artifact; harmless — the build pulls only `OTA_PASSWORD`). Multi-`--project` support in the
  provisioner would avoid the vanity project — to propose to cauldnz-pos.
- Backlog (cauldnz-pos): per-env scoping (custom roles), OIDC/SPIFFE, self-service rotation via the control plane.
- **⚠️ Plane boundary:** this is a **work** laptop but the POS is **personal-plane** — the machine identity
  here is for **personal** dev work (SB20 is a hobby); keep work/client secrets + content OUT of the personal
  POS.

## 2. Observability — OTLP → Loki/Grafana
A small **non-blocking** emitter (`sb20proxy.obs`) pushes structured **signals** to `wtrmax.local:4318/v1/logs`
at key tooling moments. **Signals about content, never content** (the platform rule): emit `event_type` +
counts + pointers (capture filename, PR #, board id) — never raw frames, secrets, or PII.
- **Conventions:** `service.name = sb20proxy`; attributes `plane=personal`, `event_type`, `source`; plus ours.
- **Emit points (desk tooling):** capture done (`event_type=capture`, file, record-count), build, flash/OTA
  (`event_type=ota`, result), ride summary (`event_type=ride`, gates passed). View in Grafana
  (`{service_name="sb20proxy"}`).
- **Non-blocking:** wrap so a failed/absent emit never crashes the tool (the NAS may be off / off-LAN).

## 3. Local models — Ollama (OpenAI-compatible)
Point routine LLM work at `http://wtrmax.local:11434/v1` (dummy key) via a thin `sb20proxy.llm` helper.
- **Routine / token-heavy** work runs local: capture summarisation, draft status updates, triage of `/log`
  dumps — keeping the cloud agent (and its tokens) for the hard reasoning. `llama3.2:3b` for light text.
- **Embeddings:** `nomic-embed-text` for a cheap recall/search layer over `decisions.md` + the committed
  captures (find "where did we see X" without a full re-read).
- Endpoints are **LAN hostnames, not secrets** → live in config/constants, not the vault.

## 4. Surface into POS (coming soon)
When the overlay ships, register SB20 — repo map, goals, health — per `cauldnz-pos`'s
`overlay/observation-schema.md`, so the **day-driver** is aware of it. This is the intended home for the
agent to **file weekly project status** (generated from `decisions.md` + the session ledger + PR activity)
and **push backlog items**. Until then: tracked here; revisit when the service lands.

## Standing conventions
- **Personal-plane only**; **signals not content**; **endpoints in config, secrets in Infisical (never Git)**;
  every emit/integration **optional + non-blocking** so the project still runs with the NAS off / off-LAN.
