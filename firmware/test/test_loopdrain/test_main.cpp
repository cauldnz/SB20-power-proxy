// Host tests for the loop-drain handoff and the control-point apply order (item 5 / R10).
//
// These cover the chain that runs BETWEEN the pure codecs and the radio -- the part that used to
// exist only as adjacent statements inside NimBLE callbacks and bare `volatile` globals in
// firmware/src/main.cpp, where nothing could observe it. The codecs at each end of the chain are
// already tested (test_proxy); what is tested here is the wiring:
//
//   captured CP bytes -> handleControlPoint -> applyCpResult -> [reply, then source zero]
//                                                                        |
//                                                        PendingFlag.publish() (BLE task)
//                                                                        |
//                                                        loop(): take() -> source meter zero
//
// No radio, no RTOS: the seam is driven through ICpSink and the slots, so the whole chain runs on
// the host. The control-point vectors are the same real captured bytes used in test_proxy.

#include <unity.h>

#include <string>
#include <vector>

#include "CpApply.h"
#include "Cps.h"
#include "LoopDrain.h"

using namespace sb20proxy;

void setUp() {}
void tearDown() {}

// --- fakes -----------------------------------------------------------------------------------

// Records WHAT happened and, critically, IN WHAT ORDER.
class RecordingCpSink : public ICpSink {
 public:
    std::vector<std::string> order;
    std::vector<uint8_t> lastReply;
    uint16_t crankLen = 0;
    int zeros = 0;

    void setCrankLength(uint16_t halfMm) override {
        crankLen = halfMm;
        order.push_back("crank");
    }
    void reply(const uint8_t* data, size_t len) override {
        lastReply.assign(data, data + len);
        order.push_back("reply");
    }
    void requestSourceZero() override {
        ++zeros;
        order.push_back("zero");
    }

    int indexOf(const char* what) const {
        for (size_t i = 0; i < order.size(); ++i)
            if (order[i] == what) return (int)i;
        return -1;
    }
};

// Stands in for CalibrationSession: records the samples and the order they arrived in.
class RecordingCalSink {
 public:
    std::vector<std::string> order;
    std::vector<float> refPowers, dutPowers;
    std::vector<uint32_t> refTimes, dutTimes;

    void onRef(float p, uint32_t t) {
        refPowers.push_back(p);
        refTimes.push_back(t);
        order.push_back("ref");
    }
    void onDut(float p, uint32_t t) {
        dutPowers.push_back(p);
        dutTimes.push_back(t);
        order.push_back("dut");
    }
};

// --- the control-point apply order (the reason-531 invariant) ---------------------------------

void test_zero_reset_replies_before_forwarding_the_zero() {
    // THE load-bearing test of this suite. The SB20 terminates the link (reason 531) if a CP write
    // goes unanswered, and the Assioma's zero takes ~3.6 s -- so the reply MUST be indicated before
    // we touch the source meter. Real captured op: Enhanced Offset Compensation (0x10), the op the
    // Stages app actually sends.
    const uint8_t req[] = {CP_OP_ENHANCED_OFFSET_COMP};
    const std::vector<uint8_t> mfgData = {0x04, 0x85, 0x03, 0xB7, 0x03};
    CpResult r = handleControlPoint(req, sizeof(req), 345, /*offset*/ 0, /*mfg*/ 0x01BA, mfgData);

    RecordingCpSink sink;
    applyCpResult(r, sink);

    TEST_ASSERT_EQUAL_INT(1, sink.zeros);
    TEST_ASSERT_TRUE(sink.indexOf("reply") >= 0);
    TEST_ASSERT_TRUE(sink.indexOf("zero") >= 0);
    TEST_ASSERT_TRUE(sink.indexOf("reply") < sink.indexOf("zero"));

    // and the client got the real crank's byte-exact reply, not a truncated one
    const uint8_t expected[] = {0x20, 0x10, 0x01, 0x00, 0x00, 0xBA, 0x01, 0x04, 0x85, 0x03, 0xB7, 0x03};
    TEST_ASSERT_EQUAL_INT(12, (int)sink.lastReply.size());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, sink.lastReply.data(), 12);
}

void test_basic_offset_comp_0x0C_also_replies_before_zeroing() {
    const uint8_t req[] = {CP_OP_START_OFFSET_COMP};
    CpResult r = handleControlPoint(req, sizeof(req), 345, 903);
    RecordingCpSink sink;
    applyCpResult(r, sink);
    TEST_ASSERT_EQUAL_INT(1, sink.zeros);
    TEST_ASSERT_TRUE(sink.indexOf("reply") < sink.indexOf("zero"));
}

