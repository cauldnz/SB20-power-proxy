#pragma once
// OPTIONAL — you usually do NOT need this file.
//
// Networked firmware updates use the signed-PULL path (the device fetches + verifies a signed image
// itself; see code/findings/ota-update-plan.md). Day-to-day dev flashing is over USB
// (code/scripts/flash_c3.py) or, if you opt in here, over authenticated ArduinoOTA.
//
// Copy this to `ota_secret.h` (gitignored) ONLY if you want the convenience of authenticated
// over-the-air *push* flashing during development. Defining OTA_PASSWORD turns the ArduinoOTA
// listener ON (port 3232) and requires that password to flash:
//   firmware/flash.ps1                              # reads OTA_PASSWORD from this file, passes espota -a
//   espota.py -i <ip> -p 3232 -f firmware.bin -a <password>
//
// WITHOUT this file, push OTA is DISABLED (fail-closed) — there is no networked flash listener at all,
// which is the secure default. Flash over USB instead. (2026-06-24 security review, Vuln 1: the old
// open ArduinoOTA + open /update form let anyone on the LAN flash arbitrary firmware.)
//
// Pattern mirrors wifi_secret.example.h.

#define OTA_PASSWORD "choose-a-long-random-passphrase"
