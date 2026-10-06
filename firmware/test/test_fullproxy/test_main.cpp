// Host tests for the SB20 full proxy's pure core (code/findings/sb20-full-proxy.md, issue #291):
//   * FtmsArbiter.h - who may write the bike's FTMS control point, relayed both ways (§3c)
//   * BikeMirror.h  - the bike's static reads, validated, and the proxy's name (§3a/§3b/§3e)
//
// Golden vectors are copied from the real captures in code/findings/captures/ (each names its file
// and JSONL line). The one exception is Set Indoor Bike Simulation (0x11): no capture of it exists
// yet, so that vector is SPEC-BUILT (the same bytes the Python encoder produces) and the test only
// asserts the arbiter relays it unchanged, which holds for any bytes.

#include <unity.h>

#include <string>
#include <vector>

#include "BikeMirror.h"
#include "FtmsArbiter.h"

using namespace sb20proxy;

void setUp() {}
void tearDown() {}

// ---- helpers --------------------------------------------------------------------------------------

static std::vector<uint8_t> H(const char* hex) {
    std::vector<uint8_t> out;
    auto nib = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
        if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
        return (uint8_t)(c - 'A' + 10);
    };
    for (size_t i = 0; hex[i] && hex[i + 1]; i += 2) out.push_back((uint8_t)(nib(hex[i]) << 4 | nib(hex[i + 1])));
    return out;
}

static void assertBytes(const std::vector<uint8_t>& want, const std::vector<uint8_t>& got) {
    TEST_ASSERT_EQUAL_UINT32(want.size(), got.size());
    if (!want.empty()) TEST_ASSERT_EQUAL_HEX8_ARRAY(want.data(), got.data(), want.size());
}

static CpActions clientWrite(FtmsArbiter& a, uint16_t id, const char* hex, uint32_t now) {
    auto v = H(hex);
    return a.onClientWrite(id, v.data(), v.size(), now);
}
static CpActions headUnitWrite(FtmsArbiter& a, const char* hex, uint32_t now) {
    auto v = H(hex);
    return a.onHeadUnitWrite(v.data(), v.size(), now);
}
static CpActions bike(FtmsArbiter& a, const char* hex, uint32_t now) {
    auto v = H(hex);
    return a.onBikeIndication(v.data(), v.size(), now);
}

static void assertWriteBike(const CpAction& act, const char* hex) {
    TEST_ASSERT_EQUAL(CpActionKind::WriteBike, act.kind);
    assertBytes(H(hex), act.bytes);
}
static void assertIndicate(const CpAction& act, uint16_t id, const char* hex) {
    TEST_ASSERT_EQUAL(CpActionKind::IndicateClient, act.kind);
    TEST_ASSERT_EQUAL_UINT16(id, act.clientId);
    assertBytes(H(hex), act.bytes);
}
static void assertHeadUnit(const CpAction& act, HeadUnitNotice n, const char* hex) {
    TEST_ASSERT_EQUAL(CpActionKind::NotifyHeadUnit, act.kind);
    TEST_ASSERT_EQUAL(n, act.notice);
    assertBytes(H(hex), act.bytes);
}

static const uint16_t QZ = 1;    // an app's connection id on the proxy
static const uint16_t APP2 = 2;  // a second app

// A client that holds control with a link up: the captured Request Control round trip.
// G-sb20-ftms-erg-20260621-0949.jsonl line 21 (write 00) / line 22 (indication 800001).
static FtmsArbiter clientOwned() {
    FtmsArbiter a;
    a.onBikeLinkUp(0);
    clientWrite(a, QZ, "00", 10);
    bike(a, "800001", 120);
    return a;
}

// ================================ arbiter ==========================================================

void test_client_request_control_is_forwarded_and_granted() {
    FtmsArbiter a;
    TEST_ASSERT_TRUE(a.onBikeLinkUp(0).empty());  // nobody owns: nothing to re-assert
    auto w = clientWrite(a, QZ, "00", 10);        // erg line 21
    TEST_ASSERT_EQUAL_UINT32(1, w.size());
    assertWriteBike(w[0], "00");
    TEST_ASSERT_TRUE(a.busy());
    auto r = bike(a, "800001", 120);  // erg line 22
    TEST_ASSERT_EQUAL_UINT32(1, r.size());
    assertIndicate(r[0], QZ, "800001");
    TEST_ASSERT_EQUAL(CpOwner::Client, a.owner());
    TEST_ASSERT_EQUAL_UINT16(QZ, a.ownerClient());
    TEST_ASSERT_FALSE(a.busy());
}

