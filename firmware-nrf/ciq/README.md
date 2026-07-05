# Bridge Remote (Connect IQ)

Garmin device app for the nRF52840 Bike Bridge — an **in-ride controller** for the bridge.
Consumes the Bridge GATT contract (`../GATT.md`, PROTO_VER 1).

On screen: live out/in watts + correction, the erg/workout line, and recording state.

Controls:
- **SELECT** — toggle the on-board IMU track recording.
- **MENU** — start / pause / resume the loaded workout (erg).
- **UP / DOWN** — the *shifter*: nudge the erg target ±10 W (the bridge clamps to ±200 W).

Workout **setup** (pick the trainer, load a preset) is done from the Web Bluetooth app; the
Garmin drives the ride. The erg line only appears once a workout is loaded on the bridge, and
goes green when the trainer has granted control.

## Build (needs the Connect IQ SDK — Garmin-login-gated, owner installs)

1. Install the SDK via Garmin's **Connect IQ SDK Manager** (developer.garmin.com; needs your
   Garmin login) + the device files for **Edge 540** and **Epix 2**.
2. Generate a developer key once: `openssl genrsa -out dev_key.pem 4096` then
   `openssl pkcs8 -topk8 -inform PEM -outform DER -in dev_key.pem -out dev_key.der -nocrypt`.
3. Compile: `monkeyc -f monkey.jungle -o BridgeRemote.prg -y dev_key.der -d edge540`
   (repeat with `-d epix2`).
4. Sideload: copy the `.prg` to the device's `GARMIN/Apps/` over USB mass storage.

`build.sh` builds both targets (`edge540` + `epix2`); it needs a JDK 17+ on PATH (the SDK
bundles none), the SDK, and the dev key.

Status: compiles clean for **edge540** + **epix2** (CIQ SDK 9.2, Toybox.BluetoothLowEnergy).
The record path is bench-proven; the erg/shifter path mirrors the P4 Workout characteristic and
is pending on-device validation on the owner's Edge 540 / Epix 2.
