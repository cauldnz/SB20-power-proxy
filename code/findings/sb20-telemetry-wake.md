# SB20 telemetry "wake" — what the banked capture does and does not show (#288)

**Status: RESEARCH** (desk analysis of banked captures, 2026-10-02). Nothing here has been proven on
the bike. It reads two captures from session 13 and one from session 6. Correlation is not causation:
every causal statement below is labelled as a hypothesis.

## TL;DR

1. **There is a degraded-to-healthy transition in the capture.** It happens at
   **2026-07-26T00:05:28.676Z** (10:05:28 local) in `SNIFF-session13-sb20-app-wake-20260726.pcap`.
2. **"Degraded" does not mean the flags changed.** The SB20 sends Indoor Bike Data (`0x2AD2`) as
   separate frames. Speed (`0x0000`) and distance (`0x0011`) frames arrive in both states. In the
   degraded state the **cadence + power frame (`0x00c5`) is missing**, and CSC crank revolutions are
   frozen. The healthy state has all three frames.
3. **The capture does not contain the Stages app's link.** The sniffer followed one connection, qz's
   (access address `0x693c1f72`), from 23:48:10Z to the end. The app's writes to the bike are not
   in this pcap, so no byte-exact "app wake sequence" can be read from it.
4. **Nothing on qz's link preceded the transition.** qz's last write was 11 min 1 s earlier
   (23:54:27.6Z, FTMS Set Target Power 10 W). Between that write and the transition, the only
   non-periodic event on the link is a burst of **11 notifications on the vendor channel
   `0c46beb0`**: `fd 04` ×10 and `fd 03` ×1, at 23:59:12.5–13.7Z, **6 min 16 s before** the
   transition. qz did not request them. In session 6 the bike sent `fd 04` only in reply to the
   Stages app's vendor commands. So this burst is the best on-air footprint of the app, or of
   some other event inside the bike.
5. **New lead: the crank link.** In the two minutes of adverts before qz connected (degraded
   state), the **real left crank `E8:CF:D8:D9:3A:20` "Stages 62144" was advertising as
   connectable**, so the bike was not connected to it. The CYD (`8C:94:DF:93:CC:8E`) was **also
   advertising as "Stages 62144"**. In the cold-start capture, the bike itself (`E4:AA:5A:D6:0E:D4`,
   acting as a central) **connected to the real left crank 23 s before qz connected**. That capture
   was healthy from its first frame. The fields that go missing (cadence, power, crank revs) are
   exactly the ones that come from the crank. The fields that survive (speed, distance) do not.
   **Hypothesis:** "degraded" means the bike has no live crank link, and "wake" means the bike
   re-acquired its crank.

The rest of this doc covers the method, the timeline, the candidate sequences, the competing
explanations, and the on-bike experiment that would settle it.

## 1. What was captured, and what was not

| Capture | Window (UTC) | What the sniffer saw |
|---|---|---|
| `SNIFF-session13-sb20-app-wake-20260726.pcap` (29,339 frames) | 23:46:11.411 – 00:09:33.012 | adverts for 119 s (6,270 frames), then **only** the qz↔bike link `0x693c1f72` (23,069 frames) |
| `SNIFF-session13-coldstart-qz-only.pcap` (20,428 frames) | 00:21:17.617 – 00:37:08.189 | adverts for 23 s (1,156 frames, including the bike→crank `CONNECT_IND`), then only the qz↔bike link `0xbebad4e3` |
| `SNIFF-sb20-app-20260621-1713.pcap` (session 6) | 2026-06-21 07:26:42 – 07:32:00 | the **Stages app**↔bike link `0xed6109c5` from connect, with the app's vendor init on `0c46beb1` |

The nRF sniffer follows **one connection at a time** (`nrf-sniffer.md`). Once it locked onto qz's
`CONNECT_IND` it saw nothing else: no adverts, no other centrals, and no bike↔crank link.
**Local time is UTC+10.** The session-13 qz log's own epoch/local pairs show this: `10:21:50` local
= `1785025310` = 00:21:50Z.

## 2. Method (reproducible)