void test_erg_relay_round_trip_is_verbatim_including_the_echoed_target() {
    FtmsArbiter a = clientOwned();
    // erg line 27/28: Start, then line 33/34: Set Target Power 150 W. The SB20 echoes the target in
    // its reply (8005019600), so the relay must not rebuild the 3-byte spec form.
    assertWriteBike(clientWrite(a, QZ, "07", 200)[0], "07");
    assertIndicate(bike(a, "800701", 350)[0], QZ, "800701");
    auto w = clientWrite(a, QZ, "059600", 400);
    TEST_ASSERT_EQUAL_UINT32(1, w.size());
    assertWriteBike(w[0], "059600");
    auto r = bike(a, "8005019600", 500);
    TEST_ASSERT_EQUAL_UINT32(1, r.size());
    assertIndicate(r[0], QZ, "8005019600");
    TEST_ASSERT_TRUE(a.started());
    assertBytes(H("059600"), a.lastTarget());
}

void test_second_client_is_refused_control_not_permitted() {
    FtmsArbiter a = clientOwned();
    auto r = clientWrite(a, APP2, "00", 200);
    TEST_ASSERT_EQUAL_UINT32(1, r.size());
    assertIndicate(r[0], APP2, "800005");  // §3c: 0x80 00 05, nothing reaches the bike
    auto r2 = clientWrite(a, APP2, "05c800", 210);
    assertIndicate(r2[0], APP2, "800505");
    TEST_ASSERT_EQUAL_UINT16(QZ, a.ownerClient());
    TEST_ASSERT_EQUAL_UINT32(2, a.refusals());
}

void test_write_without_requesting_control_is_refused() {
    FtmsArbiter a;
    a.onBikeLinkUp(0);
    auto r = clientWrite(a, QZ, "05c800", 10);  // erg200 line 37's bytes, sent with no Request Control
    TEST_ASSERT_EQUAL_UINT32(1, r.size());
    assertIndicate(r[0], QZ, "800505");
    TEST_ASSERT_EQUAL(CpOwner::None, a.owner());
}

void test_head_unit_takes_control_at_workout_start_and_client_is_refused() {
    FtmsArbiter a;
    a.onBikeLinkUp(0);
    auto s = a.onWorkoutStart(10);
    TEST_ASSERT_EQUAL_UINT32(2, s.size());
    assertHeadUnit(s[0], HeadUnitNotice::Granted, "");
    assertWriteBike(s[1], "00");
    assertHeadUnit(bike(a, "800001", 100)[0], HeadUnitNotice::Response, "800001");
    TEST_ASSERT_EQUAL(CpOwner::HeadUnit, a.owner());

    auto r = clientWrite(a, QZ, "00", 200);
    TEST_ASSERT_EQUAL_UINT32(1, r.size());
    assertIndicate(r[0], QZ, "800005");

    // The head unit's own erg write: erg3way line 35/36 (100 W).
    assertWriteBike(headUnitWrite(a, "056400", 300)[0], "056400");
    assertHeadUnit(bike(a, "8005016400", 500)[0], HeadUnitNotice::Response, "8005016400");
}

void test_workout_start_is_refused_while_a_client_owns() {
    FtmsArbiter a = clientOwned();
    auto s = a.onWorkoutStart(200);
    TEST_ASSERT_EQUAL_UINT32(1, s.size());
    assertHeadUnit(s[0], HeadUnitNotice::Refused, "");
    TEST_ASSERT_EQUAL(CpOwner::Client, a.owner());
    // and the head unit's writes are answered "control not permitted", never sent
    auto w = headUnitWrite(a, "056400", 210);
    TEST_ASSERT_EQUAL_UINT32(1, w.size());
    assertHeadUnit(w[0], HeadUnitNotice::Response, "800505");
}

