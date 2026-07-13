#pragma once
// Minimal host-side <Arduino.h> shim — JUST enough for firmware/src/ui/LvglUi.cpp to compile and
// link under the native (desktop/CI) test env [env:native-lvgl]. LVGL itself is portable C; this
// covers the handful of Arduino/ESP symbols LvglUi.cpp touches (millis, a no-op Serial, psramFound,
// heap_caps_malloc) so the LVGL head-unit UI can be rendered + tapped on the host with no board.
// See code/findings/ui-unification.md §U5.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// --- time -----------------------------------------------------------------------------------
// The test owns the clock (LVGL reads it via lv_tick_set_cb -> millis()), so timers/animations are
// deterministic: advance it with lvglShimAdvanceMs() between pumps.
inline unsigned long g_lvglShimMillis = 0;
inline unsigned long millis() { return g_lvglShimMillis; }
inline void lvglShimAdvanceMs(unsigned long ms) { g_lvglShimMillis += ms; }

// --- Serial (no-op sink) --------------------------------------------------------------------
// LvglUi's SCREEN serial-dump path writes here; on the host we simply discard it.
struct HostSerial {
    int printf(const char*, ...) { return 0; }
    std::size_t write(const std::uint8_t*, std::size_t n) { return n; }
    std::size_t write(std::uint8_t) { return 1; }
    void print(const char* = "") {}
    void println(const char* = "") {}
    void setTxTimeoutMs(std::uint32_t) {}
};
inline HostSerial Serial;

// --- ESP heap / PSRAM -----------------------------------------------------------------------
#ifndef MALLOC_CAP_DMA
#define MALLOC_CAP_DMA 0
#endif
#ifndef MALLOC_CAP_8BIT
#define MALLOC_CAP_8BIT 0
#endif
inline bool psramFound() { return false; }
inline void* heap_caps_malloc(std::size_t sz, std::uint32_t /*caps*/) { return std::malloc(sz); }
