// Host tests for the nRF bridge's pure read -> correct -> re-frame relay (lib/bridge/SourceRelay.h).
//
// This logic ran inside a Bluefruit notify callback until now, so none of it could be exercised
// without a radio and a live meter. These tests pin the behaviours that actually bite in the field:
// the carried-over crank state across a source swap, the ordering of the single-sided doubling
// against the correction, and the exact bytes each mode emits.
#include <unity.h>

#include <vector>

#include "SourceRelay.h"

using namespace nrfbridge;
using namespace sb20proxy;

void setUp() {}
void tearDown() {}

// ---- helpers ------------------------------------------------------------------------------------

// A standard CPS measurement carrying power + crank revolution data (flags bit 5).
static std::vector<uint8_t> cpsFrame(int16_t watts, uint16_t revs, uint16_t evtTicks) {
    const uint16_t flags = CPM_CRANK_REV_DATA_PRESENT;
    return {
        (uint8_t)(flags & 0xFF),    (uint8_t)(flags >> 8),
        (uint8_t)(watts & 0xFF),    (uint8_t)((watts >> 8) & 0xFF),
        (uint8_t)(revs & 0xFF),     (uint8_t)((revs >> 8) & 0xFF),
        (uint8_t)(evtTicks & 0xFF), (uint8_t)((evtTicks >> 8) & 0xFF),
    };
}

// A power-only CPS measurement (no crank data).
static std::vector<uint8_t> cpsFramePowerOnly(int16_t watts) {
    return {0x00, 0x00, (uint8_t)(watts & 0xFF), (uint8_t)((watts >> 8) & 0xFF)};
}

static RelayOutput feed(SourceRelay& rel, const std::vector<uint8_t>& f, uint32_t t, bool spoof,
                        bool singleSided, const Correction& c) {
    return rel.onFrame(f.data(), (uint16_t)f.size(), t, spoof, singleSided, c);
}

static int16_t leU16(const std::vector<uint8_t>& v, size_t i) {
    return (int16_t)((uint16_t)v[i] | ((uint16_t)v[i + 1] << 8));
}

// ---- cadence derivation -------------------------------------------------------------------------

// The first frame has nothing to difference against, so cadence stays unknown (-1) rather than
// being reported as 0 - a consumer must be able to tell "not known yet" from "coasting".
static void test_first_frame_cadence_unknown() {
    SourceRelay rel;
    Correction c;
    RelayOutput o = feed(rel, cpsFrame(100, 10, 1024), 1000, false, false, c);
    TEST_ASSERT_EQUAL_INT16(-1, o.source.cadence_rpm);
    TEST_ASSERT_EQUAL_INT16(100, o.source.power_w);
}

// 1 revolution in exactly 1024 ticks = 1.0 s = 60 rpm.
static void test_cadence_from_crank_delta() {
    SourceRelay rel;
    Correction c;
    feed(rel, cpsFrame(100, 10, 1024), 1000, false, false, c);
    RelayOutput o = feed(rel, cpsFrame(100, 11, 2048), 2000, false, false, c);
    TEST_ASSERT_EQUAL_INT16(60, o.source.cadence_rpm);
}

// A new sample with no new revolutions means the rider stopped pedalling: 0, not unknown.
static void test_cadence_zero_when_coasting() {
    SourceRelay rel;
    Correction c;
    feed(rel, cpsFrame(100, 10, 1024), 1000, false, false, c);
    RelayOutput o = feed(rel, cpsFrame(0, 10, 2048), 2000, false, false, c);
    TEST_ASSERT_EQUAL_INT16(0, o.source.cadence_rpm);
}

// Both crank counters are uint16 and wrap. Differencing must wrap with them, or the rev delta at
// the wrap point becomes enormous and the derived cadence is garbage.
static void test_cadence_survives_counter_wrap() {
    SourceRelay rel;
    Correction c;
    // revs 65535 -> 0 is a delta of 1; ticks 65024 -> 512 wraps to 1024 = 1.0 s -> 60 rpm.
    feed(rel, cpsFrame(100, 65535, 65024), 1000, false, false, c);
    RelayOutput o = feed(rel, cpsFrame(100, 0, 512), 2000, false, false, c);
    TEST_ASSERT_EQUAL_INT16(60, o.source.cadence_rpm);
}

// A frame with no crank data cannot say anything about cadence.
static void test_power_only_frame_leaves_cadence_unknown() {
    SourceRelay rel;
    Correction c;
    RelayOutput o = feed(rel, cpsFramePowerOnly(250), 1000, false, false, c);
    TEST_ASSERT_EQUAL_INT16(250, o.source.power_w);
    TEST_ASSERT_EQUAL_INT16(-1, o.source.cadence_rpm);
}

// ---- correction ordering ------------------------------------------------------------------------