void test_first_claim_wins_even_before_the_bike_answers() {
    FtmsArbiter a;
    a.onBikeLinkUp(0);
    clientWrite(a, QZ, "00", 10);  // in flight, not yet answered
    auto s = a.onWorkoutStart(20);
    assertHeadUnit(s[0], HeadUnitNotice::Refused, "");
    assertIndicate(clientWrite(a, APP2, "00", 30)[0], APP2, "800005");
}

void test_one_request_in_flight_later_ones_queue() {
    FtmsArbiter a = clientOwned();
    auto w1 = clientWrite(a, QZ, "07", 200);
    auto w2 = clientWrite(a, QZ, "059600", 210);  // written before the Start was answered
    TEST_ASSERT_EQUAL_UINT32(1, w1.size());
    TEST_ASSERT_TRUE(w2.empty());
    TEST_ASSERT_EQUAL_UINT32(1, a.queued());
    auto r = bike(a, "800701", 350);  // erg line 28
    TEST_ASSERT_EQUAL_UINT32(2, r.size());
    assertIndicate(r[0], QZ, "800701");
    assertWriteBike(r[1], "059600");  // the queued one goes out the moment the first completes
}

void test_queue_overflow_answers_operation_failed() {
    FtmsArbiter a = clientOwned();
    clientWrite(a, QZ, "07", 200);  // in flight
    for (size_t i = 0; i < FtmsArbiter::kMaxQueue; ++i) TEST_ASSERT_TRUE(clientWrite(a, QZ, "059600", 201).empty());
    auto r = clientWrite(a, QZ, "05c800", 202);
    TEST_ASSERT_EQUAL_UINT32(1, r.size());
    assertIndicate(r[0], QZ, "800504");
}

void test_timeout_answers_operation_failed_and_drops_the_late_reply() {
    FtmsArbiter a = clientOwned();
    clientWrite(a, QZ, "059600", 1000);
    TEST_ASSERT_TRUE(a.onTick(1000 + FtmsArbiter::kDefaultTimeoutMs - 1).empty());
    auto t = a.onTick(1000 + FtmsArbiter::kDefaultTimeoutMs);
    TEST_ASSERT_EQUAL_UINT32(1, t.size());
    assertIndicate(t[0], QZ, "800504");  // §3c: the proxy answers 0x80 <op> 04 itself
    TEST_ASSERT_EQUAL_UINT32(1, a.timeouts());
    TEST_ASSERT_FALSE(a.busy());
    TEST_ASSERT_TRUE(bike(a, "8005019600", 4000).empty());  // too late: stale, dropped
    TEST_ASSERT_EQUAL_UINT32(1, a.staleIndications());
    TEST_ASSERT_EQUAL_UINT32(0, a.lastTarget().size());  // a failed write is never re-asserted
}

void test_timeout_survives_millis_wraparound() {
    FtmsArbiter a;
    const uint32_t nearWrap = 0xFFFFFF00u;
    a.onBikeLinkUp(nearWrap);
    clientWrite(a, QZ, "00", nearWrap);
    TEST_ASSERT_TRUE(a.onTick(nearWrap + 100).empty());
    TEST_ASSERT_EQUAL_UINT32(1, a.onTick(nearWrap + FtmsArbiter::kDefaultTimeoutMs).size());
    TEST_ASSERT_EQUAL(CpOwner::None, a.owner());  // the unanswered claim is reverted
}

void test_link_down_fails_in_flight_and_queued_and_new_writes() {
    FtmsArbiter a = clientOwned();
    clientWrite(a, QZ, "07", 200);
    clientWrite(a, QZ, "059600", 210);
    auto d = a.onBikeLinkDown(220);
    TEST_ASSERT_EQUAL_UINT32(2, d.size());
    assertIndicate(d[0], QZ, "800704");
    assertIndicate(d[1], QZ, "800504");
    TEST_ASSERT_FALSE(a.busy());
    TEST_ASSERT_EQUAL_UINT32(0, a.queued());
    auto w = clientWrite(a, QZ, "05c800", 300);
    TEST_ASSERT_EQUAL_UINT32(1, w.size());
    assertIndicate(w[0], QZ, "800504");  // link down: answered at once, not queued
    TEST_ASSERT_EQUAL(CpOwner::Client, a.owner());  // a bike drop does not cost the app its control
}

