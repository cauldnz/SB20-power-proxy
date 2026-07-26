// Host-side unit tests for the pure peer-role ladder (firmware-nrf/lib/bridge/PeerRole.h).
//
//   pio test -e native
//
// The bridge multiplexes four concurrent central links by peer NAME. That routing used to be
// written twice — once in `scanCb` (connect to this advert?) and once in `centralConnectCb`
// (what is this link?) — and the two copies had drifted apart. These tests pin the ladder,
// including the drift, so the seam extraction is provably behaviour-neutral and the divergence
// is documented rather than assumed correct.

#include <unity.h>

#include "PeerRole.h"

using namespace bridge;

void setUp(void) {}
void tearDown(void) {}

// ================= nameContains: the empty-filter trap =========================================

void test_name_contains_empty_needle_matches_everything(void) {
    // strstr(x, "") == x. This is why every caller gates on `filter[0]` when an empty filter is
    // meant to DISABLE a role rather than match all of them.
    TEST_ASSERT_TRUE(nameContains("Assioma 12345", ""));
    TEST_ASSERT_TRUE(nameContains("", ""));
}

void test_name_contains_is_a_substring_match_not_equality(void) {
    TEST_ASSERT_TRUE(nameContains("Stages 62144", "Stages"));
    TEST_ASSERT_TRUE(nameContains("Stages 62144", "62144"));
    TEST_ASSERT_FALSE(nameContains("Stages 62144", "Assioma"));
    TEST_ASSERT_FALSE(nameContains("", "Stages"));
}

void test_name_contains_tolerates_null(void) {
    TEST_ASSERT_FALSE(nameContains(nullptr, "Stages"));
    TEST_ASSERT_FALSE(nameContains("Stages", nullptr));
}

// ================= scan-time ladder ============================================================

void test_advert_takes_any_cps_when_no_source_filter(void) {
    // The desk fake-meter case: Windows stamps the PC's own name into the scan response, so the
    // rig can only be picked up by "any CPS advertiser".
    PeerFilters f;
    PeerRoleState s;
    AdvertDecision d = classifyAdvert("CHAULDP1GEN8", true, /*advertisesCps=*/true, f, s);
    TEST_ASSERT_TRUE(d.connect);
    TEST_ASSERT_EQUAL(PeerRole::Source, d.role);
}

void test_advert_ignores_non_cps_when_no_source_filter(void) {
    PeerFilters f;
    PeerRoleState s;
    TEST_ASSERT_FALSE(classifyAdvert("Echo Dot-851", true, /*advertisesCps=*/false, f, s).connect);
}

void test_advert_matches_source_by_name_when_filter_set(void) {
    PeerFilters f;
    f.source = "Assioma";
    PeerRoleState s;
    // The CPS bit is deliberately not consulted once a name filter is set — service discovery is
    // the validator, because the name lives in the scan response and 0x1818 in the adv packet.
    TEST_ASSERT_TRUE(classifyAdvert("Assioma 17039", true, false, f, s).connect);
    TEST_ASSERT_FALSE(classifyAdvert("Stages 62144", true, true, f, s).connect);
}

void test_advert_without_a_name_cannot_match_a_source_filter(void) {
    PeerFilters f;
    f.source = "Assioma";
    PeerRoleState s;
    TEST_ASSERT_FALSE(classifyAdvert("", /*haveName=*/false, true, f, s).connect);
}

void test_advert_stops_connecting_sources_once_one_is_up(void) {
    PeerFilters f;
    PeerRoleState s;
    s.srcConnected = true;
    TEST_ASSERT_FALSE(classifyAdvert("Assioma 17039", true, true, f, s).connect);
}

void test_advert_still_takes_the_reference_while_a_source_is_up(void) {
    // The whole point of calibration: hold the DUT and add a second meter alongside it.
    PeerFilters f;
    f.source = "Assioma";
    f.reference = "XCADEY";
    PeerRoleState s;
    s.srcConnected = true;
    s.calibrating = true;
    AdvertDecision d = classifyAdvert("XCADEY-X1", true, true, f, s);
    TEST_ASSERT_TRUE(d.connect);
    TEST_ASSERT_EQUAL(PeerRole::Reference, d.role);
}

void test_advert_reference_needs_calibration_active(void) {
    PeerFilters f;
    f.reference = "XCADEY";
    PeerRoleState s;  // calibrating = false
    TEST_ASSERT_EQUAL(PeerRole::Source, classifyAdvert("XCADEY-X1", true, true, f, s).role);
}

