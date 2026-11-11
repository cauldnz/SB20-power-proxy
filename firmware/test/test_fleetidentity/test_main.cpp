// Host tests for FleetIdentity.h + RuntimeConfig::resolveIdentity — the per-board default identity.
//
// Why this suite exists: every board used to boot as "Stages 62144", bike 1's REAL left crank, so a
// fresh or erased board next to that bike could pair it to an unfed spoof, and two boards flashed
// from one image were indistinguishable on the air (session 13 G0, #330). The rules below are the
// ones that would rot silently: the derivation is deterministic, per board, inside the Stages app's
// 0..65535 ANT ID range, unpadded and never a real crank's id — and a STORED identity (including a deliberate real-crank id for a rescue) is
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
constexpr uint8_t kS3Mac[6] = {0xA4, 0xCB, 0x8F, 0xDA, 0xE9, 0xCC};     // 0xE9CC = 59852
constexpr uint8_t kC3o96Mac[6] = {0x10, 0xB4, 0x1D, 0xBA, 0xC9, 0x0C};  // 0xC90C = 51468

void test_golden_vectors_shared_with_the_python_twin() {
    TEST_ASSERT_EQUAL_STRING("Stages 59852", defaultSpoofName(kS3Mac).c_str());
    TEST_ASSERT_EQUAL_STRING("Stages 51468", defaultSpoofName(kC3o96Mac).c_str());
    // The SetupPin.h example SSID "Setup-A6E9" is cut from the same two bytes: 0xA6E9 = 42729.
    const uint8_t setupExample[6] = {0, 0, 0, 0, 0xA6, 0xE9};
    TEST_ASSERT_EQUAL_STRING("Stages 42729", defaultSpoofName(setupExample).c_str());
}

void test_derivation_is_deterministic() {
    TEST_ASSERT_EQUAL_STRING(defaultSpoofName(kS3Mac).c_str(), defaultSpoofName(kS3Mac).c_str());
    TEST_ASSERT_EQUAL_STRING("Stages 59852", defaultSpoofName(kS3Mac).c_str());
}

void test_only_the_last_two_bytes_matter_and_a_low_bit_changes_the_name() {
    const uint8_t a[6] = {0xA4, 0xCB, 0x8F, 0xDA, 0xE9, 0xCC};
    const uint8_t b[6] = {0x00, 0x11, 0x22, 0x33, 0xE9, 0xCC};  // different OUI, same tail -> same name
    const uint8_t c[6] = {0xA4, 0xCB, 0x8F, 0xDA, 0xE9, 0xCD};  // lowest bit flipped -> different name
    TEST_ASSERT_EQUAL_STRING(defaultSpoofName(a).c_str(), defaultSpoofName(b).c_str());
    TEST_ASSERT_TRUE(defaultSpoofName(a) != defaultSpoofName(c));
    TEST_ASSERT_EQUAL_STRING("Stages 59853", defaultSpoofName(c).c_str());
}

