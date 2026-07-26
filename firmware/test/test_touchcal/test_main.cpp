// Host tests for TouchCalRitual — the sequencing half of the touch-calibration ritual.
//
// Until this suite existed, the only way to exercise any of this was to physically tap a CYD
// four times and watch what happened. The rules it encodes are exactly the kind that rot
// silently: how long a press has to last to count, how much dropout is a flicker rather than a
// release, what a rejected fit does next, when the success screen goes away.
//
// Every test here drives the ritual with synthetic samples, which is precisely what the RAWTAP
// serial hook does on the device — so passing here means the same path the hardware runs.
#include <unity.h>

#include "TouchCalRitual.h"

using namespace sb20proxy;

void setUp() {}
void tearDown() {}

namespace {

// The film's raw corners, spread wide enough for the least-squares fit to trust them.
constexpr float kRawLo = 300.0f;
constexpr float kRawHi = 3700.0f;

struct Harness {
    TouchCalRitual ritual;
    uint32_t nowMs = 1000;
    TouchCalTickResult last;

    void tick(bool down, float rx = 0, float ry = 0) {
        last = ritual.tick(down, rx, ry, nowMs);
        nowMs += 20;  // the LCD task's period
    }

    // Hold, then release. Release needs kMaxGapTicks+1 idle ticks: the first few are ridden
    // through as flicker, and only the one after that ends the press.
    TouchCalTickResult press(float rx, float ry, int heldTicks = TouchCalRitual::kMinPressTicks) {
        for (int i = 0; i < heldTicks; ++i) tick(true, rx, ry);
        TouchCalTickResult out;
        for (int i = 0; i <= TouchCalRitual::kMaxGapTicks; ++i) {
            tick(false);
            if (last.action != TouchCalAction::None) out = last;
        }
        return out;
    }

    // The raw sample for crosshair `idx`, matching touchCalTarget()'s corner order.
    void rawForTarget(int idx, float& rx, float& ry) const {
        rx = (idx & 1) ? kRawHi : kRawLo;
        ry = (idx & 2) ? kRawHi : kRawLo;
    }

    TouchCalTickResult tapTarget(int idx, int heldTicks = TouchCalRitual::kMinPressTicks) {
        float rx, ry;
        rawForTarget(idx, rx, ry);
        return press(rx, ry, heldTicks);
    }
};

void test_inactive_ritual_ignores_samples() {
    Harness h;
    h.tick(true, kRawLo, kRawLo);
    TEST_ASSERT_FALSE(h.ritual.active());
    TEST_ASSERT_EQUAL(static_cast<int>(TouchCalAction::None), static_cast<int>(h.last.action));
}

void test_four_clean_taps_produce_a_valid_fit() {
    Harness h;
    h.ritual.start();
    TEST_ASSERT_TRUE(h.ritual.active());

    for (int i = 0; i < TOUCH_CAL_POINTS - 1; ++i) {
        TouchCalTickResult r = h.tapTarget(i);
        TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(TouchCalAction::PointRecorded),
                                  static_cast<int>(r.action), "each tap records one crosshair");
        TEST_ASSERT_EQUAL(i, r.pointIndex);
        TEST_ASSERT_EQUAL(i + 1, h.ritual.step());
    }

    TouchCalTickResult r = h.tapTarget(TOUCH_CAL_POINTS - 1);
    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(TouchCalAction::Fitted), static_cast<int>(r.action),
                              "the last tap completes the fit");
    TEST_ASSERT_TRUE(r.fit.valid);

    // The fit must map the tapped raw values back onto the crosshairs it asked for.
    for (int i = 0; i < TOUCH_CAL_POINTS; ++i) {
        float rx, ry;
        h.rawForTarget(i, rx, ry);
        int x = 0, y = 0;
        touchCalApply(r.fit, rx, ry, x, y);
        int tx = 0, ty = 0;
        touchCalTarget(i, tx, ty);
        TEST_ASSERT_INT_WITHIN_MESSAGE(2, tx, x, "fit maps raw back to its own target (x)");
        TEST_ASSERT_INT_WITHIN_MESSAGE(2, ty, y, "fit maps raw back to its own target (y)");
    }
}

