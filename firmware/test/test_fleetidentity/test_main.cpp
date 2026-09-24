// Host tests for FleetIdentity.h + RuntimeConfig::resolveIdentity — the per-board default identity.
//
// Why this suite exists: every board used to boot as "Stages 62144", bike 1's REAL left crank, so a
// fresh or erased board next to that bike could pair it to an unfed spoof, and two boards flashed
// from one image were indistinguishable on the air (session 13 G0, #330). The rules below are the
// ones that would rot silently: the derivation is deterministic, per board, zero-padded and never a
// real crank's id — and a STORED identity (including a deliberate real-crank id for a rescue) is
// never touched.
//
// The golden vectors are shared with code/tests/test_qa_acceptance.py (the Python twin), so the two
// sides cannot drift.
#include <unity.h>

#include "FleetIdentity.h"
#include "RuntimeConfig.h"

using namespace sb20proxy;

void setUp() {}
void tearDown() {}

namespace {

// Base MACs from BOARDS.md: the Waveshare S3-Touch and the 0.96" C3 OLED board.
constexpr uint8_t kS3Mac[6] = {0xA4, 0xCB, 0x8F, 0xDA, 0xE9, 0xCC};     // 0xE9CC = 59852 -> 9852
constexpr uint8_t kC3o96Mac[6] = {0x10, 0xB4, 0x1D, 0xBA, 0xC9, 0x0C};  // 0xC90C = 51468 -> 1468

void test_golden_vectors_shared_with_the_python_twin() {
    TEST_ASSERT_EQUAL_STRING("Stages 99852", defaultSpoofName(kS3Mac).c_str());
    TEST_ASSERT_EQUAL_STRING("Stages 91468", defaultSpoofName(kC3o96Mac).c_str());
    // The SetupPin.h example SSID "Setup-A6E9" is cut from the same two bytes: 0xA6E9 = 42729 -> 2729.
    const uint8_t setupExample[6] = {0, 0, 0, 0, 0xA6, 0xE9};
    TEST_ASSERT_EQUAL_STRING("Stages 92729", defaultSpoofName(setupExample).c_str());
}

void test_derivation_is_deterministic() {
    TEST_ASSERT_EQUAL_STRING(defaultSpoofName(kS3Mac).c_str(), defaultSpoofName(kS3Mac).c_str());
    TEST_ASSERT_EQUAL_STRING("Stages 99852", defaultSpoofName(kS3Mac).c_str());
}

void test_only_the_last_two_bytes_matter_and_a_low_bit_changes_the_name() {
    const uint8_t a[6] = {0xA4, 0xCB, 0x8F, 0xDA, 0xE9, 0xCC};
    const uint8_t b[6] = {0x00, 0x11, 0x22, 0x33, 0xE9, 0xCC};  // different OUI, same tail -> same name
    const uint8_t c[6] = {0xA4, 0xCB, 0x8F, 0xDA, 0xE9, 0xCD};  // lowest bit flipped -> different name
    TEST_ASSERT_EQUAL_STRING(defaultSpoofName(a).c_str(), defaultSpoofName(b).c_str());
    TEST_ASSERT_TRUE(defaultSpoofName(a) != defaultSpoofName(c));
    TEST_ASSERT_EQUAL_STRING("Stages 99853", defaultSpoofName(c).c_str());
}

void test_zero_padding_keeps_four_digits() {
    const uint8_t seven[6] = {0, 0, 0, 0, 0x00, 0x07};
    TEST_ASSERT_EQUAL_STRING("Stages 90007", defaultSpoofName(seven).c_str());
    const uint8_t zero[6] = {0, 0, 0, 0, 0x00, 0x00};
    TEST_ASSERT_EQUAL_STRING("Stages 90000", defaultSpoofName(zero).c_str());
    const uint8_t tenThousand[6] = {0, 0, 0, 0, 0x27, 0x10};  // 10000 wraps to 0000
    TEST_ASSERT_EQUAL_STRING("Stages 90000", defaultSpoofName(tenThousand).c_str());
}

void test_never_a_real_crank_id_for_any_mac_tail() {
    // The 9-prefix keeps every derived id out of the real cranks' range; sweep every tail to prove it,
    // including the one whose low digits spell 2144 (0x0860 = 2144 -> "Stages 92144", not 62144).
    for (unsigned tail = 0; tail < 65536u; ++tail) {
        const uint8_t mac[6] = {0, 0, 0, 0, (uint8_t)(tail >> 8), (uint8_t)(tail & 0xFF)};
        const std::string name = defaultSpoofName(mac);
        if (isRealCrankIdentity(name)) TEST_FAIL_MESSAGE(("derived a real crank id: " + name).c_str());
        if (name.size() != 12) TEST_FAIL_MESSAGE(("not \"Stages 9NNNN\": " + name).c_str());
    }
    const uint8_t spells2144[6] = {0, 0, 0, 0, 0x08, 0x60};
    TEST_ASSERT_EQUAL_STRING("Stages 92144", defaultSpoofName(spells2144).c_str());
}

void test_real_crank_identities_are_bike_1s_two_cranks_exactly() {
    TEST_ASSERT_TRUE(isRealCrankIdentity("Stages 62144"));   // bike 1's left crank
    TEST_ASSERT_TRUE(isRealCrankIdentity("Stages 4963"));    // bike 1's right crank
    TEST_ASSERT_FALSE(isRealCrankIdentity("Stages 62145"));  // our own id (session 8: proven to pair)
    TEST_ASSERT_FALSE(isRealCrankIdentity("Stages 92729"));
    TEST_ASSERT_FALSE(isRealCrankIdentity("SB20 Corrector"));
    TEST_ASSERT_FALSE(isRealCrankIdentity(""));
}

// ---------- RuntimeConfig::resolveIdentity — absent vs stored -----------------------------------

void test_defaults_carry_no_identity_until_resolved() {
    RuntimeConfig c = RuntimeConfig::defaults();
    TEST_ASSERT_EQUAL_STRING("", c.spoofName.c_str());  // "" = not stored; derived at boot
}

void test_an_absent_identity_is_derived_from_the_mac_and_reported_as_default() {
    RuntimeConfig c = RuntimeConfig::defaults();
    TEST_ASSERT_TRUE(c.resolveIdentity(kS3Mac));
    TEST_ASSERT_EQUAL_STRING("Stages 99852", c.spoofName.c_str());
    TEST_ASSERT_FALSE(c.resolveIdentity(kS3Mac));  // idempotent: now set in memory, nothing to do
    TEST_ASSERT_EQUAL_STRING("Stages 99852", c.spoofName.c_str());
}

void test_a_stored_identity_is_left_exactly_as_stored() {
    RuntimeConfig c = RuntimeConfig::fromLine("v2|aa:bb:cc:dd:ee:ff|ASSIOMA|0|Stages 62145|11821518");
    TEST_ASSERT_FALSE(c.resolveIdentity(kS3Mac));
    TEST_ASSERT_EQUAL_STRING("Stages 62145", c.spoofName.c_str());
}

void test_a_deliberate_real_crank_id_for_a_crank_rescue_is_not_overridden() {
    // The single-right-crank rescue: a board deliberately configured AS bike 1's crank must stay so.
    RuntimeConfig c = RuntimeConfig::fromLine("v2|e3:25:39:38:92:71|Stages|1|Stages 62144|11821518");
    TEST_ASSERT_FALSE(c.resolveIdentity(kS3Mac));
    TEST_ASSERT_EQUAL_STRING("Stages 62144", c.spoofName.c_str());
}

void test_a_legacy_line_with_no_identity_field_derives_one() {
    // A pre-spoof-picker 3-field line, and any line whose identity slot is empty, has NO identity —
    // it must not silently mean "Stages 62144" any more.
    RuntimeConfig legacy = RuntimeConfig::fromLine("aa:bb:cc:dd:ee:ff|ASSIOMA|1");
    TEST_ASSERT_EQUAL_STRING("", legacy.spoofName.c_str());
    TEST_ASSERT_TRUE(legacy.resolveIdentity(kC3o96Mac));
    TEST_ASSERT_EQUAL_STRING("Stages 91468", legacy.spoofName.c_str());
    RuntimeConfig blankSlot = RuntimeConfig::fromLine("v2|aa:bb:cc:dd:ee:ff|ASSIOMA|0||11821518|0");
    TEST_ASSERT_TRUE(blankSlot.resolveIdentity(kC3o96Mac));
    TEST_ASSERT_EQUAL_STRING("Stages 91468", blankSlot.spoofName.c_str());
    TEST_ASSERT_EQUAL_STRING("11821518", blankSlot.spoofSerial.c_str());  // the rest of the line is kept
}

void test_a_corrector_with_no_name_gets_our_own_honest_name_not_a_stages_id() {
    RuntimeConfig c = RuntimeConfig::defaults();
    c.mode = ProxyMode::Corrector;
    TEST_ASSERT_TRUE(c.resolveIdentity(kS3Mac));
    TEST_ASSERT_EQUAL_STRING(Config::CORRECTOR_NAME, c.spoofName.c_str());
}

void test_an_unresolved_config_round_trips_as_still_unresolved() {
    // The web hooks reload from NVS, so the boot path never writes the derived name back; a blank
    // identity saved from a form stays blank -> derived again next boot, never pinned by accident.
    RuntimeConfig c = RuntimeConfig::defaults();
    RuntimeConfig back = RuntimeConfig::fromLine(c.toLine());
    TEST_ASSERT_EQUAL_STRING("", back.spoofName.c_str());
    TEST_ASSERT_TRUE(back.resolveIdentity(kS3Mac));
    TEST_ASSERT_EQUAL_STRING("Stages 99852", back.spoofName.c_str());
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_golden_vectors_shared_with_the_python_twin);
    RUN_TEST(test_derivation_is_deterministic);
    RUN_TEST(test_only_the_last_two_bytes_matter_and_a_low_bit_changes_the_name);
    RUN_TEST(test_zero_padding_keeps_four_digits);
    RUN_TEST(test_never_a_real_crank_id_for_any_mac_tail);
    RUN_TEST(test_real_crank_identities_are_bike_1s_two_cranks_exactly);
    RUN_TEST(test_defaults_carry_no_identity_until_resolved);
    RUN_TEST(test_an_absent_identity_is_derived_from_the_mac_and_reported_as_default);
    RUN_TEST(test_a_stored_identity_is_left_exactly_as_stored);
    RUN_TEST(test_a_deliberate_real_crank_id_for_a_crank_rescue_is_not_overridden);
    RUN_TEST(test_a_legacy_line_with_no_identity_field_derives_one);
    RUN_TEST(test_a_corrector_with_no_name_gets_our_own_honest_name_not_a_stages_id);
    RUN_TEST(test_an_unresolved_config_round_trips_as_still_unresolved);
    return UNITY_END();
}
