# Contributing the OBC listener upstream to qdomyos-zwift

**Status: branch pushed + GREEN on our fork's Linux-desktop CI (2026-07-25); the upstream PR is NOT
open and needs the owner's explicit go** (it is a public action on a third party's repo, under the
owner's name).

This doc is the whole state of that contribution, so it does not have to live in a session's context.
It is a **process/state** doc, not a protocol doc — the OBC wire format is
[`obc-protocol.md`](obc-protocol.md), which stays canonical.

---

## 1. What the contribution is

The **consumer half** of OpenBikeControl. Our firmware is the *producer* (a button box that advertises
the OBC service and notifies Button-State); qz is a natural *consumer* — it already has a
button→action dispatch (gears, ERG target, peloton offset, resistance, zone, lap, start/stop) driven by
Zwift Click / Thinkrider / CYCPLUS remotes. `obclistener` is qz as a BLE **central** onto an OBC box.

The producer side of the OBC work **already landed upstream in #4504**. Our issue asking for the
consumer side is **[#4791](https://github.com/cagnulein/qdomyos-zwift/issues/4791)**.

**Licence boundary (load-bearing):** qz is **GPL-3.0**; this repo is **MIT clean-room**. Code we write
*into* qz is a contribution under qz's licence. **Nothing flows back** — do not copy qz source into
`firmware/`, `firmware-nrf/`, or `code/`. The shared artefact is the *protocol* (UUIDs + the
`[0x01, id, state, …]` wire format), which is ours and documented in `obc-protocol.md`.

## 2. Where the code is

Fork: `cauldnz/qdomyos-zwift` (local clone `C:\repos\qdomyos-zwift`, **shallow** — do not try to rebase
long histories; branch fresh off `upstream/master` instead).

| Branch | Purpose |
|---|---|
| `feat/obc-listener-upstream` | ⭐ **the upstream-ready change.** Cut fresh from `upstream/master` `e695994`. Purely additive: **264 insertions, 0 deletions**, 8 files. No fork-only files. |
| `ci/verify-obc-listener` | `feat/…` + our fork-only `.github/workflows/ci-linux-desktop.yml`, purely so the fork's Actions build-verify it. **Never propose this branch upstream.** |

Upstream's own `main.yml` triggers only on `push` to `master` and on `pull_request`, so pushing a
feature branch to the fork does **not** build it — hence the separate verification branch.

**Build-verified 2026-07-25:** `ci/verify-obc-listener` is **green** on the fork's Linux-desktop CI
(run 30121934489, 12m28s). One fix was needed beyond the raw port: qz keeps a **fixed-size**
`QVariant allSettings[allSettingsCount][2]` defaults table with `allSettingsCount = 1000`, and that
array was already exactly full — adding `obc_listener_enabled` overflowed it
(`error: too many initializers for 'QVariant [1000][2]'`). Bumped the count to **1001**. This is the
kind of thing the compile-only reasoning misses and a real build catches.

Files touched (all additive):

- `src/devices/obclistener/obclistener.{h,cpp}` — new class (199 lines).
- `src/devices/bluetooth.cpp` — 18-line discovery block in `connectedAndDiscovered()`.
- `src/devices/bluetooth.h` — the include + `obclistener* obcListener = nullptr;`.
- `src/qdomyos-zwift.pri` — the `.cpp` + `.h` entries.
- `src/qzsettings.{h,cpp}` — `obc_listener_enabled`, default **false**, + the defaults-map row.
- `src/settings.qml` — the property + an "OpenBikeControl Options" accordion toggle.

## 3. Review fixes applied vs our original fork commit (`1249c9b`)

Three deliberate changes were made while re-cutting onto current master. **Do not silently revert
them** — each has a reason:

1. **Default flipped `true` → `false`.** Every comparable upstream toggle
   (`default_zwift_click`, `default_thinkrider_controller`, `default_cycplus_bc2_controller`) is
   `false`. A new BLE central that connects on its own by default is exactly the kind of change a
   maintainer bounces.
2. **Match on the service UUID only.** The original also matched `b.name().toUpper().startsWith("OBC")`.
   qz's own `AGENTS.md` has a **CRITICAL** section demanding a conflict check before adding any name
   pattern to `bluetooth.cpp` — a bare 3-letter prefix is precisely the hazard it warns about. The
   service UUID is unambiguous and every OBC controller advertises it.
3. **Raw `QStringLiteral("obc_listener_enabled")` → the `QZSettings` constant + QML toggle.** Upstream
   routes every device toggle through `QZSettings::x` / `QZSettings::default_x` and surfaces it in
   `settings.qml`. Without the QML toggle a default-off setting is unreachable for a normal user.

Also corrected: the header comment claimed the class was "compiled non-iOS for now". It is **not**
gated — the `.pri` adds it unconditionally. The comment now says what is true: it compiles everywhere,
but iOS drives BLE through a native Swift wrapper rather than QtBluetooth, so iOS needs a native
central bridge (like `iOS_zwiftClickRemote`) as a follow-up.

## 4. ⚠️ Deadline — the stale bot closes #4791 around **2026-07-31**

`.github/stale.yml` upstream: `daysUntilStale: 15`, `daysUntilClose: 7`, `staleLabel: wontfix`.
The bot commented and applied `wontfix` on **2026-07-24 01:44 UTC**. Seven days later it closes.
Any activity on the issue resets it; opening the PR is the natural reset.

## 5. The PR body — honest framing (use verbatim; the claim boundaries matter)

> ### OpenBikeControl listener — qz as a BLE central to an OBC button box
>
> This is the **consumer half** of the OpenBikeControl work whose producer landed in #4504. qz connects
> to an OBC controller as a BLE central, subscribes to its Button-State characteristic, and dispatches
> each press through the existing `homeform` keyboard-action path — so an OBC box drives gears, ERG
> target, peloton offset, resistance, zone, lap and start/stop exactly like the other remotes.
>
> - New setting `obc_listener_enabled`, **default off**, with a Settings toggle under "OpenBikeControl
>   Options" — same shape as the Thinkrider / CYCPLUS controller options.
> - Discovery matches on the **advertised OBC service UUID only** (no name prefix), bike-only, one
>   controller. Modelled on the `zwiftclickremote` BLE-central pattern.
> - OBC's standard button ids are semantic, so it works with no configuration
>   (ShiftUp→gear_up, ERG Up/Down→power_up/down, Lap→lap); each id is overridable per user via the
>   QSettings key `obc_button_<id>`.
> - Purely additive: no existing behaviour changes when the setting is off.
>
> **Testing status, stated plainly:** it compiles green on a Linux-desktop build. The **controller**
> side has been verified on air against a reference OBC consumer — a real OBC box, discovered and
> decoded, presses arriving in order. The **listener in this PR has not yet been tested on air with
> qz**; I have a firmware-side test source ready and will report results here.
>
> iOS drives BLE through a native Swift wrapper rather than QtBluetooth, so iOS support needs a native
> central bridge (like `iOS_zwiftClickRemote`) — happy to follow up separately.
>
> Closes #4791.

### Claim boundaries — do NOT say any of these

- ❌ Do **not** claim the listener has been tested end-to-end with qz. It has not.
- ❌ Do **not** claim iOS support.
- ❌ Do **not** claim maintainer interest or that anything was pre-agreed.
- ❌ Do **not** cite **#1649** or mention the SB20's resistance control. That diagnosis was **retracted**
  (`decisions.md` 2026-07-06 correction — the owner controls his SB20 in qz fine). Raising it would be
  reasserting something we withdrew. See the `contribute-sb20-fix-to-qdomyos` memory.

## 6. What would upgrade the PR from "compiles" to "verified on air"

Run a real qz against a real OBC box:

1. A **Linux-desktop qz build** from `feat/obc-listener-upstream` at the bike.
2. Enable the toggle (Settings → OpenBikeControl Options → OBC Controller), restart qz.
3. A board advertising OBC in reach — our ESP32 in Devmode is the turnkey source
   (`POST /obc/devmode/on`, then `GET /obc/press?id=…` for virtual presses; **`OBC-SB20` advert**).
4. Watch qz's debug log for `obclistener: connecting…` → `subscribed to OBC Button-State` →
   `button <id> -> <action>`, and the action landing in the UI.

This is session 12 **stretch S3** — genuinely optional, and only worth attempting if a runnable qz build
is already staged at the bike. Do not build qz on the rider's clock (PLAYBOOK: never debug tooling
during a ride).

## 7. Open the PR (owner's call — needs an explicit go)

```bash
gh pr create --repo cagnulein/qdomyos-zwift --base master --head cauldnz:feat/obc-listener-upstream --title "OpenBikeControl listener — qz as a BLE central to an OBC button box" --body-file <the §5 body>
```
