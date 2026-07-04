#!/usr/bin/env bash
# Build Bridge Remote for both target devices. Needs: the Connect IQ SDK (via Garmin's SDK
# Manager, account-gated) + a JDK 11+ on PATH + a developer signing key.
#   JDK:  any Temurin/OpenJDK 17+; put its bin on PATH.
#   Key:  openssl genrsa -out dev_key.pem 4096
#         openssl pkcs8 -topk8 -inform PEM -outform DER -in dev_key.pem -out dev_key.der -nocrypt
set -euo pipefail
SDK="${CIQ_SDK:-$HOME/AppData/Roaming/Garmin/ConnectIQ/Sdks/*/bin}"
KEY="${CIQ_KEY:-$HOME/.ciq-keys/dev_key.der}"
MONKEYC=$(ls $SDK/monkeyc.bat 2>/dev/null | head -1)
for dev in edge540 epix2; do
  "$MONKEYC" -f monkey.jungle -o "BridgeRemote-$dev.prg" -y "$KEY" -d "$dev"
  echo "built BridgeRemote-$dev.prg"
done
