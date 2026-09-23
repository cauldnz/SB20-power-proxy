# Branch triage — appendix to the 2026-09-23 state-of-the-repo review

**Baseline:** `origin/main` = `be32624` at the time of deletion (2026-09-23). Method: for every
branch, `git cherry origin/main <branch>` (a `-` line means an equivalent patch is already on main)
cross-checked with the commit list of the PR that merged the branch (`gh pr view <n> --json commits`).

## Kept

| Branch | Why |
|---|---|
| `cauldnz-review-synthesis` | Finished, unmerged Copilot session (1 commit, 3 files): opened as PR #327 and merged; deleted afterwards |
| `feat/cyd-wifi-status` | PR #255, closed 2026-07-11 unmerged, +27/−1 (`LcdUi.h`, `main.cpp`, one test): the only unmerged code in the repository. Re-filed as an issue; branch deleted after that |

## Deleted remote branches (28) — all content on `origin/main`

| Branch | Tip | Evidence |
|---|---|---|
| away-docs-sweep | `7e1329d` | patch-equivalent (PR #217) |
| away-night-findings | `02dcbe5` | patch-equivalent (PR #226) |
| ble-scan-selfheal | `adf423e` | patch-equivalent (PR #220) |
| cyd-boot-button-cal | `42fe220` | patch-equivalent (PR #208) |
| cyd-panel-orientation | `f25c249` | patch-equivalent (PR #206) |
| cyd-ui-fixes | `871d886` | patch-equivalent (PR #209) |
| docs/ui-architecture-review | `ad6e46f` | 4 commits touching only `design/ui-architecture-review.md` and `design/ui-schema-design.md`; main carries both in a newer form (PR #280, 2026-07-24) |
| feat/annotation-layer | `(merged)` | ancestor of main (PR #60) |
| feat/obc-transmitter | `(merged)` | ancestor of main (PR #248 via #251) |
| feat/obc-web-config | `(merged)` | ancestor of main (PR #250 via #251) |
| fix-erg-bare-build | `d5d8584` | patch-equivalent (PR #213) |
| fix-import-workout-ct | `8e5011d` | patch-equivalent (PR #239) |
| fix/c3-portal-ap-visibility | `(merged)` | ancestor of main (PR #254/#267) |
| ftms-erg-phase4 | `f6b31b0` | patch-equivalent (PR #212) |
| ftms-gatt-analysis | `4696473` | patch-equivalent (PR #243) |
| lcd-picker-filter | `efe61d8` | patch-equivalent (PR #214) |
| nrf-ciq-compile | `031369e` | patch-equivalent (PR #216) |
| nrf-imu-selftest | `341ad27` | patch-equivalent (PR #218) |
| nrf-ota-picker | `3757892` | patch-equivalent (PR #223) |
| nrf-quickwins | `e6f0ad5` | both commits listed in merged PRs #221 and #222 |
| portal-subnet | `3dc9557` | patch-equivalent (PR #228) |
| recovery-capture-plan | `4b6f0c5` | patch-equivalent (PR #245) |
| ride-pcap-analysis | `524bc5c` | patch-equivalent (PR #244) |
| s3-lvgl-unstall | `03d3c6b` | patch-equivalent (PR #210) |
| spa-xss-fix | `f677d07` | patch-equivalent (PR #238) |
| three-board-web-ui | `cdf702a` | patch-equivalent (PR #211) |
| web-trainer-picker | `89e5f24` | patch-equivalent (PR #219) |
| worktree-nrf52-sense | `8601782` | all 5 commits listed in merged PR #215 |

`fix/guition-validation-nvs-safe-flash` (PR #322) was deleted by the hardware session the same day.

## Deleted local branches (43)

38 were patch-equivalent to `origin/main` (including the eight `cauldnz-*` Copilot session branches,
the two `claude/*` worktree branches, `main-sync` and `verify`). Five carried a sync-merge commit
plus a commit that a merged PR landed: `docs/front-door` (#309), `feat/version-the-nvs-config-line`
(#301), `fix/flash-ride-safety-guards` (#302), `flash-guard-banner` (#313, both commits),
`docs/ui-architecture-review` (#280).

## Removed worktrees (3)

`.claude/worktrees/ecstatic-benz-9dfa6d` (`9172af0`, detached, clean, last touched 2026-07-20),
`.claude/worktrees/inspiring-villani-f5af8c` (`df0d015`, detached, clean, 2026-07-20),
`C:\repos\chrisauld_microsoft\ai-strategy\copilot-worktrees\SB20-power-proxy\cauldnz-fantastic-system`
(`8383ad3`, detached, clean, 2026-07-26).

## Rollback record — every ref before deletion

`git for-each-ref` on the desk checkout immediately before any deletion. Restore a branch with
`git push origin <sha>:refs/heads/<name>` (objects persist through the merged PRs), or with the
"Restore branch" button on the PR that merged it.

```
capture-truncation-marker 55819bf 2026-07-27 [gone]
cauldnz-document-bike-session-workflow 6d278c6 2026-07-23 
cauldnz-dream-device-features 7eda65f 2026-07-20 [gone]
cauldnz-effective-enigma 18c3667 2026-07-27 
cauldnz-fantastic-system 8383ad3 2026-07-26 
cauldnz-legendary-invention 8383ad3 2026-07-26 
cauldnz-musical-invention 8383ad3 2026-07-26 
cauldnz-plan-trainer-dynamics-program 4fe871c 2026-07-20 [gone]
cauldnz-review-glm-5-2 8383ad3 2026-07-26 
cauldnz-review-synthesis 6310b3f 2026-09-23 
cauldnz-solid-tribble 8383ad3 2026-07-26 
cauldnz-studious-journey 8383ad3 2026-07-26 
ci/pin-toolchain-and-cover-shipping-builds 6a8f2a8 2026-07-26 [gone]
claude/ecstatic-benz-9dfa6d 9172af0 2026-07-20 
claude/inspiring-villani-f5af8c c79b8bf 2026-07-20 [gone]
docs/dreaming-20-ideas 0a31b22 2026-07-15 [gone]
docs/front-door 6c7e618 2026-07-27 [gone]
docs/nrf-full-ant-loop-bench b331d19 2026-07-15 [gone]
docs/ui-architecture-review ad6e46f 2026-07-11 
experiment/try-new-tool 866de76 2026-06-22 
feat/compare-and-shifter-cores c79b8bf 2026-07-20 [gone]
feat/cyd-wattybird-easter-egg 191e9b1 2026-07-20 [gone]
feat/cyd-wifi-status bdf3771 2026-07-11 
feat/doctor-capture-rig-gate 64f9dfa 2026-06-25 
feat/obc-web-config d9be04e 2026-07-08 
feat/version-the-nvs-config-line e4a5f4a 2026-07-26 [gone]
fix/c3-portal-ap-visibility 35c05d6 2026-07-13 
fix/flash-ride-safety-guards f7c7417 2026-07-26 [gone]
fix/hermetic-suite-optional-deps a678fc7 2026-07-26 [gone]
fix/monitor-ride-sigterm-cleanup 64b1829 2026-06-22 [gone]
fix/nrf-reference-latch-guards 07ccb15 2026-07-26 [gone]
fix/pcap-16bit-char-resolution 405e095 2026-06-22 [gone]
flash-guard-banner 8dc7655 2026-07-27 [gone]
lcd-viewmodel 1e2d1b6 2026-07-27 [gone]
main df0d015 2026-07-20 [behind 82]
main-sync b6431ec 2026-07-27 [behind 20]
nrf-flash-path 86ea577 2026-07-27 [gone]
nrf-source-relay e1c9022 2026-07-27 [gone]
refactor/cps-spoof-split 82a99a4 2026-07-26 [gone]
refactor/nrf-peer-role-seam 85386a5 2026-07-26 [gone]
refactor/script-duplication 80e87eb 2026-07-26 [gone]
refactor/touchcal-ritual 121adcb 2026-07-26 [gone]
seam-tests-loopdrain 6596fd9 2026-07-27 [gone]
test/analysis-cp-routing 5507796 2026-07-26 [gone]
verify b6431ec 2026-07-27 [behind 20]
wifilink-devicestate ec16a60 2026-07-27 [gone]
origin be32624 2026-09-23 
origin/away-docs-sweep 7e1329d 2026-07-05 
origin/away-night-findings 02dcbe5 2026-07-05 
origin/ble-scan-selfheal adf423e 2026-07-05 
origin/cauldnz-review-synthesis 6310b3f 2026-09-23 
origin/cyd-boot-button-cal 42fe220 2026-07-03 
origin/cyd-panel-orientation f25c249 2026-07-03 
origin/cyd-ui-fixes 871d886 2026-07-03 
origin/docs/ui-architecture-review ad6e46f 2026-07-11 
origin/feat/annotation-layer a08cded 2026-06-21 
origin/feat/cyd-wifi-status bdf3771 2026-07-11 
origin/feat/obc-transmitter 2dae672 2026-07-07 
origin/feat/obc-web-config d9be04e 2026-07-08 
origin/fix-erg-bare-build d5d8584 2026-07-04 
origin/fix-import-workout-ct 8e5011d 2026-07-05 
origin/fix/c3-portal-ap-visibility 35c05d6 2026-07-13 
origin/ftms-erg-phase4 f6b31b0 2026-07-04 
origin/ftms-gatt-analysis 4696473 2026-07-06 
origin/lcd-picker-filter efe61d8 2026-07-04 
origin/main be32624 2026-09-23 
origin/nrf-ciq-compile 031369e 2026-07-05 
origin/nrf-imu-selftest 341ad27 2026-07-05 
origin/nrf-ota-picker 3757892 2026-07-05 
origin/nrf-quickwins e6f0ad5 2026-07-05 
origin/portal-subnet 3dc9557 2026-07-05 
origin/recovery-capture-plan 4b6f0c5 2026-07-06 
origin/ride-pcap-analysis 524bc5c 2026-07-06 
origin/s3-lvgl-unstall 03d3c6b 2026-07-04 
origin/spa-xss-fix f677d07 2026-07-05 
origin/three-board-web-ui cdf702a 2026-07-04 
origin/web-trainer-picker 202274c 2026-07-05 
origin/worktree-nrf52-sense 8601782 2026-07-04 
```