- **Index:** `sb20proxy.analysis.pcap_sqlite.import_pcap` into a scratch SQLite DB (the
  `14_build_pcap_fit.py` path, restricted to the session-13 pcaps). It produced 2,624 ATT records
  and 1,016 Indoor Bike Data frames for the wake capture, and 3,265 ATT records and 1,749 Indoor
  Bike Data frames for the cold start.
- **Link layer** (connections, adverts, `CONNECT_IND`): `tshark -T fields` over `btle.access_address`,
  `btle.advertising_header.pdu_type`, `btle.advertising_address`, `btle.initiator_address`, and
  `btcommon.eir_ad.entry.device_name`.
- **CSC crank revolutions:** `tshark -Y "btatt.handle==0x003c && btatt.opcode==0x1b" -e
  btatt.csc_measurement.cumulative_crank_revolutions`. The index stores an empty `value_hex` for
  CSC, CCCD and FTMS Status, because Wireshark dissects those into named fields rather than
  `btatt.value` (a known indexer limitation, §8).
- **Handle map:** the GATT discovery in the capture itself (§3).
- **Indexer fix made in this PR:** `resolve_char` labelled every `0c46be…` vendor characteristic
  `stages_prop_0c46_f4e5`, which merged the button channel `be60` with `beb0`. It now labels
  `stages_prop_0c46be60` / `…be61` / `…beb0` / `…beb1` (test in `test_pcap_sqlite.py`).

## 3. The bike's GATT, from discovery in this capture