void test_crank_length_ops_never_zero_the_source() {
    // A crank-length write must not send the rider's meter into a 3.6 s zero.
    const uint8_t set[] = {CP_OP_SET_CRANK_LENGTH, 0x59, 0x01};  // 0x0159 = 345
    RecordingCpSink s1;
    applyCpResult(handleControlPoint(set, sizeof(set), 330, 0), s1);
    TEST_ASSERT_EQUAL_INT(0, s1.zeros);
    TEST_ASSERT_EQUAL_UINT16(345, s1.crankLen);
    // state is persisted BEFORE the reply, so a client that immediately reads back sees the new value
    TEST_ASSERT_TRUE(s1.indexOf("crank") < s1.indexOf("reply"));

    const uint8_t get[] = {CP_OP_REQUEST_CRANK_LENGTH};
    RecordingCpSink s2;
    applyCpResult(handleControlPoint(get, sizeof(get), 345, 0), s2);
    TEST_ASSERT_EQUAL_INT(0, s2.zeros);
    TEST_ASSERT_EQUAL_INT(-1, s2.indexOf("crank"));  // unchanged -> not persisted
}

void test_every_control_point_write_gets_a_reply() {
    // The seam must answer EVERY write, including ops we do not implement -- an unanswered write is
    // what disconnects the SB20. Walk the real ops plus an unsupported one and a bogus one.
    const uint8_t ops[] = {CP_OP_SET_CRANK_LENGTH, CP_OP_REQUEST_CRANK_LENGTH,
                           CP_OP_START_OFFSET_COMP, CP_OP_ENHANCED_OFFSET_COMP,
                           0x01 /*set cumulative*/, 0x7F /*not a real op*/};
    for (uint8_t op : ops) {
        const uint8_t req[] = {op, 0x59, 0x01};
        RecordingCpSink sink;
        applyCpResult(handleControlPoint(req, sizeof(req), 345, 0), sink);
        TEST_ASSERT_TRUE(sink.indexOf("reply") >= 0);
        TEST_ASSERT_TRUE(sink.lastReply.size() >= 3);
        TEST_ASSERT_EQUAL_HEX8(0x20, sink.lastReply[0]);  // response opcode
    }
}

// --- the pending-slot handoff -----------------------------------------------------------------

void test_flag_is_not_pending_until_published() {
    PendingFlag f;
    TEST_ASSERT_FALSE(f.pending());
    TEST_ASSERT_FALSE(f.take());
    f.publish();
    TEST_ASSERT_TRUE(f.pending());
    TEST_ASSERT_TRUE(f.take());
    TEST_ASSERT_FALSE(f.take());  // drained exactly once
}

void test_flag_coalesces_repeat_publishes() {
    // The SB20/app can write 0x10 several times before loop() next runs; that must forward ONE zero,
    // not one per write -- each zero stalls the source meter for ~3.6 s.
    PendingFlag f;
    f.publish();
    f.publish();
    f.publish();
    TEST_ASSERT_TRUE(f.take());
    TEST_ASSERT_FALSE(f.take());
}

void test_flag_republished_during_drain_is_not_lost() {
    // Consumer takes, then a callback publishes again before the next loop -> still delivered.
    PendingFlag f;
    f.publish();
    TEST_ASSERT_TRUE(f.take());
    f.publish();
    TEST_ASSERT_TRUE(f.take());
}

void test_slot_carries_the_published_payload() {
    PendingSlot<CalSample> s;
    CalSample out;
    TEST_ASSERT_FALSE(s.take(out));
    s.publish(CalSample{212.5f, 1234});
    TEST_ASSERT_TRUE(s.take(out));
    TEST_ASSERT_EQUAL_FLOAT(212.5f, out.power_w);
    TEST_ASSERT_EQUAL_UINT32(1234, out.t_ms);
    TEST_ASSERT_FALSE(s.take(out));
}

void test_slot_keeps_only_the_latest_value() {
    // Deliberate coalescing: these are periodic samples, freshest wins. Documented invariant 2.
    PendingSlot<CalSample> s;
    s.publish(CalSample{100.0f, 1});
    s.publish(CalSample{200.0f, 2});
    CalSample out;
    TEST_ASSERT_TRUE(s.take(out));
    TEST_ASSERT_EQUAL_FLOAT(200.0f, out.power_w);
    TEST_ASSERT_EQUAL_UINT32(2, out.t_ms);
    TEST_ASSERT_FALSE(s.take(out));
}

