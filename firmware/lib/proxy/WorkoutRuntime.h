#pragma once
#include <cstdint>
#include <string>

#include "WorkoutEngine.h"

namespace sb20proxy {

// The live workout clock — the thin stateful layer over the pure WorkoutEngine that the device seam
// drives. It owns the loaded Workout + run state (running / paused) and a monotonic clock, so the
// firmware can start / pause / resume / skip / stop a structured workout and report the live cursor
// for the Workout screen + (phase 4) the FTMS erg setpoint.
//
// Time is INJECTED as a millisecond count (the caller passes millis()), so all of this is pure and
// host-tested with a fake clock — only the call site that passes millis() lives in src/.
struct WorkoutRuntime {
    Workout workout;
    bool running = false;
    bool paused = false;
    uint32_t startMs = 0;        // clock value when the (current) run began
    uint32_t pausedAccumMs = 0;  // total paused time already folded out of elapsed
    uint32_t pauseStartMs = 0;   // when the current pause began (valid while paused)

    // Replace the workout; resets to a not-started state. Returns false if the workout is empty.
    bool load(const Workout& w) {
        workout = w;
        running = false;
        paused = false;
        startMs = pausedAccumMs = pauseStartMs = 0;
        return !workout.segments.empty();
    }

    void start(uint32_t now) {
        if (workout.segments.empty()) return;
        running = true;
        paused = false;
        startMs = now;
        pausedAccumMs = 0;
    }
    void pause(uint32_t now) {
        if (running && !paused) {
            paused = true;
            pauseStartMs = now;
        }
    }
    void resume(uint32_t now) {
        if (running && paused) {
            paused = false;
            pausedAccumMs += now - pauseStartMs;
        }
    }
    void stop() {
        running = false;
        paused = false;
    }
    // Drop the loaded workout entirely (back to the picker). Stops any run.
    void unload() {
        workout = Workout{};
        running = false;
        paused = false;
    }

    // Seconds of *workout* time elapsed (pauses removed). 0 when not running.
    long elapsedS(uint32_t now) const {
        if (!running) return 0;
        const uint32_t pausedTotal = pausedAccumMs + (paused ? now - pauseStartMs : 0);
        return (long)((now - startMs - pausedTotal) / 1000u);
    }

    // Set the workout clock so that elapsed == targetS right now (used by skip). Clears any pause.
    void seekTo(uint32_t now, long targetS) {
        if (targetS < 0) targetS = 0;
        running = true;
        paused = false;
        pausedAccumMs = 0;
        startMs = now - (uint32_t)targetS * 1000u;
    }

    // Jump to the start of the next segment (or finish if on the last). No-op when not running.
    void skip(uint32_t now) {
        if (!running || workout.segments.empty()) return;
        const WkState s = workoutStateAt(workout, elapsedS(now));
        long acc = 0;
        for (int i = 0; i <= s.segIndex && i < (int)workout.segments.size(); ++i)
            acc += workout.segments[i].durationS;  // cumulative end of the current segment
        seekTo(now, acc);
    }

    WkState state(uint32_t now) const { return workoutStateAt(workout, elapsedS(now)); }
    std::string json(uint32_t now) const {
        return renderWorkoutJson(workout, state(now), running, paused);
    }

    // Dispatch a control verb from the route layer. Unknown verbs are ignored.
    void control(const std::string& action, uint32_t now) {
        if (action == "start") start(now);
        else if (action == "pause") pause(now);
        else if (action == "resume") resume(now);
        else if (action == "skip") skip(now);
        else if (action == "stop") stop();
    }
};

}  // namespace sb20proxy
