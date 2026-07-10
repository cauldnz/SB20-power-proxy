#pragma once
#include <Arduino.h>

// Board capability seam for the nRF52840 builds. The Seeed XIAO nRF52840 Sense (default) has a 3-channel
// RGB status LED + an LSM6DS3 IMU; a generic / Adafruit-Feather-family board typically has a single LED
// and no onboard IMU. Select a non-XIAO board with -DBOARD_FEATHER (or -DBOARD_GENERIC_NRF52). Only the
// RGB-LED macros (LED_RED/GREEN/BLUE) are a *compile* blocker off the XIAO — the IMU driver links on any
// nRF52 and fails safe at runtime (g_imuOk = false), so it needs no gating to build.

#if defined(BOARD_FEATHER) || defined(BOARD_GENERIC_NRF52)
#define BOARD_HAS_RGB_LED 0
#ifndef BOARD_HAS_IMU
#define BOARD_HAS_IMU 0  // no onboard IMU → recording is inert (imu.begin() fails safe)
#endif
#else  // XIAO nRF52840 Sense (default)
#define BOARD_HAS_RGB_LED 1
#ifndef BOARD_HAS_IMU
#define BOARD_HAS_IMU 1
#endif
#endif

// Glanceable status LED. RGB boards drive three active-LOW channels (XIAO); single-LED boards light
// LED_BUILTIN if ANY channel is requested (so link/searching/recording is at least on-vs-off).
inline void boardLedBegin() {
#if BOARD_HAS_RGB_LED
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
#elif defined(LED_BUILTIN)
    pinMode(LED_BUILTIN, OUTPUT);
#endif
}
inline void boardLed(bool r, bool g, bool b) {
#if BOARD_HAS_RGB_LED
    digitalWrite(LED_RED, r ? LOW : HIGH);  // active-low
    digitalWrite(LED_GREEN, g ? LOW : HIGH);
    digitalWrite(LED_BLUE, b ? LOW : HIGH);
#elif defined(LED_BUILTIN)
    digitalWrite(LED_BUILTIN, (r || g || b) ? HIGH : LOW);  // single LED (Feather LED is active-high)
#else
    (void)r;
    (void)g;
    (void)b;
#endif
}