| Value handle | CCCD | Characteristic | Properties |
|---|---|---|---|
| `0x000c` | `0x000d` | Service Changed `0x2A05` | indicate |
| `0x001d` | `0x001e` | **Indoor Bike Data `0x2AD2`** | notify |
| `0x0020` | `0x0021` | Fitness Machine Status `0x2ADA` | notify |
| `0x0023` | `0x0024` | **FTMS Control Point `0x2AD9`** | write, indicate |
| `0x002f` | `0x0030` | vendor `0c46be60` (buttons) | notify |
| `0x0032` | `0x0033` | vendor `0c46be61` | notify |
| `0x0036` | `0x0037` | vendor **`0c46beb0`** (the app channel's replies) | notify |
| `0x0039` | — | vendor **`0c46beb1`** (the app channel's commands) | **write without response** |
| `0x003c` | `0x003d` | **CSC Measurement `0x2A5B`** | notify |
| `0x0043` | `0x0044` | SC Control Point `0x2A55` | write, indicate |
| `0x0047` | `0x0048` | Nordic buttonless DFU | write, indicate |

**Correction to the issue body:** qz did **not** leave `be61`/`beb0` untouched. It wrote `01 00`
(notify on) to their CCCDs (`0x0033`, `0x0037`) in both captures. It **never wrote `0c46beb1`**.

## 4. Timeline — the wake capture

All times are 2026-07-25/26 UTC. The bike is `E4:AA:5A:D6:0E:D4` and the qz host is
`e4:70:b8:73:f9:82`.

| UTC | Link | Event (byte-exact where relevant) |
|---|---|---|
| 23:46:11.4 – 23:48:10.2 | adverts | The bike advertises "Stages Bike 0105". The **real left crank `E8:CF:D8:D9:3A:20` "Stages 62144" advertises as connectable** (10 frames, last at 23:47:45). The **right crank `E3:25:39:38:92:71` "Stages 4963"** also advertises (7, last at 23:47:32). The **CYD `8C:94:DF:93:CC:8E` advertises as "Stages 62144"** (61, 23:46:12–23:46:44, RSSI ≈ −28). The nRF board `DE:F2:ED:C4:F3:FD` "SB20 Bridge" advertises. There is **no `CONNECT_IND` from the bike to any crank** in this window. |
| 23:47:07.5 | adverts | One malformed `CONNECT_IND` (initiator `6f:2a:33:b9:15:ec`, payload truncated). Its target cannot be decoded, so it is ignored. |
| 23:48:10.211 | — | `CONNECT_IND` from the qz host to the bike, access address `0x693c1f72`. The sniffer follows this link to the end of the capture. |
| 23:48:10 – 23:48:22 | qz | MTU exchange, full service and characteristic discovery, DIS and FTMS static reads |
| 23:48:22.48 – 23:48:25.11 | qz | CCCD writes: `0x0d`=`02 00`, `0x3d`=`01 00`, `0x44`=`02 00`, `0x1e`=`01 00`, `0x21`=`01 00`, `0x24`=`02 00`, `0x48`=`02 00`, `0x30`=`01 00`, `0x33`=`01 00`, `0x37`=`01 00` |
| 23:48:25.114 | qz | First IBD notification, `11 00 84 06 00` (distance 1,668 m). From here the IBD alternates `0x0011` and `0x0000` frames about every 3 s, with **no `0x00c5`**. CSC crank revolutions are **2687 and frozen**. |
| 23:48:38.76 – 23:48:41.30 | qz | All 10 CCCDs written `00 00`. A qz process exited; session 13 records three orphaned qz processes. |
| 23:49:30.73 – 23:49:40.38 | qz | DIS/FTMS re-read and the same 10 CCCDs re-enabled on the same link. This is another qz process on the shared BlueZ connection. |
| 23:49:41.8 | qz | Speed 25.6 km/h with distance climbing. Still no `0x00c5`, and CSC is still 2687. The flywheel is turning, but the bike reports no cadence or power. |
| 23:52:25.16 | qz | First button frame on `0c46be60` (`01 00 01 00`, …). Button traffic continues to 00:02:51.6. |
| 23:52:25.84 / 26.33 / 26.63 | qz | FTMS CP `00` → indication `80 00 01`; `07` → `80 07 01`; `05 05 00` (5 W) → `80 05 01 05 00`; Status `0x08` (target power changed) |
| **23:52:28.28** | qz | Speed **27.56 → 0.00 km/h between two consecutive frames**, 2 s after erg engaged. Distance then **freezes at 3,182 m** (`11 00 6e 0c 00`). The step is instantaneous, so it is not a coast-down. |
| 23:54:27.62 | qz | FTMS CP `05 0a 00` (10 W) → `80 05 01 0a 00`. **This is the last ATT write on the link.** |
| **23:59:12.521 – 13.690** | qz | **11 unsolicited `0c46beb0` notifications:** `fd04` at .521, .810, .812, .815; `fd03` at 13.398; `fd04` at 13.399, .591, .592, .688, .689, .690. qz wrote nothing near them. |
| 00:02:51.62 | qz | Last button frame |
| 00:04:40 – 00:05:27.6 | qz | Steady degraded pattern: `11 00 6e 0c 00` / `00 00 00 00` (distance 3,182 m frozen, speed 0) |
| **00:05:28.676** | qz | **First `0x00c5` frame: `c5 00 00 00 00 00 00 00`** (cadence 0, power 0). From here it arrives about 1 Hz between the speed and distance frames. |
| 00:05:41.156 | qz | First non-zero `0x00c5`: `c5 00 8d 00 41 00 00 00` (70.5 rpm, 65 W) |
| 00:05:41.164 / 42.034 / 42.717 | qz | Distance resumes (3,186 m). CSC crank revolutions move **2687 → 2688**. Speed 16.6 km/h. |
| 00:05:41 – 00:09:33 | qz | Healthy: 228 `0x00c5` frames, 81 `0x0011`, 80 `0x0000`. CSC changes 222 times. |

Counts on the followed link: **before** 00:05:28.6 there were 310 `0x0011`, 317 `0x0000` and
**0 `0x00c5`** frames. **After** it there were 81, 80 and **228**.

### The cold-start capture (healthy throughout), for contrast

| UTC | Event |
|---|---|
| 00:21:17.617 | The sniffer starts |
| **00:21:17.640** | **`CONNECT_IND` with the bike `E4:AA:5A:D6:0E:D4` as initiator to the real left crank `E8:CF:D8:D9:3A:20`.** The bike is a BLE central to its crank (the "Pair with Bluetooth" topology, `sb20-hardware-reference.md`). |
| 00:21:40.243 | `CONNECT_IND` from the qz host to the bike (`0xbebad4e3`). The discovery and CCCD writes are byte-identical to the wake capture. |
| 00:21:51.093 | **The first IBD notification is `c5 00 00 00 00 00 00 00`.** `0x00c5` is present from the very first frame, 4 min **before** qz's first FTMS write (00:26:04.2). |
| 00:22:35.9 | CSC crank revolutions **0 → 1**. The counter restarted at 0, which confirms the power cycle. |
| whole capture | 1,131 `0x00c5`, 307 `0x0011`, 310 `0x0000` frames. **No `0c46beb0` notifications at all.** |

So the healthy state needs **no** qz control-point traffic. qz's GATT behaviour is identical in
both captures, so qz is not the variable.

## 5. Candidate "wake" sequences

### 5a. What the capture shows directly (byte-exact)

Between qz's last write and the transition, the only non-periodic event on the observed link is this
`0c46beb0` (handle `0x0036`) burst. qz caused none of it:

```
23:59:12.521  fd 04
23:59:12.810  fd 04      23:59:12.812  fd 04      23:59:12.815  fd 04
23:59:13.398  fd 03      23:59:13.399  fd 04
23:59:13.591  fd 04      23:59:13.592  fd 04
23:59:13.688  fd 04      23:59:13.689  fd 04      23:59:13.690  fd 04
```

`fd 03` has not been seen before in any capture.

### 5b. What the Stages app sends on connect (session 6, byte-exact)

This is the only record of the app's own conversation with the bike. Commands go out on
`0c46beb1` (write without response, handle `0x0039`) and replies come back on `0c46beb0`.
Retransmitted duplicates are dropped below. The app subscribes only to Service Changed and `beb0`.
It never subscribes to IBD or CSC, and it never writes FTMS.

| t (s after `08 00`) | App → `beb1` | Bike → `beb0` | Reading (hypothesis) |
|---|---|---|---|
| 0.00 | `08 00` | `08 00` | hello / mode query |
| 0.75 | `01 08` | `01 09 c0 f2` | `0xf2c0` = **62144**: left-crank id |
| 0.87 | `01 0b` | `01 0c 63 13` | `0x1363` = **4963**: right-crank id |
| 0.93 | `0d 02` | `0d 02 c0 f2 31 2e 38 2e 32` | crank 62144, firmware "1.8.2" |
| 1.05 | `0d 04` | `0d 04 63 13 31 2e 38 2e 32` | crank 4963, firmware "1.8.2" |
| 1.14 | `0c 00 01` | `0c 00 01` | — |
| 1.35 | `0c 02 00 02 01 32 6b 01 04 64 00` | `0c 02 01 02 01 32 6b 01 04 64 00` | config header |
| 1.47 | `0c 03 …` ×6 (u16-LE ramp 500, 632, 765, …) | — | a table upload, possibly gears/resistance. **Writes bike config.** |
| 1.77 | `0c 02 0b 02 01 32 6b 01 04 64 00` | `0c 02 0c …`, `0c 01 02 01 16 d5 0c 00 00 00 00` | — |
| 1.83 | `fd 00` | `fd 01`, **`fd 04`** | status handshake |
| 1.95 | `05 01 d4 00 01` | echo, `fd 04` | — |
| 2.04 | `0b 00 01 02 05 03 04 05 01 02 03 04 01 02 03 04 00` | echo, `fd 04` | button/gear mapping? **Writes bike config.** |
| 2.10 | `10 00 00` | echo, `fd 04` | — |
| 2.16 | `03 01 24 2c 20 03` | echo, `fd 04` ×2 | — |
| 2.37 | `0e 00` | `0e 00 88 04 00 02 01` | — |
| 2.49 | `0a 00 00 00` | `0a 01 54 01 c0 f2 ce 61 b4 00 63 13 4a 9a 37 01` | carries **both crank ids** (`c0f2`, `6313`) |
| 2.58 | `10 01` | `10 01 00 02` | then `08 02 …` telemetry frames on `beb0` at about 2 Hz |
| 11.7 → | `02 00 <u16-LE> 00 00` streamed | — | the app's erg setpoint (decisions 2026-06-21) |

In session 6, **every `fd 04` came 5–100 ms after an app write**. So the session-13 burst is
consistent with another central running part of this init on the vendor channel at 23:59:12Z. The
bike would send the `fd` statuses to every subscriber of `beb0`, and qz was one. It is equally
consistent with the bike raising those statuses for an internal reason.

### 5c. The minimal candidate, honestly stated

**No minimal wake sequence can be identified from this capture**, because the app's link was not
followed. There are two candidates, and they are not exclusive:

1. **The app's vendor init, if the app is the cause.** The smallest plausible subset is the
   crank-touching commands, all of which look like reads: `01 08`, `01 0b`, `0d 02`, `0d 04`,
   `0a 00 00 00`, plus the `fd 00` status handshake and `10 01` (which starts the vendor telemetry).
   The table, mapping and config writes (`0c 02`, `0c 03`, `05 01 …`, `0b 00 …`, `10 00 00`,
   `03 01 …`) change bike settings. They are the last thing to replay, not the first.
2. **The bike re-acquiring its crank (no app command needed).** The observed footprint would be a
   bike→crank `CONNECT_IND` shortly before the first `0x00c5`. It was not observable here, because
   the sniffer was locked onto qz's link.

## 6. Competing explanations

| # | Explanation | For | Against / gaps |
|---|---|---|---|
| H1 | **Crank link lost, then regained.** Degraded = the bike has no live crank. The wake is the bike reconnecting to it. | The missing fields are exactly the crank-derived ones. In the degraded window the **real left crank was advertising** (unconnected). An **imposter "Stages 62144" (the CYD)** was on air, and session 13 G0 had found that collision. In the cold start the bike connected to the real crank before qz, and was healthy from frame 1. The first `0x00c5` is all zeros and real data follows about 12 s later, which looks like a link coming up before a crank event arrives. | No bike→crank connection event was captured in the wake window (the sniffer was on qz's link). We don't know why the link would come back at 00:05:28. The CYD's adverts stop at 23:46:44, 18 min earlier. |
| H2 | **The Stages app's vendor commands re-arm the bike.** | Owner observation: the stream woke after the app connected. The `fd 04`/`fd 03` burst at 23:59:12 matches the app's init footprint. The app's init reads both crank ids, so it may restart the bike's crank search, which would be H1 by another route. | The burst is **6 min 16 s before** the transition. The app's whole init takes about 3 s, so a direct effect should show within seconds. The app's link is not in the capture. The SB20 stops advertising once a central connects (session 13), so it is unclear how the app reached `E4:AA:5A` at all while qz held it. |
| H3 | **A Garmin paired as a trainer degrades other consumers** (owner, #288). | It fits the cold-start fix: the watch's trainer pairing was removed. | **In this capture the stream recovered at 00:05:28 while the Garmin was still trainer-paired.** The watch was changed only during the cold-start prep, after 00:16Z, per the issue comments. So H3 cannot explain this recovery. It can still explain how the degradation started. |
| H4 | **Pedalling, crank wake or a timeout.** The crank sleeps and wakes on pedalling; the bike retries on a timer. | Real data starts at 00:05:41, the same second as distance, CSC and speed resume. That could simply be the rider starting to pedal. | The `0x00c5` frame appears 12.5 s *before* any motion. The speed and distance frames had shown the flywheel turning at 23:49–23:52 with no `0x00c5`, so pedalling alone did not restore it then. |
| H5 | **Process churn on qz's side** (orphaned processes, unsubscribe/resubscribe). | Session 13 records it. | No qz ATT activity at all in the 11 min before the transition. The healthy capture used the identical GATT sequence. |

**Leading desk reading: H1** (possibly reached through H2). **Confidence: low to moderate.** It
rests on n = 1 transition and one cold-start contrast. The decisive observable, a bike→crank link
event at the transition, was not captured.

One side observation needs its own check. In the degraded state, **engaging erg (Start + 5 W) was
followed 2 s later by speed dropping to 0 and distance freezing**. In the healthy capture the same
`00 / 07 / 05 05 00` sequence left speed alive. If speed is derived from crank power once erg is
on, that again points at a missing crank.

## 7. Proposed on-bike experiment (fold into session 14 G1b)

**Goal:** one variable at a time, with an instrument on the link that matters. The gaps in this
analysis came from sniffing the wrong link.

**Instruments:**
- **The sniffer follows the real left crank `E8:CF:D8:D9:3A:20`, not the bike.** Start it before
  the bike powers on (sniff before connect). That captures the bike→crank `CONNECT_IND`, every
  disconnect, and the crank's CPS stream. The bike is the initiator, so the link is only visible by
  following the crank.
- **One consumer on the bike,** logging every `0x2AD2` frame and `beb0` notification to JSONL:
  `06_capture_ble.py --subscribe-all --address E4:AA:5A:D6:0E:D4`, or qz with its log.
- **Detector:** speed/distance frames arriving with **no `0x00c5` for more than 5 s** means
  degraded. The same rule is what the full proxy's `/status` flag would use (`sb20-full-proxy.md` §5).

**Steps:**
1. **Baseline.** Cold-start the bike with the CYD, the nRF board and every other spoof off, the
   watch off, and the app closed. Expect a bike→crank `CONNECT_IND`, then `0x00c5` from the first
   frame (this reproduces the cold-start pcap).
2. **H1 induce:** with the CYD advertising "Stages 62144" **before** the bike powers on,
   cold-start. Record whether the bike connects to the imposter or the real crank, and whether
   `0x00c5` is present. Then switch the CYD off and **wait 10 min pedalling without touching
   anything** (this also controls for H4). Pass for H1: `0x00c5` presence tracks the bike's link
   to a working crank within seconds of its `CONNECT_IND`.
3. **H3 induce (G1b as planned):** from a healthy state, pair the Garmin to the trainer and watch
   the detector. Unpair it and watch for recovery without a power cycle.
4. **H2 recovery, only if a degraded state was reproduced and has persisted 5 min:** open the
   Stages app and connect once. Note the exact time and whether the app connects to the bike or a
   crank, then compare against the sniffer and the detector. **Do not replay the vendor bytes
   ourselves until this step shows an effect.** If it does, the next desk task is a
   `--vendor-write` option for the capture tool. No tool writes arbitrary bytes to `0c46beb1` today:
   `06_capture_ble.py --control-point` speaks CPS ops only. The replay would then go
   reads-first (`08 00`, `01 08`, `01 0b`, `0d 02`, `0d 04`, `0a 00 00 00`, `fd 00`, `10 01`),
   one command every 10 s against the detector, with the config writes held back.

## 8. Open questions

- Is "degraded" exactly "no crank link"? (Step 2.)
- Can a second central reach `E4:AA:5A` while qz holds it? The `fd` burst on qz's link suggests
  another actor on the vendor channel, but session 13 measured that the bike stops advertising.
- What do `fd 03` and `fd 04` mean? (Status codes. `fd 00` → `fd 01` + `fd 04` in session 6.)
- Why did speed drop to 0 two seconds after erg engaged in the degraded state?
- **For the product:** under the full proxy, our board *is* the bike's left crank (the spoof). If
  H1 holds, a spoof restart or dropout produces exactly this degraded state. The proxy should
  watch for it and reconnect, not just flag it.
- **Tooling gap:** `pcap_sqlite` stores an empty `value_hex` for CSC, CCCD and FTMS Status. Wireshark
  dissects those into named fields, so `btatt.value` is empty. CSC crank revolutions had to be
  pulled with a separate tshark field query.

## Related

[`ftms-protocol.md`](ftms-protocol.md) (the separate-frame IBD layout) ·
[`shifter-ble-protocol.md`](shifter-ble-protocol.md) (the vendor services) ·
[`sb20-hardware-reference.md`](sb20-hardware-reference.md) (the bike↔crank topology) ·
[`sb20-full-proxy.md`](sb20-full-proxy.md) §3h, §5 · [`nrf-sniffer.md`](nrf-sniffer.md) ·
`sessions/session-13-qz-obc-consumer-and-sb20-buttons.md` · issue #288.