void test_link_drop_and_reconnect_reasserts_control_start_and_last_target() {
    FtmsArbiter a = clientOwned();
    // erg line 27/28 and 33/34: Start, then 150 W accepted.
    clientWrite(a, QZ, "07", 200);
    bike(a, "800701", 350);
    clientWrite(a, QZ, "059600", 400);
    bike(a, "8005019600", 500);

    TEST_ASSERT_TRUE(a.onBikeLinkDown(1000).empty());  // nothing in flight: nothing to answer
    auto up = a.onBikeLinkUp(5000);
    TEST_ASSERT_EQUAL_UINT32(1, up.size());
    assertWriteBike(up[0], "00");  // §3c: re-request control first
    auto r1 = bike(a, "800001", 5100);
    TEST_ASSERT_EQUAL_UINT32(1, r1.size());  // the reply goes to nobody; the next re-assert goes out
    assertWriteBike(r1[0], "07");
    auto r2 = bike(a, "800701", 5200);
    TEST_ASSERT_EQUAL_UINT32(1, r2.size());
    assertWriteBike(r2[0], "059600");  // the owner's last Set Target Power, byte for byte
    TEST_ASSERT_TRUE(bike(a, "8005019600", 5300).empty());  // not indicated: the app did not ask
    TEST_ASSERT_EQUAL_UINT32(0, a.reassertFailures());
    TEST_ASSERT_EQUAL(CpOwner::Client, a.owner());
}

void test_reassert_failure_is_counted_not_indicated() {
    FtmsArbiter a = clientOwned();
    a.onBikeLinkDown(100);
    a.onBikeLinkUp(200);
    TEST_ASSERT_TRUE(bike(a, "800005", 300).empty());  // the bike refuses the re-request
    TEST_ASSERT_EQUAL_UINT32(1, a.reassertFailures());
}

void test_head_unit_owner_is_reasserted_after_a_link_drop() {
    FtmsArbiter a;
    a.onWorkoutStart(0);  // link not up yet: ownership is held, the request waits for the link
    auto up = a.onBikeLinkUp(100);
    TEST_ASSERT_EQUAL_UINT32(1, up.size());
    assertWriteBike(up[0], "00");
    bike(a, "800001", 200);
    headUnitWrite(a, "05c800", 300);  // erg200 line 37/39 (200 W)
    bike(a, "800501c800", 400);
    a.onBikeLinkDown(500);
    assertWriteBike(a.onBikeLinkUp(600)[0], "00");
    assertWriteBike(bike(a, "800001", 700)[0], "05c800");  // never started: no Start re-sent
}

void test_client_drop_releases_control_and_the_bike_holds_its_target() {
    FtmsArbiter a = clientOwned();
    clientWrite(a, QZ, "059600", 200);
    bike(a, "8005019600", 300);
    auto d = a.onClientDisconnect(QZ, 400);
    TEST_ASSERT_TRUE(d.empty());  // owner, 2026-10-02: no Stop, no Reset - the bike holds 150 W
    TEST_ASSERT_EQUAL(CpOwner::None, a.owner());
    TEST_ASSERT_EQUAL_UINT32(0, a.lastTarget().size());
    a.onBikeLinkDown(500);
    TEST_ASSERT_TRUE(a.onBikeLinkUp(600).empty());  // nobody owns: nothing re-asserted
    // and the next app can take control
    assertWriteBike(clientWrite(a, APP2, "00", 700)[0], "00");
}

void test_client_drop_with_a_request_in_flight_swallows_the_reply() {
    FtmsArbiter a = clientOwned();
    clientWrite(a, QZ, "05c800", 200);
    a.onClientDisconnect(QZ, 210);
    TEST_ASSERT_TRUE(bike(a, "800501c800", 300).empty());
    TEST_ASSERT_EQUAL(CpOwner::None, a.owner());
    TEST_ASSERT_EQUAL_UINT32(0, a.lastTarget().size());
}

