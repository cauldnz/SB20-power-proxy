// Host tests for the pure ANT+ Controls -> OBC decoder (pio test -e native). Vectors are the STANDARD
// Generic Command page format (public ANT+ Controls profile / openant reference), not invented AXS
// bytes — the AXS physical-button->command binding is verified separately on an on-air capture.
#include <unity.h>

#include <vector>

#include "AntControlsSource.h"

using namespace sb20proxy;

void setUp() {}
void tearDown() {}

// Build a Generic Command page: [0x49, serialLo, serialHi, mfgLo, mfgHi, seq, cmdLo, cmdHi].
static void mkPage(uint8_t* p, uint16_t serial, uint16_t mfg, uint8_t seq, uint16_t cmd) {
    p[0] = 0x49;
    p[1] = serial & 0xFF; p[2] = serial >> 8;
    p[3] = mfg & 0xFF;    p[4] = mfg >> 8;
    p[5] = seq;
    p[6] = cmd & 0xFF;    p[7] = cmd >> 8;
}

static void test_decode_generic_command() {
    uint8_t p[8];
    mkPage(p, 0x1234, 0x00AB, 7, (uint16_t)AntControlCmd::MenuDown);
    auto e = decodeAntControlPage(p, 8);
    TEST_ASSERT_TRUE(e.valid);
    TEST_ASSERT_EQUAL(1, (int)e.cmd);   // MenuDown = 1
    TEST_ASSERT_EQUAL(7, e.seq);
    TEST_ASSERT_EQUAL_HEX16(0x1234, e.serial);
    TEST_ASSERT_EQUAL_HEX16(0x00AB, e.mfg);
}

static void test_rejects_non_command_page() {
    uint8_t p[8] = {0x10, 0, 0, 0, 0, 0, 0, 0};   // not 0x49
    TEST_ASSERT_FALSE(decodeAntControlPage(p, 8).valid);
    TEST_ASSERT_FALSE(decodeAntControlPage(nullptr, 8).valid);
    TEST_ASSERT_FALSE(decodeAntControlPage(p, 4).valid);   // too short
}

static void test_command_to_obc_mapping() {
    TEST_ASSERT_EQUAL_HEX8(OBC_BTN_NAV_UP, antControlToObc(AntControlCmd::MenuUp));
    TEST_ASSERT_EQUAL_HEX8(OBC_BTN_LAP, antControlToObc(AntControlCmd::Lap));
    TEST_ASSERT_EQUAL_HEX8(OBC_BTN_SELECT, antControlToObc(AntControlCmd::MenuSelect));
    TEST_ASSERT_EQUAL_HEX8(0, antControlToObc(AntControlCmd::NoCommand));   // 0 = unmapped
    TEST_ASSERT_EQUAL_HEX8(0, antControlToObc(AntControlCmd::Reset));
}

static void test_feed_emits_stateless_click() {
    AntControlsSource src;
    std::vector<std::vector<uint8_t>> msgs;
    auto emit = [&](const uint8_t* m, size_t n) { msgs.emplace_back(m, m + n); };
    uint8_t p[8];
    mkPage(p, 1, 1, 3, (uint16_t)AntControlCmd::MenuUp);
    src.feed(p, 8, emit);
    TEST_ASSERT_EQUAL(2, (int)msgs.size());                    // PRESSED then RELEASED
    TEST_ASSERT_EQUAL_HEX8(OBC_MSG_BUTTON_STATE, msgs[0][0]);
    TEST_ASSERT_EQUAL_HEX8(OBC_BTN_NAV_UP, msgs[0][1]);
    TEST_ASSERT_EQUAL_HEX8(OBC_STATE_PRESSED, msgs[0][2]);
    TEST_ASSERT_EQUAL_HEX8(OBC_STATE_RELEASED, msgs[1][2]);
}

static void test_dedups_retransmits_by_sequence() {
    AntControlsSource src;
    int count = 0;
    auto emit = [&](const uint8_t*, size_t) { ++count; };
    uint8_t p[8];
    mkPage(p, 1, 1, 5, (uint16_t)AntControlCmd::Lap);
    src.feed(p, 8, emit);   // seq 5 -> 2 msgs
    src.feed(p, 8, emit);   // same seq 5 -> ignored (re-broadcast)
    TEST_ASSERT_EQUAL(2, count);
    mkPage(p, 1, 1, 6, (uint16_t)AntControlCmd::MenuDown);
    src.feed(p, 8, emit);   // new seq 6 -> +2
    TEST_ASSERT_EQUAL(4, count);
}

static void test_nocommand_fires_nothing_but_tracks_seq() {
    AntControlsSource src;
    int count = 0;
    auto emit = [&](const uint8_t*, size_t) { ++count; };
    uint8_t p[8];
    mkPage(p, 1, 1, 9, (uint16_t)AntControlCmd::NoCommand);
    src.feed(p, 8, emit);
    TEST_ASSERT_EQUAL(0, count);
    TEST_ASSERT_EQUAL(9, src.lastSeq());   // still updates the dedup baseline
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_decode_generic_command);
    RUN_TEST(test_rejects_non_command_page);
    RUN_TEST(test_command_to_obc_mapping);
    RUN_TEST(test_feed_emits_stateless_click);
    RUN_TEST(test_dedups_retransmits_by_sequence);
    RUN_TEST(test_nocommand_fires_nothing_but_tracks_seq);
    return UNITY_END();
}