void test_advert_reference_is_not_taken_twice(void) {
    PeerFilters f;
    f.reference = "XCADEY";
    PeerRoleState s;
    s.calibrating = true;
    s.refConnected = true;
    s.srcConnected = true;
    TEST_ASSERT_FALSE(classifyAdvert("XCADEY-X1", true, true, f, s).connect);
}

void test_advert_reference_excluded_when_it_also_matches_the_source_filter(void) {
    // One meter must not be latched as both DUT and reference: the fit would compare it against
    // itself and produce a meaningless identity curve.
    PeerFilters f;
    f.source = "XCADEY";
    f.reference = "XCADEY";
    PeerRoleState s;
    s.calibrating = true;
    TEST_ASSERT_EQUAL(PeerRole::Source, classifyAdvert("XCADEY-X1", true, true, f, s).role);
}

void test_advert_trainer_wins_over_source(void) {
    PeerFilters f;
    f.trainer = "SB20-FTMS";
    PeerRoleState s;
    AdvertDecision d = classifyAdvert("SB20-FTMS-Server", true, true, f, s);
    TEST_ASSERT_TRUE(d.connect);
    TEST_ASSERT_EQUAL(PeerRole::Trainer, d.role);
}

void test_advert_trainer_not_taken_twice(void) {
    PeerFilters f;
    f.trainer = "SB20-FTMS";
    PeerRoleState s;
    s.ergConnected = true;
    // Falls through the ladder; with no source filter and no CPS bit there is nothing to take.
    TEST_ASSERT_FALSE(classifyAdvert("SB20-FTMS-Server", true, false, f, s).connect);
}

void test_advert_sb20_shifter_only_when_sink_enabled(void) {
    PeerFilters f;
    PeerRoleState s;
    TEST_ASSERT_EQUAL(PeerRole::Source, classifyAdvert("Stages Bike 1234", true, false, f, s).role);
    s.sinkShifter = true;
    AdvertDecision d = classifyAdvert("Stages Bike 1234", true, false, f, s);
    TEST_ASSERT_TRUE(d.connect);
    TEST_ASSERT_EQUAL(PeerRole::Sb20Shifter, d.role);
}

void test_advert_empty_source_filter_makes_the_reference_unreachable(void) {
    // PINS A LATENT BUG, NOT DESIRED BEHAVIOUR.
    //
    // The reference branch excludes names that also match the source filter — but it spells that
    // as `strstr(name, srcFilter) == nullptr`, and strstr(name, "") always matches. So whenever
    // srcFilter is EMPTY (the "accept any CPS advertiser" desk mode) the exclusion fires for every
    // peer and no reference meter can ever be latched by the scanner. Calibration silently never
    // starts. Found by extracting this ladder; see decisions.md.
    PeerFilters f;
    f.reference = "XCADEY";
    f.source = "";  // any-CPS mode
    PeerRoleState s;
    s.calibrating = true;

    AdvertDecision d = classifyAdvert("XCADEY-X1", true, true, f, s);
    TEST_ASSERT_NOT_EQUAL(PeerRole::Reference, d.role);

    // With a source filter that simply doesn't match, the same peer IS taken as the reference.
    f.source = "Assioma";
    TEST_ASSERT_EQUAL(PeerRole::Reference, classifyAdvert("XCADEY-X1", true, true, f, s).role);
}

void test_advert_ladder_priority_trainer_then_reference_then_sb20(void) {
    // A pathological peer whose name matches every filter at once proves the ordering. srcFilter
    // is non-empty and non-matching so the reference branch is actually reachable — see
    // test_advert_empty_source_filter_makes_the_reference_unreachable for why that matters.
    PeerFilters f;
    f.trainer = "X";
    f.reference = "X";
    f.source = "Assioma";
    PeerRoleState s;
    s.calibrating = true;
    s.sinkShifter = true;
    const char* name = "X Stages Bike";

    TEST_ASSERT_EQUAL(PeerRole::Trainer, classifyAdvert(name, true, true, f, s).role);
    s.ergConnected = true;
    TEST_ASSERT_EQUAL(PeerRole::Reference, classifyAdvert(name, true, true, f, s).role);
    s.refConnected = true;
    TEST_ASSERT_EQUAL(PeerRole::Sb20Shifter, classifyAdvert(name, true, true, f, s).role);
    s.sb20Connected = true;
    TEST_ASSERT_EQUAL(PeerRole::Source, classifyAdvert(name, true, true, f, s).role);
}

// ================= connect-time ladder =========================================================

