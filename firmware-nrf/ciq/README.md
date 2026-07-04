# Bridge Remote (Connect IQ)

Garmin device app for the nRF52840 Bike Bridge: live out/in watts + correction on screen,
SELECT toggles the on-board IMU track recording. Consumes the Bridge GATT contract
(`../GATT.md`, PROTO_VER 1).

## Build (needs the Connect IQ SDK — Garmin-login-gated, owner installs)

1. Install the SDK via Garmin's **Connect IQ SDK Manager** (developer.garmin.com; needs your
   Garmin login) + the device files for **Edge 540** and **Epix 2**.
2. Generate a developer key once: `openssl genrsa -out dev_key.pem 4096` then
   `openssl pkcs8 -topk8 -inform PEM -outform DER -in dev_key.pem -out dev_key.der -nocrypt`.
3. Compile: `monkeyc -f monkey.jungle -o BridgeRemote.prg -y dev_key.der -d edge540`
   (repeat with `-d epix2`).
4. Sideload: copy the `.prg` to the device's `GARMIN/Apps/` over USB mass storage.

Status: written against CIQ API 3.3 (BluetoothLowEnergy); NOT yet compiled — the SDK
download is account-gated. Expect a round of compile fixes on first build.