void test_sim_op_is_relayed_unchanged() {
    FtmsArbiter a = clientOwned();
    // SPEC-BUILT (no 0x11 capture yet): Set Indoor Bike Simulation, wind 0, grade +2.50 %, Crr 0,
    // Cw 0 - the bytes code/src/sb20proxy/ble/ftms.py encode_set_indoor_bike_sim(grade_pct=2.5) makes.
    auto w = clientWrite(a, QZ, "110000fa000000", 200);
    TEST_ASSERT_EQUAL_UINT32(1, w.size());
    assertWriteBike(w[0], "110000fa000000");
    assertIndicate(bike(a, "801101", 300)[0], QZ, "801101");
    assertBytes(H("110000fa000000"), a.lastTarget());  // a free ride re-asserts its simulation
}

void test_bike_refusal_reverts_the_claim() {
    FtmsArbiter a;
    a.onBikeLinkUp(0);
    clientWrite(a, QZ, "00", 10);
    clientWrite(a, QZ, "07", 20);  // queued behind the claim
    auto r = bike(a, "800005", 100);
    TEST_ASSERT_EQUAL_UINT32(2, r.size());
    assertIndicate(r[0], QZ, "800005");  // the bike's own refusal, relayed unchanged
    assertIndicate(r[1], QZ, "800705");  // then the queued Start, which never had control
    TEST_ASSERT_EQUAL(CpOwner::None, a.owner());
    TEST_ASSERT_EQUAL_UINT32(0, a.queued());
}

void test_reset_releases_the_clients_control() {
    FtmsArbiter a = clientOwned();
    // erg line 195/196: the capture's "reset_release_control"
    assertWriteBike(clientWrite(a, QZ, "01", 200)[0], "01");
    assertIndicate(bike(a, "800101", 400)[0], QZ, "800101");
    TEST_ASSERT_EQUAL(CpOwner::None, a.owner());
}

void test_workout_stop_releases_without_writing_the_bike() {
    FtmsArbiter a;
    a.onBikeLinkUp(0);
    a.onWorkoutStart(10);
    bike(a, "800001", 100);
    TEST_ASSERT_TRUE(a.onWorkoutStop(200).empty());
    TEST_ASSERT_EQUAL(CpOwner::None, a.owner());
    assertWriteBike(clientWrite(a, QZ, "00", 300)[0], "00");  // the app may take it now
}

void test_stale_and_malformed_indications_are_dropped() {
    FtmsArbiter a = clientOwned();
    TEST_ASSERT_TRUE(bike(a, "800701", 200).empty());  // nothing in flight
    clientWrite(a, QZ, "059600", 300);
    TEST_ASSERT_TRUE(bike(a, "800101", 310).empty());  // a different op: stale
    TEST_ASSERT_TRUE(bike(a, "089600", 320).empty());  // a Status frame, not an indication
    TEST_ASSERT_TRUE(a.busy());
    TEST_ASSERT_EQUAL_UINT32(1, bike(a, "8005019600", 330).size());
    TEST_ASSERT_EQUAL_UINT32(3, a.staleIndications());
}

void test_empty_and_oversized_writes() {
    FtmsArbiter a = clientOwned();
    TEST_ASSERT_TRUE(a.onClientWrite(QZ, nullptr, 0, 200).empty());
    std::vector<uint8_t> big(FtmsArbiter::kMaxWrite + 1, 0x00);
    big[0] = FTMS_CP_SET_TARGET_POWER;
    auto r = a.onClientWrite(QZ, big.data(), big.size(), 210);
    assertIndicate(r[0], QZ, "800503");
    TEST_ASSERT_FALSE(a.busy());
}

// ================================ mirror cache =====================================================