void test_unpadded_and_never_the_ant_wildcard() {
    const uint8_t seven[6] = {0, 0, 0, 0, 0x00, 0x07};
    TEST_ASSERT_EQUAL_STRING("Stages 7", defaultSpoofName(seven).c_str());  // real cranks are unpadded
    const uint8_t zero[6] = {0, 0, 0, 0, 0x00, 0x00};
    TEST_ASSERT_EQUAL_STRING("Stages 1", defaultSpoofName(zero).c_str());   // ANT 0 is the wildcard
    const uint8_t top[6] = {0, 0, 0, 0, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL_STRING("Stages 65535", defaultSpoofName(top).c_str());  // the app's maximum
}

void test_every_derived_id_fits_the_stages_app_and_is_never_a_real_crank() {
    // The Stages app's "Left/Right ANT ID" fields take 0..65535 only: it rejected 99744 on
    // 2026-11-11 (screenshots/stages-app/power-meters-ant-id-range-rejects-99744.png). Sweep every tail.
    for (unsigned tail = 0; tail < 65536u; ++tail) {
        const uint8_t mac[6] = {0, 0, 0, 0, (uint8_t)(tail >> 8), (uint8_t)(tail & 0xFF)};
        const std::string name = defaultSpoofName(mac);
        if (isRealCrankIdentity(name)) TEST_FAIL_MESSAGE(("derived a real crank id: " + name).c_str());
        const std::string digits = name.substr(std::string(kDerivedIdentityPrefix).size());
        if (name.rfind(kDerivedIdentityPrefix, 0) != 0 || !isAllDigits(digits) || digits[0] == '0')
            TEST_FAIL_MESSAGE(("not \"Stages N\": " + name).c_str());
        const unsigned long n = std::stoul(digits);
        if (n < 1 || n > kMaxAntDeviceNumber) TEST_FAIL_MESSAGE(("outside 1..65535: " + name).c_str());
    }
    // The tails that spell bike 1's own cranks get their low bit flipped.
    const uint8_t spells62144[6] = {0, 0, 0, 0, 0xF2, 0xC0};
    TEST_ASSERT_EQUAL_STRING("Stages 62145", defaultSpoofName(spells62144).c_str());
    const uint8_t spells4963[6] = {0, 0, 0, 0, 0x13, 0x63};
    TEST_ASSERT_EQUAL_STRING("Stages 4962", defaultSpoofName(spells4963).c_str());
}

void test_real_crank_identities_are_bike_1s_two_cranks_exactly() {
    TEST_ASSERT_TRUE(isRealCrankIdentity("Stages 62144"));   // bike 1's left crank
    TEST_ASSERT_TRUE(isRealCrankIdentity("Stages 4963"));    // bike 1's right crank
    TEST_ASSERT_FALSE(isRealCrankIdentity("Stages 62145"));  // our own id (session 8: proven to pair)
    TEST_ASSERT_FALSE(isRealCrankIdentity("Stages 42729"));
    TEST_ASSERT_FALSE(isRealCrankIdentity("SB20 Corrector"));
    TEST_ASSERT_FALSE(isRealCrankIdentity(""));
}

// ---------- the "Stages " prefix: a bare id is never found by the bike (#384 / #382) ------------

void test_a_bare_crank_id_is_normalised_to_the_name_the_bike_scans_for() {
    // The defect an outside user hit: he entered his real left crank's id and the board advertised
    // "51862", which the SB20 never matches — it scans for "Stages 51862".
    TEST_ASSERT_EQUAL_STRING("Stages 51862", normaliseSpoofName("51862").c_str());
    TEST_ASSERT_EQUAL_STRING("Stages 62144", normaliseSpoofName("62144").c_str());
    TEST_ASSERT_EQUAL_STRING("Stages 4963", normaliseSpoofName("4963").c_str());
}

void test_normalisation_leaves_everything_else_alone_and_is_idempotent() {
    TEST_ASSERT_EQUAL_STRING("", normaliseSpoofName("").c_str());  // blank = derive at boot
    // Already correct -> untouched, so re-saving a good name never doubles the prefix.
    TEST_ASSERT_EQUAL_STRING("Stages 51862", normaliseSpoofName("Stages 51862").c_str());
    TEST_ASSERT_EQUAL_STRING("Stages 51862",
                             normaliseSpoofName(normaliseSpoofName("51862")).c_str());
    // A custom non-numeric name is the user's business: preserved verbatim, warned about elsewhere.
    TEST_ASSERT_EQUAL_STRING("SB20 Corrector", normaliseSpoofName("SB20 Corrector").c_str());
    TEST_ASSERT_EQUAL_STRING("my crank", normaliseSpoofName("my crank").c_str());
    // Not all-digits -> not a bare id, even though it looks close.
    TEST_ASSERT_EQUAL_STRING("51862 L", normaliseSpoofName("51862 L").c_str());
    TEST_ASSERT_EQUAL_STRING(" 51862", normaliseSpoofName(" 51862").c_str());
}

void test_is_all_digits() {
    TEST_ASSERT_TRUE(isAllDigits("0"));
    TEST_ASSERT_TRUE(isAllDigits("51862"));
    TEST_ASSERT_FALSE(isAllDigits(""));  // blank is not an id
    TEST_ASSERT_FALSE(isAllDigits("51862 "));
    TEST_ASSERT_FALSE(isAllDigits("-1"));
    TEST_ASSERT_FALSE(isAllDigits("1e5"));
    TEST_ASSERT_FALSE(isAllDigits("Stages 51862"));
}

void test_the_warning_flags_exactly_the_names_the_bike_cannot_find() {
    TEST_ASSERT_TRUE(spoofNameMissingStagesPrefix("my crank"));
    TEST_ASSERT_TRUE(spoofNameMissingStagesPrefix("stages 51862"));  // case matters on the air
    TEST_ASSERT_TRUE(spoofNameMissingStagesPrefix("Stages51862"));   // the space is part of it
    TEST_ASSERT_FALSE(spoofNameMissingStagesPrefix(""));             // blank = derived, always fine
    TEST_ASSERT_FALSE(spoofNameMissingStagesPrefix("Stages 51862"));
    TEST_ASSERT_FALSE(spoofNameMissingStagesPrefix(defaultSpoofName(kS3Mac)));  // derived is prefixed
}

void test_every_derived_identity_passes_its_own_prefix_rule() {
    // The two rules must agree: a MAC-derived default must never trip the warning.
    for (unsigned tail = 0; tail < 65536u; tail += 7u) {
        const uint8_t mac[6] = {0, 0, 0, 0, (uint8_t)(tail >> 8), (uint8_t)(tail & 0xFF)};
        const std::string name = defaultSpoofName(mac);
        if (spoofNameMissingStagesPrefix(name))
            TEST_FAIL_MESSAGE(("derived name trips the prefix warning: " + name).c_str());
    }
}

// ---------- RuntimeConfig::resolveIdentity — absent vs stored -----------------------------------

void test_defaults_carry_no_identity_until_resolved() {
    RuntimeConfig c = RuntimeConfig::defaults();
    TEST_ASSERT_EQUAL_STRING("", c.spoofName.c_str());  // "" = not stored; derived at boot
}

void test_an_absent_identity_is_derived_from_the_mac_and_reported_as_default() {
    RuntimeConfig c = RuntimeConfig::defaults();
    TEST_ASSERT_TRUE(c.resolveIdentity(kS3Mac));
    TEST_ASSERT_EQUAL_STRING("Stages 59852", c.spoofName.c_str());
    TEST_ASSERT_FALSE(c.resolveIdentity(kS3Mac));  // idempotent: now set in memory, nothing to do
    TEST_ASSERT_EQUAL_STRING("Stages 59852", c.spoofName.c_str());
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
    TEST_ASSERT_EQUAL_STRING("Stages 51468", legacy.spoofName.c_str());
    RuntimeConfig blankSlot = RuntimeConfig::fromLine("v2|aa:bb:cc:dd:ee:ff|ASSIOMA|0||11821518|0");
    TEST_ASSERT_TRUE(blankSlot.resolveIdentity(kC3o96Mac));
    TEST_ASSERT_EQUAL_STRING("Stages 51468", blankSlot.spoofName.c_str());
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
    TEST_ASSERT_EQUAL_STRING("Stages 59852", back.spoofName.c_str());
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_golden_vectors_shared_with_the_python_twin);
    RUN_TEST(test_derivation_is_deterministic);
    RUN_TEST(test_only_the_last_two_bytes_matter_and_a_low_bit_changes_the_name);
    RUN_TEST(test_unpadded_and_never_the_ant_wildcard);
    RUN_TEST(test_every_derived_id_fits_the_stages_app_and_is_never_a_real_crank);
    RUN_TEST(test_real_crank_identities_are_bike_1s_two_cranks_exactly);
    RUN_TEST(test_a_bare_crank_id_is_normalised_to_the_name_the_bike_scans_for);
    RUN_TEST(test_normalisation_leaves_everything_else_alone_and_is_idempotent);
    RUN_TEST(test_is_all_digits);
    RUN_TEST(test_the_warning_flags_exactly_the_names_the_bike_cannot_find);
    RUN_TEST(test_every_derived_identity_passes_its_own_prefix_rule);
    RUN_TEST(test_defaults_carry_no_identity_until_resolved);
    RUN_TEST(test_an_absent_identity_is_derived_from_the_mac_and_reported_as_default);
    RUN_TEST(test_a_stored_identity_is_left_exactly_as_stored);
    RUN_TEST(test_a_deliberate_real_crank_id_for_a_crank_rescue_is_not_overridden);
    RUN_TEST(test_a_legacy_line_with_no_identity_field_derives_one);
    RUN_TEST(test_a_corrector_with_no_name_gets_our_own_honest_name_not_a_stages_id);
    RUN_TEST(test_an_unresolved_config_round_trips_as_still_unresolved);
    return UNITY_END();
}