void test_clear_drops_a_pending_value_without_delivering_it() {
    PendingSlot<CalSample> s;
    s.publish(CalSample{100.0f, 1});
    s.clear();
    CalSample out;
    TEST_ASSERT_FALSE(s.take(out));
}

// --- the calibration drain order --------------------------------------------------------------

void test_drain_applies_reference_before_dut() {
    // The accumulator pairs a DUT sample against the most recent reference, so the reference must
    // land first. main.cpp documented this in a comment with nothing enforcing it -- this is the
    // enforcement. Publish DUT FIRST to prove the order comes from the drain, not the publish.
    CalibrationDrain d;
    d.publishDut(250.0f, 20);
    d.publishRef(240.0f, 10);

    RecordingCalSink sink;
    TEST_ASSERT_EQUAL_INT(2, d.drain(sink));
    TEST_ASSERT_EQUAL_INT(2, (int)sink.order.size());
    TEST_ASSERT_EQUAL_STRING("ref", sink.order[0].c_str());
    TEST_ASSERT_EQUAL_STRING("dut", sink.order[1].c_str());
    TEST_ASSERT_EQUAL_FLOAT(240.0f, sink.refPowers[0]);
    TEST_ASSERT_EQUAL_FLOAT(250.0f, sink.dutPowers[0]);
    TEST_ASSERT_EQUAL_UINT32(10, sink.refTimes[0]);
    TEST_ASSERT_EQUAL_UINT32(20, sink.dutTimes[0]);
}

void test_drain_is_a_noop_when_nothing_pending() {
    CalibrationDrain d;
    RecordingCalSink sink;
    TEST_ASSERT_EQUAL_INT(0, d.drain(sink));
    TEST_ASSERT_EQUAL_INT(0, (int)sink.order.size());
}

void test_drain_delivers_one_side_when_only_one_meter_is_streaming() {
    // Normal case early in a calibration boot: the reference has connected, the DUT has not.
    CalibrationDrain d;
    d.publishRef(240.0f, 10);
    RecordingCalSink sink;
    TEST_ASSERT_EQUAL_INT(1, d.drain(sink));
    TEST_ASSERT_EQUAL_STRING("ref", sink.order[0].c_str());
    TEST_ASSERT_EQUAL_INT(0, (int)sink.dutPowers.size());
}

void test_repeated_drains_deliver_each_sample_once() {
    // The defect the read-then-clear order exists to avoid: a sample delivered twice biases the fit.
    CalibrationDrain d;
    RecordingCalSink sink;
    for (int i = 0; i < 5; ++i) {
        d.publishRef(200.0f + i, (uint32_t)(i * 100));
        d.publishDut(210.0f + i, (uint32_t)(i * 100));
        d.drain(sink);
        d.drain(sink);  // a second drain with nothing new must add nothing
    }
    TEST_ASSERT_EQUAL_INT(5, (int)sink.refPowers.size());
    TEST_ASSERT_EQUAL_INT(5, (int)sink.dutPowers.size());
    TEST_ASSERT_EQUAL_INT(10, (int)sink.order.size());
    TEST_ASSERT_EQUAL_FLOAT(204.0f, sink.refPowers[4]);
    TEST_ASSERT_EQUAL_FLOAT(214.0f, sink.dutPowers[4]);
}

void test_drain_clear_drops_inflight_samples() {
    // A meter dropped mid-session: whatever was stashed is stale, not a sample to accumulate.
    CalibrationDrain d;
    d.publishRef(240.0f, 10);
    d.publishDut(250.0f, 20);
    TEST_ASSERT_TRUE(d.refPending());
    TEST_ASSERT_TRUE(d.dutPending());
    d.clear();
    RecordingCalSink sink;
    TEST_ASSERT_EQUAL_INT(0, d.drain(sink));
}

// A sink that publishes a fresh sample while the drain is calling it -- the deterministic stand-in
// for a notification landing mid-drain. Closest a single-threaded host test can get to the real race.
class RepublishingCalSink {
 public:
    CalibrationDrain* drain = nullptr;
    int refCalls = 0, dutCalls = 0;
    bool armed = true;
    std::vector<float> refPowers;