// QDZ-sb20-ftms-gatt-20260706-0739.jsonl, the uncontended GATT dump of bike 1 (E4:AA:5A:D6:0E:D4).
struct GattRead { uint16_t uuid; const char* hex; };
static const GattRead kSb20Reads[] = {
    {0x2A29, "537461676573204379636c696e67"},  // line 5:  "Stages Cycling"
    {0x2A24, "53423230"},                      // line 6:  "SB20"
    {0x2A25, "4830353132323130313035"},        // line 7:  "H0512210105"
    {0x2A26, "312e31"},                        // line 8:  "1.1"
    {0x2A27, "302e30"},                        // line 9:  "0.0"
    {0x2A28, "312e31322e342b33373932"},        // line 10: "1.12.4+3792"
    {0x2ACC, "8a4000000e200000"},              // line 11: Feature
    {0x2AD8, "0000a00f0100"},                  // line 12: power 0..4000 W, 1 W
    {0x2AD6, "0000ff000100"},                  // line 13: resistance 0..255, 1
    {0x2AD5, "18fce8030100"},                  // line 14: inclination -100.0..+100.0 %, 0.1
};
// The bike's advertised name: G-assioma17039-ble-20260615-065730.jsonl line 8 (E4:AA:5A:D6:0E:D4).
static const char* kSb20Name = "Stages Bike 0105";

static bool readInto(BikeMirror& m, uint16_t uuid, const char* hex) {
    auto v = H(hex);
    return m.onRead(uuid, v.data(), v.size());
}

void test_mirror_is_ready_only_once_every_required_read_is_in() {
    BikeMirror m;
    TEST_ASSERT_FALSE(m.ready());
    TEST_ASSERT_TRUE(m.setBikeName(kSb20Name));
    for (const auto& r : kSb20Reads) {
        TEST_ASSERT_FALSE(m.ready());  // the last read (0x2AD5) is required, so never ready early
        TEST_ASSERT_TRUE(readInto(m, r.uuid, r.hex));
    }
    TEST_ASSERT_TRUE(m.ready());
    TEST_ASSERT_EQUAL_STRING("Stages Cycling", m.text(MirrorField::Manufacturer).c_str());
    TEST_ASSERT_EQUAL_STRING("SB20", m.text(MirrorField::Model).c_str());
    TEST_ASSERT_EQUAL_STRING("1.12.4+3792", m.text(MirrorField::SoftwareRev).c_str());
    assertBytes(H("8a4000000e200000"), m.value(MirrorField::Feature));  // served unchanged
    TEST_ASSERT_TRUE(m.feature().powerTargetSetting());
}

void test_mirror_names_what_is_missing() {
    BikeMirror m;
    m.setBikeName(kSb20Name);
    readInto(m, 0x2A29, kSb20Reads[0].hex);
    readInto(m, 0x2A24, kSb20Reads[1].hex);
    readInto(m, 0x2ACC, "8a4000000e200000");
    auto miss = m.missing();
    // the SB20's Target 0x200e promises inclination (bit 1), resistance (bit 2) and power (bit 3)
    TEST_ASSERT_EQUAL_UINT32(3, miss.size());
    TEST_ASSERT_EQUAL(MirrorField::PowerRange, miss[0]);
    TEST_ASSERT_EQUAL(MirrorField::ResistanceRange, miss[1]);
    TEST_ASSERT_EQUAL(MirrorField::InclinationRange, miss[2]);
}

void test_mirror_optional_dis_strings_do_not_block_ready() {
    BikeMirror m;
    m.setBikeName(kSb20Name);
    for (const auto& r : kSb20Reads) {
        if (r.uuid >= 0x2A25 && r.uuid <= 0x2A28) continue;  // serial, fw, hw, sw
        readInto(m, r.uuid, r.hex);
    }
    TEST_ASSERT_TRUE(m.ready());
    TEST_ASSERT_FALSE(m.has(MirrorField::Serial));
}

void test_mirror_requires_only_the_ranges_the_feature_promises() {
    BikeMirror m;
    m.setBikeName(kSb20Name);
    readInto(m, 0x2A29, kSb20Reads[0].hex);
    readInto(m, 0x2A24, kSb20Reads[1].hex);
    // The real Feature with the Target bits cut to power only (0x08): only 0x2AD8 is then required.
    readInto(m, 0x2ACC, "8a40000008000000");
    TEST_ASSERT_FALSE(m.ready());
    readInto(m, 0x2AD8, "0000a00f0100");
    TEST_ASSERT_TRUE(m.ready());
}