// The single-sided doubling must happen BEFORE the correction, so scale/offset act on total power.
// With an offset in play the two orderings give different answers, so this pins the one we want.
static void test_single_sided_doubles_before_correction() {
    SourceRelay rel;
    Correction c;
    c.scale = 1.0f;
    c.offset = -10.0f;
    RelayOutput o = feed(rel, cpsFrame(100, 10, 1024), 1000, false, /*singleSided=*/true, c);
    TEST_ASSERT_EQUAL_INT16(200, o.source.power_w);     // doubled pre-correction
    TEST_ASSERT_EQUAL_INT16(190, o.corrected.power_w);  // then offset applied once: 200 - 10
}

static void test_scale_applied_to_output_frame() {
    SourceRelay rel;
    Correction c;
    c.scale = 1.5f;
    RelayOutput o = feed(rel, cpsFrame(100, 10, 1024), 1000, false, false, c);
    TEST_ASSERT_EQUAL_INT16(150, o.corrected.power_w);
    TEST_ASSERT_EQUAL_INT16(150, leU16(o.frame, 2));  // the corrected value is what goes on air
}

// ---- framing ------------------------------------------------------------------------------------

// Corrector mode emits a standard CPS frame and passes the source's crank fields straight through.
static void test_corrector_mode_passes_crank_through() {
    SourceRelay rel;
    Correction c;
    RelayOutput o = feed(rel, cpsFrame(123, 77, 4096), 1000, /*spoof=*/false, false, c);
    TEST_ASSERT_EQUAL_INT16(123, leU16(o.frame, 2));
    TEST_ASSERT_EQUAL_UINT16(77, (uint16_t)leU16(o.frame, 4));
    TEST_ASSERT_EQUAL_UINT16(4096, (uint16_t)leU16(o.frame, 6));
}

// Spoof mode emits the captured Stages 0x2F shape: 11 bytes, flags 0x002F.
static void test_spoof_mode_emits_stages_frame() {
    SourceRelay rel;
    Correction c;
    RelayOutput o = feed(rel, cpsFrame(200, 10, 1024), 1000, /*spoof=*/true, false, c);
    TEST_ASSERT_EQUAL_UINT32(11, (uint32_t)o.frame.size());
    TEST_ASSERT_EQUAL_UINT16(CPM_STAGES_FLAGS, (uint16_t)leU16(o.frame, 0));
    TEST_ASSERT_EQUAL_INT16(200, leU16(o.frame, 2));
    TEST_ASSERT_EQUAL_UINT8(100, o.frame[4]);  // no source split -> 50 % (raw 100)
}

// A source that reports a real L/R split must have it forwarded, not replaced by the 50 % default.
static void test_spoof_forwards_source_balance() {
    SourceRelay rel;
    Correction c;
    // flags bit0 = pedal power balance present; balance byte follows power.
    std::vector<uint8_t> f = {0x01, 0x00, 0xC8, 0x00, 110};  // 200 W, balance 55 %
    RelayOutput o = rel.onFrame(f.data(), (uint16_t)f.size(), 1000, true, false, c);
    TEST_ASSERT_EQUAL_UINT8(110, o.frame[4]);
}

// ---- accumulated torque -------------------------------------------------------------------------

// Torque only accumulates once there is a previous sample to integrate against.
static void test_accum_torque_zero_on_first_frame() {
    SourceRelay rel;
    Correction c;
    RelayOutput o = feed(rel, cpsFrame(200, 10, 1024), 1000, true, false, c);
    TEST_ASSERT_EQUAL_UINT16(0, rel.accumTorque());
    TEST_ASSERT_EQUAL_UINT16(0, (uint16_t)leU16(o.frame, 5));
}

// 200 W at 60 rpm -> T = 200*60/(2*pi*60) = 31.83 Nm -> 31.83*32 = 1018.6 (1/32 Nm units).
static void test_accum_torque_integrates_per_revolution() {
    SourceRelay rel;
    Correction c;
    feed(rel, cpsFrame(200, 10, 1024), 1000, true, false, c);
    feed(rel, cpsFrame(200, 11, 2048), 2000, true, false, c);
    TEST_ASSERT_UINT16_WITHIN(2, 1019, rel.accumTorque());
}

// Torque is integrated from the CORRECTED power, not the raw source power - otherwise the SB20
// would see a torque series that disagrees with the power series in the same frame.
static void test_accum_torque_uses_corrected_power() {
    SourceRelay a, b;
    Correction none, doubled;
    doubled.scale = 2.0f;
    feed(a, cpsFrame(200, 10, 1024), 1000, true, false, none);
    feed(a, cpsFrame(200, 11, 2048), 2000, true, false, none);
    feed(b, cpsFrame(200, 10, 1024), 1000, true, false, doubled);
    feed(b, cpsFrame(200, 11, 2048), 2000, true, false, doubled);
    TEST_ASSERT_UINT16_WITHIN(4, (uint16_t)(a.accumTorque() * 2), b.accumTorque());
}