    void onRef(float p, uint32_t) {
        ++refCalls;
        refPowers.push_back(p);
        if (armed) {  // a new notification arrives while we are inside the drain
            armed = false;
            drain->publishRef(999.0f, 42);
        }
    }
    void onDut(float, uint32_t) { ++dutCalls; }
};

void test_sample_published_during_a_drain_is_delivered_on_the_next_drain() {
    // Must be delivered exactly once, on the FOLLOWING drain -- not swallowed by the take() that
    // was in flight, and not re-delivered forever.
    CalibrationDrain d;
    RepublishingCalSink sink;
    sink.drain = &d;

    d.publishRef(240.0f, 10);
    TEST_ASSERT_EQUAL_INT(1, d.drain(sink));
    TEST_ASSERT_EQUAL_INT(1, sink.refCalls);
    TEST_ASSERT_EQUAL_FLOAT(240.0f, sink.refPowers[0]);

    TEST_ASSERT_EQUAL_INT(1, d.drain(sink));  // the in-flight sample lands here
    TEST_ASSERT_EQUAL_INT(2, sink.refCalls);
    TEST_ASSERT_EQUAL_FLOAT(999.0f, sink.refPowers[1]);

    TEST_ASSERT_EQUAL_INT(0, d.drain(sink));  // and is not delivered a third time
    TEST_ASSERT_EQUAL_INT(2, sink.refCalls);
}

// --- the disconnect edge ----------------------------------------------------------------------
void test_falling_edge_fires_once_when_the_meter_drops() {
    // Drives proxy.reset() so the OLED and /stats stop showing a stale power_w next to "searching".
    FallingEdge e;
    TEST_ASSERT_FALSE(e.update(true));   // connected
    TEST_ASSERT_FALSE(e.update(true));   // still connected
    TEST_ASSERT_TRUE(e.update(false));   // dropped -> fire
    TEST_ASSERT_FALSE(e.update(false));  // stays down -> must NOT keep firing
    TEST_ASSERT_FALSE(e.update(false));
}

void test_falling_edge_does_not_fire_on_a_boot_that_starts_disconnected() {
    // Every boot begins disconnected; firing here would reset the proxy before it ever had a reading.
    FallingEdge e;
    TEST_ASSERT_FALSE(e.update(false));
    TEST_ASSERT_FALSE(e.update(false));
}

void test_falling_edge_rearms_after_reconnect() {
    FallingEdge e;
    e.update(true);
    TEST_ASSERT_TRUE(e.update(false));
    e.update(true);  // reconnected
    TEST_ASSERT_TRUE(e.update(false));
}

// --- the whole chain, end to end ---------------------------------------------------------------

// Stands in for BleMeterClient (the central) -- the far end of the zero-reset chain.
class FakeSourceMeter {
 public:
    int zeroRequests = 0;
    bool requestZeroOffset() {
        ++zeroRequests;
        return true;
    }
};

// Stands in for BleCrankPeripheral: decodes the write, applies the result in order, and -- exactly
// like the real seam -- only FLAGS the zero for loop() rather than calling the central re-entrantly.
class FakeCrankPeripheral : public ICpSink {
 public:
    FakeCrankPeripheral(PendingFlag& zeroFlag, uint16_t crankLen)
        : zeroFlag_(zeroFlag), crankLen_(crankLen) {}

    void onWrite(const uint8_t* data, size_t len) {
        applyCpResult(handleControlPoint(data, len, crankLen_, 0), *this);
    }

    void setCrankLength(uint16_t halfMm) override { crankLen_ = halfMm; }
    void reply(const uint8_t* d, size_t n) override { replies.push_back(std::vector<uint8_t>(d, d + n)); }
    void requestSourceZero() override { zeroFlag_.publish(); }

    std::vector<std::vector<uint8_t>> replies;
    uint16_t crankLen() const { return crankLen_; }

 private:
    PendingFlag& zeroFlag_;
    uint16_t crankLen_;
};

// The loop() drain, as main.cpp performs it.
static void runLoopOnce(PendingFlag& zeroFlag, FakeSourceMeter& meter) {
    if (zeroFlag.take()) meter.requestZeroOffset();
}

