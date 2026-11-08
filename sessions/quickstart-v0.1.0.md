# QuickStart — ride-test the `v0.1.0` release on bike 1

**Status: READY (2026-11-08).** The execution card for the **release acceptance gate** of `v0.1.0`
(the C3-OLED crank spoof). This is not a new session — it is
[session 14](session-14-two-bike-first-rides.md)'s **G0 + G1** run on a clean board, with the capture
points I need to judge the result. Generic tester flow:
[`beta/ONBOARDING.md`](../beta/ONBOARDING.md). Release mechanics:
[`beta/RELEASE-AND-OTA.md`](../beta/RELEASE-AND-OTA.md).

**The point of the run:** the bike has not seen our firmware since **2026-07-26**, and `firmware/`
has changed by 43 files since. `Cps.h` — the frames the SB20 actually eats — is untouched, but the
identity path is new. So **G1 is a re-proof of pairing, not a formality.** If it pairs and reads true,
`v0.1.0` gets tagged.

---

## 0. What you need in your hands

- The board I just flashed (**v0.1.0**, `esp32c3-oled-live-release`).
- Your phone, with the **Stages Cycling** app.
- **Bike 1 powered.** Its **real right crank `4963` must be awake and findable** — pedal it to wake
  it, and have a **fresh CR2032** ready. This is not optional: see step 4.
- Your **Assioma** pedals charged (`17039L` / `22428R`).
- A **data** USB cable if you need to re-power the board. *(The first cable I tried carried no data
  at all — the board lit up but never enumerated. If the board ever stops appearing, suspect the
  cable before you suspect the board.)*

## 1. Restore values — write these down before you change any pairing

If anything goes sideways, this is how bike 1 goes back to normal. From session 9's close-out.

| What | Put it back to |
|---|---|
| Stages app crank ids | **L = `62144`**, **R = `4963`** |
| Crank length | **165 mm** |
| ANT+ cal offsets | **L 903 / R 951** |

---

## 2. Step 1 — power the board and join its own WiFi

A clean board has nothing stored, so it comes up as an access point.

**This board, specifically** — flashed and bench-checked 2026-11-09, base MAC `38:44:be:43:c9:dc`:

| | |
|---|---|
| Its setup network | **`Setup-C9DC`** |
| What it derives if you leave **Crank name** blank | `Stages 91676` — **don't leave it blank**, a `9xxxx` id has never been pair-tested on a bike |

I confirmed `Setup-C9DC` is on air from the desk at 90% signal, which is what tells me the board
booted the release build and raised a stable portal rather than crash-looping. The **PIN is on the
OLED** — it is derived per board from a secret inside the firmware, so I can't read it from here.

1. Power the board over USB. The OLED shows **`SB20 SETUP`** with **`Setup-C9DC`** and a PIN.
2. On your phone, join the WiFi network **`Setup-C9DC`**. The setup page should open by itself; if
   not, browse to **`http://172.29.4.1`**.

> ⚠️ **Don't go looking for a `Stages …` crank on air yet — there isn't one, and that's correct.**
> While the setup portal is up the firmware deliberately holds BLE off and releases the Bluetooth
> controller altogether, so the SoftAP gets the radio to itself (it was a coexistence fix). The
> crank only starts advertising **after** you save WiFi and the board reboots. A scan before then
> finds nothing — by design, not a fault.

> 📸 **CAPTURE 1 — photo of the OLED** showing `SB20 SETUP`, the `Setup-C9DC` name and the PIN.
> *(Proves the board booted the release build and the display works.)*

## 3. Step 2 — WiFi, meter, and the crank identity

On that setup page:

1. **WiFi:** pick your garage/home network and enter the password.
2. **Source:** **Scan** → pick **`ASSIOMA`** (your `17039L` pedal) from the list.
3. **Tick “Single-sided source — double it for total.”** Your left pedal is the source; the board
   doubles it. (This is the `single=1` from session 9.)