void test_mirror_rejects_bad_lengths_and_ranges() {
    BikeMirror m;
    TEST_ASSERT_FALSE(readInto(m, 0x2ACC, "8a4000000e2000"));    // Feature one byte short
    TEST_ASSERT_FALSE(readInto(m, 0x2AD8, "0000a00f01"));        // range truncated
    TEST_ASSERT_FALSE(readInto(m, 0x2AD8, "a00f00000100"));      // min 4000 > max 0 (real bytes swapped)
    TEST_ASSERT_FALSE(readInto(m, 0x2AD8, "0000a00f0000"));      // zero increment
    TEST_ASSERT_FALSE(readInto(m, 0x2AD8, "18fce8030100"));      // negative watts
    TEST_ASSERT_TRUE(readInto(m, 0x2AD5, "18fce8030100"));       // ...which inclination allows
    TEST_ASSERT_FALSE(readInto(m, 0x2A24, ""));                  // empty string
    TEST_ASSERT_FALSE(readInto(m, 0x2A99, "00"));                // not a mirrored characteristic
    TEST_ASSERT_FALSE(m.has(MirrorField::Feature));
    TEST_ASSERT_FALSE(m.has(MirrorField::PowerRange));
}

void test_mirror_strings_trim_nul_padding_and_reject_control_bytes() {
    BikeMirror m;
    TEST_ASSERT_TRUE(readInto(m, 0x2A24, "5342323000"));  // "SB20" + NUL padding
    TEST_ASSERT_EQUAL_STRING("SB20", m.text(MirrorField::Model).c_str());
    TEST_ASSERT_FALSE(readInto(m, 0x2A24, "53420a30"));   // a newline inside
    TEST_ASSERT_EQUAL_STRING("SB20", m.text(MirrorField::Model).c_str());  // the good value is kept
}

void test_mirror_refuses_a_proxys_own_name() {
    BikeMirror m;
    TEST_ASSERT_FALSE(m.setBikeName("Stages Bike 0105 PXY"));  // we scanned ourselves, not the bike
    TEST_ASSERT_FALSE(m.setBikeName(""));
    TEST_ASSERT_FALSE(m.has(MirrorField::Name));
}

void test_mirror_clear_forgets_a_previous_link() {
    BikeMirror m;
    m.setBikeName(kSb20Name);
    for (const auto& r : kSb20Reads) readInto(m, r.uuid, r.hex);
    TEST_ASSERT_TRUE(m.ready());
    m.clear();
    TEST_ASSERT_FALSE(m.ready());
    TEST_ASSERT_FALSE(m.proxyName().ok);
}

// ================================ the proxy's name =================================================

void test_proxy_name_is_the_bikes_name_plus_pxy() {
    ProxyName p = deriveProxyName(kSb20Name);
    TEST_ASSERT_TRUE(p.ok);
    TEST_ASSERT_FALSE(p.truncated);
    TEST_ASSERT_EQUAL_STRING("Stages Bike 0105 PXY", p.name.c_str());
    // §3e: a 22-byte name element; with flags and the 16-bit FTMS UUID the packet is 29 of 31 bytes
    TEST_ASSERT_EQUAL_UINT32(20, p.name.size());
    TEST_ASSERT_EQUAL_UINT32(29, p.advBytes);
    TEST_ASSERT_TRUE(p.advBytes <= kLegacyAdvBudget);
}

void test_proxy_name_from_the_mirror() {
    BikeMirror m;
    TEST_ASSERT_FALSE(m.proxyName().ok);
    m.setBikeName(kSb20Name);
    TEST_ASSERT_EQUAL_STRING("Stages Bike 0105 PXY", m.proxyName().name.c_str());
}