void test_app_calibrate_forwards_exactly_one_zero_to_the_source_meter() {
    // The full field path: the Stages app taps Calibrate -> 0x10 write -> we answer the SB20 -> the
    // real Assioma is zeroed once, from loop(), never from the BLE callback.
    PendingFlag zeroFlag;
    FakeSourceMeter meter;
    FakeCrankPeripheral crank(zeroFlag, 345);

    const uint8_t req[] = {CP_OP_ENHANCED_OFFSET_COMP};
    crank.onWrite(req, sizeof(req));

    // answered immediately, and the source has NOT been touched from the callback context
    TEST_ASSERT_EQUAL_INT(1, (int)crank.replies.size());
    TEST_ASSERT_EQUAL_INT(0, meter.zeroRequests);

    runLoopOnce(zeroFlag, meter);
    TEST_ASSERT_EQUAL_INT(1, meter.zeroRequests);

    runLoopOnce(zeroFlag, meter);  // no repeat on subsequent loops
    TEST_ASSERT_EQUAL_INT(1, meter.zeroRequests);
}

void test_impatient_double_tap_still_zeroes_the_source_only_once() {
    // Two CP writes landing in the same loop tick: both MUST be answered (or the SB20 drops), but the
    // source meter must be zeroed once -- each zero stalls it for ~3.6 s.
    PendingFlag zeroFlag;
    FakeSourceMeter meter;
    FakeCrankPeripheral crank(zeroFlag, 345);

    const uint8_t req[] = {CP_OP_ENHANCED_OFFSET_COMP};
    crank.onWrite(req, sizeof(req));
    crank.onWrite(req, sizeof(req));

    TEST_ASSERT_EQUAL_INT(2, (int)crank.replies.size());
    runLoopOnce(zeroFlag, meter);
    TEST_ASSERT_EQUAL_INT(1, meter.zeroRequests);
}

void test_crank_length_write_answers_without_disturbing_the_source() {
    PendingFlag zeroFlag;
    FakeSourceMeter meter;
    FakeCrankPeripheral crank(zeroFlag, 330);

    const uint8_t req[] = {CP_OP_SET_CRANK_LENGTH, 0x59, 0x01};  // 345 = 172.5 mm
    crank.onWrite(req, sizeof(req));
    runLoopOnce(zeroFlag, meter);

    TEST_ASSERT_EQUAL_INT(1, (int)crank.replies.size());
    TEST_ASSERT_EQUAL_UINT16(345, crank.crankLen());
    TEST_ASSERT_EQUAL_INT(0, meter.zeroRequests);

    // and the new length is what a subsequent read-back reports
    const uint8_t get[] = {CP_OP_REQUEST_CRANK_LENGTH};
    crank.onWrite(get, sizeof(get));
    const std::vector<uint8_t>& rb = crank.replies.back();
    const uint8_t expected[] = {0x20, 0x05, 0x59, 0x01};
    TEST_ASSERT_EQUAL_INT(4, (int)rb.size());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, rb.data(), 4);
}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_zero_reset_replies_before_forwarding_the_zero);
    RUN_TEST(test_basic_offset_comp_0x0C_also_replies_before_zeroing);
    RUN_TEST(test_crank_length_ops_never_zero_the_source);
    RUN_TEST(test_every_control_point_write_gets_a_reply);

    RUN_TEST(test_flag_is_not_pending_until_published);
    RUN_TEST(test_flag_coalesces_repeat_publishes);
    RUN_TEST(test_flag_republished_during_drain_is_not_lost);
    RUN_TEST(test_slot_carries_the_published_payload);
    RUN_TEST(test_slot_keeps_only_the_latest_value);
    RUN_TEST(test_clear_drops_a_pending_value_without_delivering_it);

    RUN_TEST(test_drain_applies_reference_before_dut);
    RUN_TEST(test_drain_is_a_noop_when_nothing_pending);
    RUN_TEST(test_drain_delivers_one_side_when_only_one_meter_is_streaming);
    RUN_TEST(test_repeated_drains_deliver_each_sample_once);
    RUN_TEST(test_drain_clear_drops_inflight_samples);
    RUN_TEST(test_sample_published_during_a_drain_is_delivered_on_the_next_drain);

    RUN_TEST(test_falling_edge_fires_once_when_the_meter_drops);
    RUN_TEST(test_falling_edge_does_not_fire_on_a_boot_that_starts_disconnected);
    RUN_TEST(test_falling_edge_rearms_after_reconnect);

    RUN_TEST(test_app_calibrate_forwards_exactly_one_zero_to_the_source_meter);
    RUN_TEST(test_impatient_double_tap_still_zeroes_the_source_only_once);
    RUN_TEST(test_crank_length_write_answers_without_disturbing_the_source);

    return UNITY_END();
}