4. Open **“Crank identity (advanced)”** and in **Crank name** type just the digits:

   ```
   62145
   ```

   **Type it bare, with no `Stages ` prefix — on purpose.** This is the one new behaviour in this
   release ([#384](https://github.com/cauldnz/SB20-power-proxy/issues/384)): the board should turn
   that into `Stages 62145` for you. Before this release it would have advertised a bare `62145`,
   which the bike never finds — that silently cost an outside user a session.

> 📸 **CAPTURE 2 — screenshot of the filled setup page**, *before* you press Save. I want to see the
> meter, the single-sided tick, and `62145` in the Crank name box.

5. **Save.** The board reboots (~2 s) and joins your WiFi; the OLED then shows its IP.

## 4. Step 3 — confirm the board is right *before* you touch the bike

Open **`http://sb20proxy.local/status`** (or the IP on the OLED).

> 📸 **CAPTURE 3 — screenshot of `/status`.** This is the most important capture of the whole run.

I am checking four things in it, and **all four must be right or stop here and send it to me**:

| Field | Must say | Why |
|---|---|---|
| `version` | **`0.1.0`** | `0.0.0-dev` means you are on a desk build and nothing after this counts as release evidence |
| `identity` | **`Stages 62145`** | the prefix was added for you — this is #384's fix working |
| `identity_default` | **`false`** | `true` means your entry did not stick and it is advertising a MAC-derived `Stages 9NNNN` |
| `source` | `ASSIOMA…` | the meter is pinned |

Then open the dashboard (**`http://sb20proxy.local/`**) and **spin the pedals**. You should see
**METER IN → CRANK OUT** with live watts and your L/R balance.

> 📸 **CAPTURE 4 — screenshot of the dashboard with live watts** while you spin.
> *(Proves the read→correct→rebroadcast chain works before the bike is involved.)*

**Green here = good to go to the bike.** Nothing below is worth attempting if this step is not clean.

---

## 5. Step 4 — pair the bike ⭐ this is the step the release hangs on

In the **Stages app**, set the bike's crank ids to:

- **Left = `62145`** ← our board
- **Right = `4963`** ← your real right crank

**The real right crank must be awake.** The SB20 needs **both** configured ids findable on air — if
either is missing you get *“pairing failed”* and no connection at all (session 9 §12). A phantom
right id **does not work**; session 8 tried `4964` and it failed. So pedal the right crank to wake it,
and swap in the fresh CR2032 if it will not show.

Set **crank length 165 mm** while you are in there.

> 📸 **CAPTURE 5 — screenshot of the app's crank-pairing screen** with both ids typed in.

> 📸 **CAPTURE 6 — screenshot of `http://sb20proxy.local/log`** immediately after pairing.
> *(After `/status`, this is the capture that tells me most. It shows `source:connected`, the
> control-point writes, and the `reason=531` disconnect-then-reconnect that is normal and expected.)*

**Expected:** the bike shows watts from your pedals; the app shows L = our spoof and R = `4963`.

## 6. Step 5 — is the power true?

Pedal steadily and compare what the **Stages app** shows against what the **Assioma's own app**
says for the same effort.

> 📸 **CAPTURE 7 — screenshot of the Stages app showing live power, cadence and L/R balance.**

## 7. Step 6 — ERG, and the one setting that defeats everybody

**Set the bike's ride mode to `External` in the Stages app.** Not `Grade`, not `Power/Erg` — in
those two the bike keeps the target itself and ignores the app, so an ERG workout looks like it does
nothing at all. This is exactly what happened in
[#382](https://github.com/cauldnz/SB20-power-proxy/issues/382).

> 📸 **CAPTURE 8 — screenshot of the ride-mode screen showing `External` selected.**

Then start an ERG workout in **Zwift or qz** and let it change target at least once.

> 📸 **CAPTURE 9 — screenshot of the app showing target vs actual**, and tell me in words:
> **do the shifters lock out, and does the resistance actually follow the target?**

⚠️ **Only one thing may drive ERG at a time.** Do not have a head-unit workout running as well.

## 8. Step 7 — before you get off the bike

> 📸 **CAPTURE 10 — `http://sb20proxy.local/diag`.** It is plain text — paste it into the chat
> rather than screenshotting it. It carries the whole config + status in one go.

Then **restore the app pairing** from the table in §1 (**L = `62144`**, R = `4963`) if you changed it.

---

## 9. Sending them back

Drop the images straight into this chat and label them `CAPTURE 1`…`CAPTURE 10` — you do not need to
upload them anywhere. If you only manage some, **3, 6 and 9 are the ones I most need** (`/status`,
`/log` after pairing, and the ERG result).

## 10. What each capture decides

| Capture | Gates |
|---|---|
| 1 OLED setup screen | the release build boots on a clean board |
| 2 setup page | the bare-id entry is what got saved |
| 3 `/status` | **the board is running `0.1.0` and #384's fix worked** — release evidence |
| 4 dashboard watts | read → correct → rebroadcast, off the bike |
| 5 app pairing screen | the recipe was entered as intended |
| 6 `/log` after pairing | **G1 PASS/FAIL** and, if it failed, why |
| 7 app power | the spoof reads true, not just connected |
| 8 ride mode | the ERG result below is interpretable at all |
| 9 ERG target vs actual | session 14 **G2** — whether `v0.1.0`'s notes may claim ERG |
| 10 `/diag` | the durable record for `decisions.md` |

## 11. If it goes wrong

| Symptom | First thing to check |
|---|---|
| “Pairing failed” in the app | Is the **real right crank `4963` awake**? Both ids must be findable. `/log` will show whether the bike ever connected. |
| Board not on WiFi / no `sb20proxy.local` | Use the IP on the OLED. If it is back on `SB20 SETUP`, the WiFi password did not take. |
| `/status` says `0.0.0-dev` | You are on an old build, not the release. Stop and tell me. |
| `identity_default: true` | The crank name did not save. Re-enter it in `/setup`. |
| Live power on the board but nothing on the bike | A pairing problem, not a power problem — go to `/log`. |
| ERG does nothing | **Ride mode is almost certainly not `External`.** Check capture 8 first. |
| Board froze / screen dead | Unplug, replug, and tell me what you were doing. |

**Do not fight a problem on the bike.** Grab `/log` and `/diag` and send them — that is faster than
debugging under load, and it is how #384 got found and fixed.