void test_connection_defaults_to_source(void) {
    PeerFilters f;
    PeerRoleState s;
    TEST_ASSERT_EQUAL(PeerRole::Source, classifyConnection("Assioma 17039", f, s));
}

void test_connection_classifies_trainer_reference_and_sb20(void) {
    PeerFilters f;
    f.trainer = "SB20-FTMS";
    f.reference = "XCADEY";
    PeerRoleState s;
    s.calibrating = true;
    s.sinkShifter = true;

    TEST_ASSERT_EQUAL(PeerRole::Trainer, classifyConnection("SB20-FTMS-Server", f, s));
    TEST_ASSERT_EQUAL(PeerRole::Reference, classifyConnection("XCADEY-X1", f, s));
    TEST_ASSERT_EQUAL(PeerRole::Sb20Shifter, classifyConnection("Stages Bike 1234", f, s));
}

void test_connection_never_reclassifies_the_source_link(void) {
    // The source link's own handle must keep its role even if its name matches another filter, or
    // the bridge would tear down the very meter it exists to read.
    PeerFilters f;
    f.trainer = "Stages";
    f.reference = "Stages";
    PeerRoleState s;
    s.calibrating = true;
    s.sinkShifter = true;
    s.isSourceLink = true;
    TEST_ASSERT_EQUAL(PeerRole::Source, classifyConnection("Stages Bike 1234", f, s));
}

void test_connect_ladder_is_missing_two_reference_guards(void) {
    // PINS A KNOWN DIVERGENCE, NOT DESIRED BEHAVIOUR.
    //
    // `classifyAdvert` refuses a reference when one is already connected, and when the name also
    // matches the source filter. `classifyConnection` does neither. So a link scan-time would
    // never have opened as a reference is still *classified* as one on connect — overwriting the
    // existing reference handle and orphaning that link.
    //
    // Preserved verbatim so the seam extraction is behaviour-neutral. When this is fixed both
    // assertions below flip to PeerRole::Source and this test should be deleted.
    PeerFilters f;
    f.reference = "XCADEY";
    f.source = "XCADEY";
    PeerRoleState s;
    s.calibrating = true;

    s.refConnected = true;  // scan-time would refuse; connect-time does not
    TEST_ASSERT_EQUAL(PeerRole::Reference, classifyConnection("XCADEY-X1", f, s));

    s.refConnected = false;  // name also matches the source filter; scan-time excludes it
    TEST_ASSERT_EQUAL(PeerRole::Reference, classifyConnection("XCADEY-X1", f, s));
}

void test_connection_with_empty_peer_name_is_a_source(void) {
    // getPeerName can come back empty; no role must be inferred from that.
    PeerFilters f;
    f.trainer = "SB20-FTMS";
    f.reference = "XCADEY";
    PeerRoleState s;
    s.calibrating = true;
    s.sinkShifter = true;
    TEST_ASSERT_EQUAL(PeerRole::Source, classifyConnection("", f, s));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_name_contains_empty_needle_matches_everything);
    RUN_TEST(test_name_contains_is_a_substring_match_not_equality);
    RUN_TEST(test_name_contains_tolerates_null);

    RUN_TEST(test_advert_takes_any_cps_when_no_source_filter);
    RUN_TEST(test_advert_ignores_non_cps_when_no_source_filter);
    RUN_TEST(test_advert_matches_source_by_name_when_filter_set);
    RUN_TEST(test_advert_without_a_name_cannot_match_a_source_filter);
    RUN_TEST(test_advert_stops_connecting_sources_once_one_is_up);
    RUN_TEST(test_advert_still_takes_the_reference_while_a_source_is_up);
    RUN_TEST(test_advert_reference_needs_calibration_active);
    RUN_TEST(test_advert_reference_is_not_taken_twice);
    RUN_TEST(test_advert_reference_excluded_when_it_also_matches_the_source_filter);
    RUN_TEST(test_advert_trainer_wins_over_source);
    RUN_TEST(test_advert_trainer_not_taken_twice);
    RUN_TEST(test_advert_sb20_shifter_only_when_sink_enabled);
    RUN_TEST(test_advert_empty_source_filter_makes_the_reference_unreachable);
    RUN_TEST(test_advert_ladder_priority_trainer_then_reference_then_sb20);

    RUN_TEST(test_connection_defaults_to_source);
    RUN_TEST(test_connection_classifies_trainer_reference_and_sb20);
    RUN_TEST(test_connection_never_reclassifies_the_source_link);
    RUN_TEST(test_connect_ladder_is_missing_two_reference_guards);
    RUN_TEST(test_connection_with_empty_peer_name_is_a_source);
    return UNITY_END();
}
