#pragma once
// TouchCalRitual — the sequencing half of the tap-the-crosshair calibration.
//
// `TouchCal.h` already had the pure pieces either side of this one: `touchCalFit()` (the
// least-squares math) and `renderTouchCalScreen()` (the crosshair UI). What sat in between
// lived in `main.cpp` as `touchCalTick()`, welded to `lcd.readRaw()`, `Preferences`, `Serial`
// and `millis()` — so the only way to exercise it was to physically tap a screen four times.
//
// That middle piece is where the fiddly logic actually is:
//   * a press is *accumulated* across ticks and averaged, not sampled once (the film is noisy);
//   * edge presses flicker below the pressure gate, so up to `kMaxGapTicks` dropouts are ridden
//     through mid-press rather than ending it;
//   * a press shorter than `kMinPressTicks` is a blip and is discarded, not recorded;
//   * a rejected fit restarts the ritual from point 0 after a brief flash;
//   * the success screen stays up while you keep tapping to verify, and only then times out.
//
// Every one of those is a rule you can get wrong in a way no compiler notices. Here they are
// pure: `tick()` takes a sample and a timestamp and returns what the caller should DO. The
// caller keeps the I/O — reading the film, persisting to NVS, printing, mapping the test tap.
//
// The device is then free to feed it real samples or injected ones (the RAWTAP serial hook),
// and the host tests below feed it synthetic presses, which is the same thing.
#include <cstdint>

#include "TouchCal.h"

namespace sb20proxy {

// What the caller must do after a tick. The ritual never touches hardware itself.
enum class TouchCalAction {
    None,           // nothing to do; keep rendering the current screen
    PointRecorded,  // a crosshair was captured (index in `pointIndex`) — worth logging
    Fitted,         // all points in and the fit is good: apply + persist `fit`
    Rejected,       // all points in but the fit is untrustworthy: tell the user, ritual restarts
    TestTap,        // success screen: map (rawX,rawY) to screen coords and show the marker
    Finished,       // the ritual is over; hand the display back to the normal UI
};

struct TouchCalTickResult {
    TouchCalAction action = TouchCalAction::None;
    // Set whenever a crosshair was captured this tick — including the last one, where `action`
    // is Fitted or Rejected. The caller logs the point, then acts on the action.
    int pointIndex = -1;
    TouchCalFit fit{};        // valid for Fitted
    float rawX = 0, rawY = 0; // the sample: averaged for PointRecorded, live for TestTap
};

class TouchCalRitual {
  public:
    // Tunables, named so a test can state the rule it is checking rather than a magic number.
    static constexpr int kMinPressTicks = 5;   // shorter than this is a blip, not a tap
    static constexpr int kMaxGapTicks = 3;     // dropouts ridden through mid-press
    static constexpr uint32_t kSuccessHoldMs = 6000;  // "tap around to test" window
    static constexpr uint32_t kFailureFlashMs = 2000; // how long "cal failed" shows

    void start() {
        active_ = true;
        idx_ = 0;
        done_ = kCollecting;
        accX_ = accY_ = 0;
        nAcc_ = 0;
        gap_ = 0;
        wasDown_ = false;
        testX_ = testY_ = -1;
    }

    bool active() const { return active_; }
    int step() const { return idx_; }
    int doneState() const { return done_; }  // -1 collecting · 1 saved · 0 failed
    int testX() const { return testX_; }
    int testY() const { return testY_; }

    // The caller maps a TestTap through the new fit and reports where it landed.
    void setTestTap(int x, int y) { testX_ = x; testY_ = y; }

    // One tick of the ritual (the device runs this every ~20 ms in the LCD task).
    TouchCalTickResult tick(bool down, float rawX, float rawY, uint32_t nowMs) {
        TouchCalTickResult r;
        if (!active_) return r;

        if (done_ == kSucceeded) {
            // Keep the screen up while the user is still tapping to verify the fit.
            if (down && !wasDown_) {
                r.action = TouchCalAction::TestTap;
                r.rawX = rawX;
                r.rawY = rawY;
                doneAt_ = nowMs;
            }
            wasDown_ = down;
            if (nowMs - doneAt_ > kSuccessHoldMs) {
                active_ = false;
                done_ = kCollecting;
                if (r.action == TouchCalAction::None) r.action = TouchCalAction::Finished;
            }
            return r;
        }

        if (done_ == kFailed) {
            if (nowMs - doneAt_ > kFailureFlashMs) {
                idx_ = 0;
                done_ = kCollecting;
            }
            wasDown_ = down;
            return r;
        }

        if (down) {
            accX_ += rawX;
            accY_ += rawY;
            ++nAcc_;
            gap_ = 0;
        } else if (nAcc_ > 0 && gap_ < kMaxGapTicks) {
            ++gap_;  // a flicker, not a release
        } else if (nAcc_ >= kMinPressTicks) {
            r = recordPoint(nowMs);
        } else if (nAcc_ > 0) {
            accX_ = accY_ = 0;  // too short to trust
            nAcc_ = 0;
            gap_ = 0;
        }
        wasDown_ = down;
        return r;
    }

  private:
    static constexpr int kCollecting = -1;
    static constexpr int kSucceeded = 1;
    static constexpr int kFailed = 0;

    TouchCalTickResult recordPoint(uint32_t nowMs) {
        TouchCalTickResult r;
        int tx, ty;
        touchCalTarget(idx_, tx, ty);
        pts_[idx_] = {accX_ / (float)nAcc_, accY_ / (float)nAcc_, (float)tx, (float)ty};
        r.action = TouchCalAction::PointRecorded;
        r.pointIndex = idx_;
        r.rawX = pts_[idx_].rawX;  // the averaged press, so the caller can log what it recorded
        r.rawY = pts_[idx_].rawY;

        accX_ = accY_ = 0;
        nAcc_ = 0;
        gap_ = 0;

        if (++idx_ < TOUCH_CAL_POINTS) return r;

        TouchCalFit f = touchCalFit(pts_, TOUCH_CAL_POINTS);
        if (f.valid) {
            r.action = TouchCalAction::Fitted;
            r.fit = f;
            done_ = kSucceeded;
        } else {
            r.action = TouchCalAction::Rejected;
            idx_ = 0;
            done_ = kFailed;
        }
        doneAt_ = nowMs;
        testX_ = testY_ = -1;
        return r;
    }

    bool active_ = false;
    int idx_ = 0;
    int done_ = kCollecting;
    TouchCalPoint pts_[TOUCH_CAL_POINTS] = {};
    float accX_ = 0, accY_ = 0;
    int nAcc_ = 0;
    int gap_ = 0;
    bool wasDown_ = false;
    uint32_t doneAt_ = 0;
    int testX_ = -1, testY_ = -1;
};

}  // namespace sb20proxy
