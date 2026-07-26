#pragma once
// Undo the Arduino core's function-like MACROS so the shared pure headers compile.
//
// WHY THIS EXISTS
// The Adafruit nRF52 core's `Arduino.h` defines `abs`, `round`, `min`, `max` and `constrain` as
// preprocessor macros. The pure, host-tested headers in `firmware/lib/proxy` (shared with the
// ESP32 builds via `lib_extra_dirs`) use the real `std::round`, `std::min<>` etc. A macro named
// `min` rewrites `std::min<uint16_t>(a, b)` into nonsense before the compiler ever sees it, so the
// pure code fails to compile — with an error that points at the pure header and gives no hint that
// a macro three includes earlier is the cause.
//
// HOW TO USE IT
// In any translation unit that includes BOTH an Arduino header and a pure header, include this
// file in between:
//
//     #include <Arduino.h>
//     #include <bluefruit.h>
//     #include "arduino_compat.h"   // <-- before any pure header
//     #include "Correction.h"
//
// Including it is harmless when the macros are absent (`#undef` of an undefined macro is legal),
// so it is safe to include unconditionally.
//
// WHY A HEADER RATHER THAN THE FIVE LINES INLINE
// It was five bare `#undef`s in `src/main.cpp` with the reasoning in a comment. Any NEW .cpp that
// pulled in Arduino.h and a pure header hit the same wall with none of that context, and the
// obvious "fix" (stop using std:: in the pure header) would have damaged the shared code to suit
// one toolchain. Naming the workaround makes it discoverable and greppable. A CI guard
// (`code/tests/test_arduino_compat_guard.py`) fails the build if these `#undef`s reappear loose in
// a source file, so the explanation cannot be bypassed and silently re-copied.
//
// DO NOT add unrelated portability shims here — this file has exactly one job.

#undef abs
#undef round
#undef min
#undef max
#undef constrain