void test_proxy_name_budget_check() {
    TEST_ASSERT_EQUAL_UINT32(22, kMaxProxyNameLen);
    TEST_ASSERT_EQUAL_UINT32(31, proxyAdvertBytes(kMaxProxyNameLen));
    // exactly fits: an 18-character bike name
    ProxyName fit = deriveProxyName("Stages Bike 012345");
    TEST_ASSERT_TRUE(fit.ok);
    TEST_ASSERT_FALSE(fit.truncated);
    TEST_ASSERT_EQUAL_UINT32(31, fit.advBytes);
    // one over: the bike's part is trimmed, the suffix and the "Stages Bike" prefix survive
    ProxyName over = deriveProxyName("Stages Bike 0123456");
    TEST_ASSERT_TRUE(over.ok);
    TEST_ASSERT_TRUE(over.truncated);
    TEST_ASSERT_EQUAL_STRING("Stages Bike 012345 PXY", over.name.c_str());
    TEST_ASSERT_EQUAL_UINT32(31, over.advBytes);
    // trimming never leaves a double space before the suffix
    ProxyName sp = deriveProxyName("Stages Bike 012345 7");
    TEST_ASSERT_EQUAL_STRING("Stages Bike 012345 PXY", sp.name.c_str());
    ProxyName sp2 = deriveProxyName("Stages Bike 01234 67");
    TEST_ASSERT_EQUAL_STRING("Stages Bike 01234 PXY", sp2.name.c_str());
}

void test_proxy_name_rejects_unusable_names() {
    TEST_ASSERT_FALSE(deriveProxyName("").ok);
    TEST_ASSERT_FALSE(deriveProxyName("Stages Bike 0105 PXY").ok);
    TEST_ASSERT_FALSE(deriveProxyName(std::string("Stages\x01")).ok);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_client_request_control_is_forwarded_and_granted);
    RUN_TEST(test_erg_relay_round_trip_is_verbatim_including_the_echoed_target);
    RUN_TEST(test_second_client_is_refused_control_not_permitted);
    RUN_TEST(test_write_without_requesting_control_is_refused);
    RUN_TEST(test_head_unit_takes_control_at_workout_start_and_client_is_refused);
    RUN_TEST(test_workout_start_is_refused_while_a_client_owns);
    RUN_TEST(test_first_claim_wins_even_before_the_bike_answers);
    RUN_TEST(test_one_request_in_flight_later_ones_queue);
    RUN_TEST(test_queue_overflow_answers_operation_failed);
    RUN_TEST(test_timeout_answers_operation_failed_and_drops_the_late_reply);
    RUN_TEST(test_timeout_survives_millis_wraparound);
    RUN_TEST(test_link_down_fails_in_flight_and_queued_and_new_writes);
    RUN_TEST(test_link_drop_and_reconnect_reasserts_control_start_and_last_target);
    RUN_TEST(test_reassert_failure_is_counted_not_indicated);
    RUN_TEST(test_head_unit_owner_is_reasserted_after_a_link_drop);
    RUN_TEST(test_client_drop_releases_control_and_the_bike_holds_its_target);
    RUN_TEST(test_client_drop_with_a_request_in_flight_swallows_the_reply);
    RUN_TEST(test_sim_op_is_relayed_unchanged);
    RUN_TEST(test_bike_refusal_reverts_the_claim);
    RUN_TEST(test_reset_releases_the_clients_control);
    RUN_TEST(test_workout_stop_releases_without_writing_the_bike);
    RUN_TEST(test_stale_and_malformed_indications_are_dropped);
    RUN_TEST(test_empty_and_oversized_writes);

    RUN_TEST(test_mirror_is_ready_only_once_every_required_read_is_in);
    RUN_TEST(test_mirror_names_what_is_missing);
    RUN_TEST(test_mirror_optional_dis_strings_do_not_block_ready);
    RUN_TEST(test_mirror_requires_only_the_ranges_the_feature_promises);
    RUN_TEST(test_mirror_rejects_bad_lengths_and_ranges);
    RUN_TEST(test_mirror_strings_trim_nul_padding_and_reject_control_bytes);
    RUN_TEST(test_mirror_refuses_a_proxys_own_name);
    RUN_TEST(test_mirror_clear_forgets_a_previous_link);

    RUN_TEST(test_proxy_name_is_the_bikes_name_plus_pxy);
    RUN_TEST(test_proxy_name_from_the_mirror);
    RUN_TEST(test_proxy_name_budget_check);
    RUN_TEST(test_proxy_name_rejects_unusable_names);
    return UNITY_END();
}