void test_a_press_is_averaged_not_sampled_once() {
    // A noisy press either side of a centre value must land on the centre, not on the last tick.
    Harness h;
    h.ritual.start();
    for (int i = 0; i < 3; ++i) h.tick(true, kRawLo - 100.0f, kRawLo - 100.0f);
    for (int i = 0; i < 3; ++i) h.tick(true, kRawLo + 100.0f, kRawLo + 100.0f);
    for (int i = 0; i <= TouchCalRitual::kMaxGapTicks; ++i) h.tick(false);

    TEST_ASSERT_EQUAL_MESSAGE(1, h.ritual.step(), "the noisy press still recorded one point");

    // Finish with clean taps; if the first point had taken the last raw sample instead of the
    // mean, the fit would be skewed. Check it still maps target 0 to its crosshair.
    for (int i = 1; i < TOUCH_CAL_POINTS; ++i) h.tapTarget(i);
    TEST_ASSERT_EQUAL(static_cast<int>(TouchCalAction::Fitted), static_cast<int>(h.last.action));
    int x = 0, y = 0;
    touchCalApply(h.last.fit, kRawLo, kRawLo, x, y);
    int tx = 0, ty = 0;
    touchCalTarget(0, tx, ty);
    TEST_ASSERT_INT_WITHIN_MESSAGE(3, tx, x, "averaged press centres on the crosshair");
}

void test_short_dropout_mid_press_is_ridden_through() {
    // Edge presses flicker below the pressure gate. A gap inside the allowance is not a release.
    Harness h;
    h.ritual.start();
    h.tick(true, kRawLo, kRawLo);
    h.tick(true, kRawLo, kRawLo);
    for (int i = 0; i < TouchCalRitual::kMaxGapTicks; ++i) h.tick(false);
    TEST_ASSERT_EQUAL_MESSAGE(0, h.ritual.step(), "the flicker did not end the press");

    // Resume and hold long enough to qualify, then release for real.
    for (int i = 0; i < TouchCalRitual::kMinPressTicks; ++i) h.tick(true, kRawLo, kRawLo);
    for (int i = 0; i <= TouchCalRitual::kMaxGapTicks; ++i) h.tick(false);
    TEST_ASSERT_EQUAL_MESSAGE(1, h.ritual.step(), "the resumed press recorded as one tap");
}

void test_a_blip_shorter_than_the_minimum_is_discarded() {
    Harness h;
    h.ritual.start();
    for (int i = 0; i < TouchCalRitual::kMinPressTicks - 1; ++i) h.tick(true, kRawLo, kRawLo);
    for (int i = 0; i <= TouchCalRitual::kMaxGapTicks; ++i) h.tick(false);

    TEST_ASSERT_EQUAL_MESSAGE(0, h.ritual.step(), "a blip must not record a crosshair");
    TEST_ASSERT_EQUAL(static_cast<int>(TouchCalAction::None), static_cast<int>(h.last.action));
}

void test_clustered_taps_are_rejected_and_the_ritual_restarts() {
    Harness h;
    h.ritual.start();
    TouchCalTickResult r;
    for (int i = 0; i < TOUCH_CAL_POINTS; ++i) r = h.press(2000.0f, 2000.0f);  // all one spot

    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(TouchCalAction::Rejected), static_cast<int>(r.action),
                              "a fit with no spread must be refused, not saved");
    TEST_ASSERT_FALSE(r.fit.valid);
    TEST_ASSERT_EQUAL_MESSAGE(0, h.ritual.doneState(), "the failure screen is showing");
    TEST_ASSERT_EQUAL_MESSAGE(0, h.ritual.step(), "and it restarts from the first crosshair");
    TEST_ASSERT_TRUE_MESSAGE(h.ritual.active(), "a rejected fit retries rather than giving up");

    h.nowMs += TouchCalRitual::kFailureFlashMs + 1;
    h.tick(false);
    TEST_ASSERT_EQUAL_MESSAGE(-1, h.ritual.doneState(), "the flash clears back to collecting");
    TEST_ASSERT_TRUE(h.ritual.active());
}

