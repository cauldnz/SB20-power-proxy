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
  - **Build** pulls `OTA_PASSWORD` from the vault (`infisical run -- pio run …`, or the raw API) as the
    **read-only** SB20 identity, instead of reading the committed header.
  - **Signed-pull backend** (P3/P4) holds the **ed25519 signing key**; the device verifies with the embedded
    public key (vendored monocypher) — only the *private* key needs the vault.
- **SB20 remaining steps:**
  1. **Retrieve the read-only creds** into Windows Credential Manager (owner, on this machine) —
     turnkey via [`tools/secrets-pull.ps1`](../../tools/secrets-pull.ps1): SSH-fetches
     `…/sb20-power-proxy.creds` from the NAS and stores it under Cred Manager target
     `SB20/infisical/sb20-power-proxy` (**never** Git). Read back with `tools/secrets-get.ps1`.
  2. **Seed the build's secrets** into the `sb20-power-proxy` project — `OTA_PASSWORD` (from the current
     `ota_secret.h`) + later the signing key — via the **Infisical UI** (owner is admin) **or** a **`--write`
     identity** (the read-only SB20 identity can't write).
  3. **Agent wires the build** to pull `OTA_PASSWORD` from the vault at build time, retiring the committed
     `ota_secret.h`: `secrets-get.ps1` → `infisical login` (UA) → `infisical secrets get OTA_PASSWORD`.
     Real-data-first: verified against the live vault.
- **P1 dev-box identity (read+write).** The per-machine pattern. Provision on the NAS (needs the admin
  bootstrap token): `scp …/new-machine-identity.sh unraid:/tmp/`, then one run **per project** — the
  script grants one project per run and isn't idempotent on a name, so today the P1 covers *both*
  projects as two scoped identities: `… chris-p1-pos --project pos --write` and
  `… chris-p1-sb20 --project sb20-power-proxy --write` (a single identity spanning both needs
  multi-`--project` support — to propose to cauldnz-pos). Store each with
  `secrets-pull.ps1 -Identity <name> -FromStdin` (pipe the one-time stdout). The `--write` identity on
  `sb20-power-proxy` is what **seeds** `OTA_PASSWORD` for step 2.
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
