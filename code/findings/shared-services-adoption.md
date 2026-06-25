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
| **Secrets** (Infisical `:8222`) | Hold the **OTA push-password** + the future **ed25519 signed-pull key**; build/backend pull at build/sign time | ⏳ **needs a Machine Identity** (owner) |
| **Observability** (OTLP `:4318` → Loki/Grafana `:3000`) | Non-blocking, content-free **signals** from the desk tooling (capture / build / flash / OTA / ride) | 🔵 wiring |
| **Local models** (Ollama `:11434/v1`) | Token-heavy **routine** work (capture summarisation, status drafting, triage) + embeddings | 🔵 client helper |
| **Surface into POS** (overlay) | Register SB20 (repo map, goals, health) so the day-driver sees it; home for weekly status + backlog | 🕓 coming soon |

## 1. Secrets — Infisical (retires gitignored `ota_secret.h`)
Move `OTA_PASSWORD` (today a gitignored `firmware/ota_secret.h`) and, when built, the **signed-pull ed25519
signing key** into the vault.
- **⚠️ Firmware nuance:** the ESP32 is a microcontroller — it can't run the Infisical SDK / pull at runtime;
  it gets secrets **baked at build/sign time**. So Infisical serves the **desk / build / backend** side:
  - **Build** pulls `OTA_PASSWORD` from the vault (`infisical run -- pio run …`, or the raw API) instead of
    reading the committed header → the password is compiled in (as today) but never committed.
  - **Signed-pull backend** (P3/P4) holds the **signing key** in the vault; the device verifies with the
    embedded public key (vendored monocypher) — only the *private* key needs the vault.
- **Identity model — PER MACHINE, not per app (owner, 2026-06-25):** each of the owner's machines gets its
  own Infisical **Machine Identity** (this dev box is the **Lenovo P1 work laptop**); a dev env on that
  machine authenticates *as the machine* and pulls the secrets for the projects its identity is scoped to.
  Secrets are organised per project; **auth is per machine**.
- **Onboard (owner action):** (1) create this machine's Machine Identity — its `clientId`/`clientSecret` live
  at the **machine/user level** (env / a local credential store, e.g. the Infisical CLI login), **never** in
  any repo; (2) create/seed an SB20 project/environment with `OTA_PASSWORD` (+ later the signing key) and
  grant this machine's identity read access. Then the build pulls from the vault *as the machine*.
- **⚠️ Plane boundary:** this is a **work** laptop but the POS is **personal-plane** — the machine identity
  here is for **personal** dev work (SB20 is a hobby); keep work/client secrets + content OUT of the personal
  POS. (The platform's `SHARED-SERVICES.md` still describes a per-*app* identity — reconcile it to per-machine
  there too; owner's repo/call.)

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