void test_success_screen_reports_verification_taps() {
    Harness h;
    h.ritual.start();
    for (int i = 0; i < TOUCH_CAL_POINTS; ++i) h.tapTarget(i);
    TEST_ASSERT_EQUAL(1, h.ritual.doneState());

    h.tick(true, kRawHi, kRawLo);
    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(TouchCalAction::TestTap),
                              static_cast<int>(h.last.action),
                              "a tap on the success screen is handed back to be mapped");
    TEST_ASSERT_EQUAL_FLOAT(kRawHi, h.last.rawX);
    TEST_ASSERT_EQUAL_FLOAT(kRawLo, h.last.rawY);

    h.ritual.setTestTap(42, 99);
    TEST_ASSERT_EQUAL(42, h.ritual.testX());
    TEST_ASSERT_EQUAL(99, h.ritual.testY());
}

void test_verification_taps_keep_the_success_screen_alive() {
    Harness h;
    h.ritual.start();
    for (int i = 0; i < TOUCH_CAL_POINTS; ++i) h.tapTarget(i);

    // Just short of the timeout, tap again: the window must restart, not expire.
    h.nowMs += TouchCalRitual::kSuccessHoldMs - 100;
    h.tick(true, kRawLo, kRawHi);
    TEST_ASSERT_EQUAL(static_cast<int>(TouchCalAction::TestTap), static_cast<int>(h.last.action));

    h.nowMs += TouchCalRitual::kSuccessHoldMs - 100;
    h.tick(false);
    TEST_ASSERT_TRUE_MESSAGE(h.ritual.active(), "tapping keeps the verification screen up");
}

void test_success_screen_finishes_after_the_hold_window() {
    Harness h;
    h.ritual.start();
    for (int i = 0; i < TOUCH_CAL_POINTS; ++i) h.tapTarget(i);
    TEST_ASSERT_TRUE(h.ritual.active());

    h.nowMs += TouchCalRitual::kSuccessHoldMs + 1;
    h.tick(false);

    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(TouchCalAction::Finished),
                              static_cast<int>(h.last.action),
                              "the caller is told to hand the display back");
    TEST_ASSERT_FALSE_MESSAGE(h.ritual.active(), "and the ritual is over");
}

void test_restarting_clears_previous_progress() {
    Harness h;
    h.ritual.start();
    h.tapTarget(0);
    h.tapTarget(1);
    TEST_ASSERT_EQUAL(2, h.ritual.step());

    h.ritual.start();
    TEST_ASSERT_EQUAL_MESSAGE(0, h.ritual.step(), "CALTOUCH restarts from the first crosshair");
    TEST_ASSERT_EQUAL(-1, h.ritual.doneState());
    TEST_ASSERT_EQUAL(-1, h.ritual.testX());
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_inactive_ritual_ignores_samples);
    RUN_TEST(test_four_clean_taps_produce_a_valid_fit);
    RUN_TEST(test_a_press_is_averaged_not_sampled_once);
    RUN_TEST(test_short_dropout_mid_press_is_ridden_through);
    RUN_TEST(test_a_blip_shorter_than_the_minimum_is_discarded);
    RUN_TEST(test_clustered_taps_are_rejected_and_the_ritual_restarts);
    RUN_TEST(test_success_screen_reports_verification_taps);
    RUN_TEST(test_verification_taps_keep_the_success_screen_alive);
    RUN_TEST(test_success_screen_finishes_after_the_hold_window);
    RUN_TEST(test_restarting_clears_previous_progress);
    return UNITY_END();
}