// Zero power contributes no torque, so a coasting rider does not inflate the accumulator.
static void test_accum_torque_not_advanced_at_zero_power() {
    SourceRelay rel;
    Correction c;
    feed(rel, cpsFrame(0, 10, 1024), 1000, true, false, c);
    feed(rel, cpsFrame(0, 11, 2048), 2000, true, false, c);
    TEST_ASSERT_EQUAL_UINT16(0, rel.accumTorque());
}

// ---- reset on source disconnect -----------------------------------------------------------------

// THE field failure this guards: a new meter starts its cumulative-rev counter wherever it likes.
// Without a reset, the first frame from meter B is differenced against meter A's last value and
// injects a huge bogus revolution delta - and so a huge bogus torque accumulation.
static void test_reset_prevents_bogus_delta_across_source_swap() {
    SourceRelay rel;
    Correction c;
    feed(rel, cpsFrame(200, 10, 1024), 1000, true, false, c);
    feed(rel, cpsFrame(200, 11, 2048), 2000, true, false, c);
    const uint16_t afterA = rel.accumTorque();
    TEST_ASSERT_TRUE(afterA > 0);

    rel.reset();

    // Meter B happens to start at rev 9000 - a ~9000-rev delta if the baseline had survived.
    RelayOutput o = feed(rel, cpsFrame(200, 9000, 30000), 3000, true, false, c);
    TEST_ASSERT_EQUAL_INT16(-1, o.source.cadence_rpm);  // no baseline yet, so unknown
    TEST_ASSERT_EQUAL_UINT16(afterA, rel.accumTorque());  // and nothing bogus accumulated
}

// reset() must NOT zero the accumulator. Accumulated torque is a free-running cumulative field
// that consumers difference; zeroing it mid-stream is a discontinuity read as a huge wrapping
// delta. Only the baselines we difference against go stale on a source swap. This pins the
// pre-extraction behaviour, which reset the have-prev flags but left the accumulator alone.
static void test_reset_preserves_accumulated_torque() {
    SourceRelay rel;
    Correction c;
    feed(rel, cpsFrame(200, 10, 1024), 1000, true, false, c);
    feed(rel, cpsFrame(200, 11, 2048), 2000, true, false, c);
    const uint16_t before = rel.accumTorque();
    TEST_ASSERT_TRUE(before > 0);
    rel.reset();
    TEST_ASSERT_EQUAL_UINT16(before, rel.accumTorque());

    // ...and it keeps climbing from there once the new source establishes a baseline.
    feed(rel, cpsFrame(200, 9000, 30000), 3000, true, false, c);
    feed(rel, cpsFrame(200, 9001, 31024), 4000, true, false, c);
    TEST_ASSERT_TRUE(rel.accumTorque() > before);
}

// Reset must clear the cadence baseline too, not just the torque state.
static void test_reset_clears_cadence_baseline() {
    SourceRelay rel;
    Correction c;
    feed(rel, cpsFrame(100, 10, 1024), 1000, false, false, c);
    RelayOutput before = feed(rel, cpsFrame(100, 11, 2048), 2000, false, false, c);
    TEST_ASSERT_EQUAL_INT16(60, before.source.cadence_rpm);
    rel.reset();
    RelayOutput after = feed(rel, cpsFrame(100, 12, 3072), 3000, false, false, c);
    TEST_ASSERT_EQUAL_INT16(-1, after.source.cadence_rpm);
}

// ---- timestamp ----------------------------------------------------------------------------------

static void test_timestamp_is_passed_through() {
    SourceRelay rel;
    Correction c;
    RelayOutput o = feed(rel, cpsFrame(100, 10, 1024), 123456, false, false, c);
    TEST_ASSERT_EQUAL_UINT32(123456, o.source.t_ms);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_first_frame_cadence_unknown);
    RUN_TEST(test_cadence_from_crank_delta);
    RUN_TEST(test_cadence_zero_when_coasting);
    RUN_TEST(test_cadence_survives_counter_wrap);
    RUN_TEST(test_power_only_frame_leaves_cadence_unknown);
    RUN_TEST(test_single_sided_doubles_before_correction);
    RUN_TEST(test_scale_applied_to_output_frame);
    RUN_TEST(test_corrector_mode_passes_crank_through);
    RUN_TEST(test_spoof_mode_emits_stages_frame);
    RUN_TEST(test_spoof_forwards_source_balance);
    RUN_TEST(test_accum_torque_zero_on_first_frame);
    RUN_TEST(test_accum_torque_integrates_per_revolution);
    RUN_TEST(test_accum_torque_uses_corrected_power);
    RUN_TEST(test_accum_torque_not_advanced_at_zero_power);
    RUN_TEST(test_reset_prevents_bogus_delta_across_source_swap);
    RUN_TEST(test_reset_preserves_accumulated_torque);
    RUN_TEST(test_reset_clears_cadence_baseline);
    RUN_TEST(test_timestamp_is_passed_through);
    return UNITY_END();
}
