// Host tests for the OpenBikeControl completion (#367): every standard button id, the app->device
// decoders (Haptic 0x03, App Information 0x04) with the spec's own example bytes, TCP framing, the
// per-link AppInfo store, the fan-out queue, multi-action bindings (and that old saved bindings load
// unchanged), the per-board OBC name + id, the mDNS TXT record, and the spec's "never coalesce" MUST.
//
// Every byte vector below is copied from the OBC spec's examples (PROTOCOL.md, BLE.md, MDNS.md at
// OpenBikeControl/openbikecontrol-protocol, protocol version 1) or from the real session-3 SB20
// shifter capture (shifter-ble-protocol.md). None is invented.
#include <unity.h>

#include <string>
#include <vector>

#include "FleetIdentity.h"
#include "Obc.h"
#include "ObcApp.h"
#include "ObcShifterSource.h"
#include "RuntimeConfig.h"
#include "Sb20ButtonMap.h"

using namespace sb20proxy;

void setUp() {}
void tearDown() {}

namespace {

typedef std::vector<std::vector<uint8_t> > Msgs;

// ---- button ids ----------------------------------------------------------------------------------

void test_catalog_has_every_spec_id() {
    // PROTOCOL.md "Standard Button IDs", every row.
    const uint8_t spec[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                            0x18, 0x19, 0x1A, 0x20, 0x21, 0x24, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36,
                            0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x40, 0x44, 0x45, 0x46, 0x50, 0x51, 0x52};
    size_t n = 0;
    const ObcButtonInfo* c = obcButtonCatalog(n);
    TEST_ASSERT_EQUAL_INT((int)sizeof(spec), (int)n);
    for (size_t i = 0; i < sizeof(spec); ++i) {
        TEST_ASSERT_NOT_NULL_MESSAGE(obcButtonInfo(spec[i]), "spec id missing from the catalog");
        TEST_ASSERT_EQUAL_UINT8(spec[i], c[i].id);  // id order
    }
    // Unassigned / custom ranges are not in the standard table.
    TEST_ASSERT_NULL(obcButtonInfo(0x41));
    TEST_ASSERT_NULL(obcButtonInfo(0x60));
    TEST_ASSERT_NULL(obcButtonInfo(0x80));
    // Only the gear-selection ids are not clickable (0x01 is a no-op for them by spec).
    for (size_t i = 0; i < n; ++i) {
        const bool gearSel = c[i].id >= 0x03 && c[i].id <= 0x05;
        TEST_ASSERT_EQUAL(!gearSel, c[i].clickable);
    }
    // Tokens are unique (they are the binding's stored form).
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j) TEST_ASSERT_FALSE(std::string(c[i].token) == c[j].token);
}

void test_new_ids_have_their_spec_values() {
    TEST_ASSERT_EQUAL_HEX8(0x04, OBC_BTN_CHAINRING_SET);
    TEST_ASSERT_EQUAL_HEX8(0x05, OBC_BTN_CASSETTE_SET);
    TEST_ASSERT_EQUAL_HEX8(0x17, OBC_BTN_HOME);
    TEST_ASSERT_EQUAL_HEX8(0x1A, OBC_BTN_BRAKE);
    TEST_ASSERT_EQUAL_HEX8(0x21, OBC_BTN_PUSH_TO_TALK);
    TEST_ASSERT_EQUAL_HEX8(0x24, OBC_BTN_SCREENSHOT);
    TEST_ASSERT_EQUAL_HEX8(0x36, OBC_BTN_PREV_INTERVAL);
    TEST_ASSERT_EQUAL_HEX8(0x37, OBC_BTN_U_TURN);
    TEST_ASSERT_EQUAL_HEX8(0x39, OBC_BTN_TAKE_BREAK);
    TEST_ASSERT_EQUAL_HEX8(0x3A, OBC_BTN_JOIN_RIDER);
    TEST_ASSERT_EQUAL_HEX8(0x3B, OBC_BTN_CHANGE_ROUTE);
    TEST_ASSERT_EQUAL_HEX8(0x3C, OBC_BTN_CRUISE);
    TEST_ASSERT_EQUAL_HEX8(0x40, OBC_BTN_CAMERA_VIEW);
    TEST_ASSERT_EQUAL_HEX8(0x44, OBC_BTN_HUD_TOGGLE);
    TEST_ASSERT_EQUAL_HEX8(0x45, OBC_BTN_MAP_TOGGLE);
    TEST_ASSERT_EQUAL_HEX8(0x46, OBC_BTN_SPECTATE);
    TEST_ASSERT_EQUAL_HEX8(0x50, OBC_BTN_POWERUP_1);
    TEST_ASSERT_EQUAL_HEX8(0x51, OBC_BTN_POWERUP_2);
    TEST_ASSERT_EQUAL_HEX8(0x52, OBC_BTN_POWERUP_3);
}

void test_multi_action_click_matches_spec_example() {
    // PROTOCOL.md "Multiple Actions Per Button": [0x01, 0x01, 0x01, 0x30, 0x01, 0x14, 0x01].
    Msgs msgs;
    const uint8_t ids[] = {OBC_BTN_SHIFT_UP, OBC_BTN_ERG_UP, OBC_BTN_SELECT};
    emitObcClicks(ids, 3, [&](const uint8_t* d, size_t n) { msgs.push_back(std::vector<uint8_t>(d, d + n)); });
    TEST_ASSERT_EQUAL_INT(2, (int)msgs.size());
    const uint8_t press[] = {0x01, 0x01, 0x01, 0x30, 0x01, 0x14, 0x01};
    const uint8_t release[] = {0x01, 0x01, 0x00, 0x30, 0x00, 0x14, 0x00};
    TEST_ASSERT_EQUAL_INT(7, (int)msgs[0].size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(press, msgs[0].data(), 7);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(release, msgs[1].data(), 7);
}

void test_device_status_not_battery_powered() {
    // MDNS.md: "Device without battery monitoring, connected" -> [0x02, 0xFF, 0x01].
    uint8_t out[OBC_MAX_MSG];
    const uint8_t want[] = {0x02, 0xFF, 0x01};
    TEST_ASSERT_EQUAL_INT(3, (int)encodeDeviceStatus(OBC_BATTERY_NA, true, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, out, 3);
}

// ---- app -> device decoders --------------------------------------------------------------------

void test_haptic_spec_examples() {
    ObcHaptic h;
    const uint8_t shortDefault[] = {0x03, 0x01, 0x00, 0x00};
    TEST_ASSERT_TRUE(decodeHaptic(shortDefault, 4, h));
    TEST_ASSERT_EQUAL_UINT8(0x01, h.pattern);
    TEST_ASSERT_EQUAL_UINT8(0x00, h.duration10ms);
    TEST_ASSERT_EQUAL_UINT8(0x00, h.intensity);
    const uint8_t dbl[] = {0x03, 0x02, 0x14, 0x80};  // double pulse, 200 ms, intensity 128
    TEST_ASSERT_TRUE(decodeHaptic(dbl, 4, h));
    TEST_ASSERT_EQUAL_UINT8(0x02, h.pattern);
    TEST_ASSERT_EQUAL_UINT8(0x14, h.duration10ms);
    TEST_ASSERT_EQUAL_UINT8(0x80, h.intensity);
    const uint8_t success[] = {0x03, 0x05, 0x00, 0xFF};
    TEST_ASSERT_TRUE(decodeHaptic(success, 4, h));
    TEST_ASSERT_EQUAL_UINT8(0x05, h.pattern);
    TEST_ASSERT_EQUAL_UINT8(0xFF, h.intensity);
    const uint8_t stop[] = {0x03, 0x00, 0x00, 0x00};
    TEST_ASSERT_TRUE(decodeHaptic(stop, 4, h));
    TEST_ASSERT_EQUAL_UINT8(0x00, h.pattern);
    // Wrong type / short -> rejected.
    TEST_ASSERT_FALSE(decodeHaptic(shortDefault, 3, h));
    const uint8_t notHaptic[] = {0x04, 0x01, 0x00, 0x00};
    TEST_ASSERT_FALSE(decodeHaptic(notHaptic, 4, h));
    TEST_ASSERT_FALSE(decodeHaptic(nullptr, 4, h));
}

// BLE.md / MDNS.md App Information example: "zwift", "1.52.0", buttons [0x01, 0x02, 0x10, 0x14].
const uint8_t kZwiftAppInfo[] = {0x04, 0x01, 0x05, 'z', 'w', 'i', 'f', 't', 0x06, '1', '.',
                                 '5',  '2',  '.',  '0', 0x04, 0x01, 0x02, 0x10, 0x14};

void test_app_info_spec_example() {
    TEST_ASSERT_EQUAL_INT((int)sizeof(kZwiftAppInfo), (int)obcAppInfoLength(kZwiftAppInfo, sizeof(kZwiftAppInfo)));
    ObcAppInfo a;
    a.clear();
    TEST_ASSERT_TRUE(decodeAppInfo(kZwiftAppInfo, sizeof(kZwiftAppInfo), a));
    TEST_ASSERT_EQUAL_UINT8(1, a.version);
    TEST_ASSERT_EQUAL_STRING("zwift", a.appId);
    TEST_ASSERT_EQUAL_STRING("1.52.0", a.appVersion);
    TEST_ASSERT_EQUAL_UINT8(4, a.buttonCount);
    TEST_ASSERT_FALSE(a.allButtons());
    TEST_ASSERT_TRUE(a.supports(0x01));
    TEST_ASSERT_TRUE(a.supports(0x02));
    TEST_ASSERT_TRUE(a.supports(0x10));
    TEST_ASSERT_TRUE(a.supports(0x14));
    TEST_ASSERT_FALSE(a.supports(0x30));
}

void test_app_info_edges() {
    ObcAppInfo a;
    a.clear();
    TEST_ASSERT_TRUE(a.supports(0x30));  // no AppInfo -> assume every button (BLE.md)
    // Every truncation of the spec example is "not yet / invalid", never a misparse.
    for (size_t n = 0; n < sizeof(kZwiftAppInfo); ++n) TEST_ASSERT_FALSE(decodeAppInfo(kZwiftAppInfo, n, a));
    // count 0 = all buttons; empty strings are legal.
    const uint8_t all[] = {0x04, 0x01, 0x00, 0x00, 0x00};
    TEST_ASSERT_TRUE(decodeAppInfo(all, sizeof(all), a));
    TEST_ASSERT_TRUE(a.allButtons());
    TEST_ASSERT_TRUE(a.supports(0x52));
    TEST_ASSERT_EQUAL_STRING("", a.appId);
    // version 0 is not a format version.
    const uint8_t v0[] = {0x04, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_FALSE(decodeAppInfo(v0, sizeof(v0), a));
    // A 40-byte id (over the spec's 32) is truncated to 32 on store, framing still holds; quotes and
    // control bytes are made safe for logs / JSON.
    std::vector<uint8_t> longId;
    longId.push_back(0x04);
    longId.push_back(0x01);
    longId.push_back(40);
    for (int i = 0; i < 40; ++i) longId.push_back(i == 0 ? '"' : (i == 1 ? 0x07 : 'a'));
    longId.push_back(0x00);  // empty version
    longId.push_back(0x01);
    longId.push_back(0x30);
    TEST_ASSERT_TRUE(decodeAppInfo(longId.data(), longId.size(), a));
    TEST_ASSERT_EQUAL_INT(32, (int)std::string(a.appId).size());
    TEST_ASSERT_EQUAL_CHAR('?', a.appId[0]);
    TEST_ASSERT_EQUAL_CHAR('?', a.appId[1]);
    TEST_ASSERT_TRUE(a.supports(0x30));
    TEST_ASSERT_FALSE(a.supports(0x31));
}

// ---- TCP framing + per-link state ---------------------------------------------------------------

void test_stream_parser_frames_split_and_glued_messages() {
    ObcStreamParser p;
    Msgs got;
    const auto sink = [&](const uint8_t* d, size_t n) { got.push_back(std::vector<uint8_t>(d, d + n)); };
    // Glued: AppInfo + Haptic in one segment.
    std::vector<uint8_t> stream(kZwiftAppInfo, kZwiftAppInfo + sizeof(kZwiftAppInfo));
    const uint8_t hap[] = {0x03, 0x02, 0x14, 0x80};
    stream.insert(stream.end(), hap, hap + 4);
    p.feed(stream.data(), stream.size(), sink);
    TEST_ASSERT_EQUAL_INT(2, (int)got.size());
    TEST_ASSERT_EQUAL_INT((int)sizeof(kZwiftAppInfo), (int)got[0].size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kZwiftAppInfo, got[0].data(), sizeof(kZwiftAppInfo));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(hap, got[1].data(), 4);
    // Split: the same bytes one at a time.
    got.clear();
    for (size_t i = 0; i < stream.size(); ++i) p.feed(&stream[i], 1, sink);
    TEST_ASSERT_EQUAL_INT(2, (int)got.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kZwiftAppInfo, got[0].data(), sizeof(kZwiftAppInfo));
    // Noise and device->app types are skipped byte-wise, then framing resumes.
    got.clear();
    const uint8_t noisy[] = {0x01, 0x01, 0x01, 0x02, 0xFF, 0x01, 0x03, 0x00, 0x00, 0x00};
    p.feed(noisy, sizeof(noisy), sink);
    TEST_ASSERT_EQUAL_INT(1, (int)got.size());
    TEST_ASSERT_EQUAL_UINT8(0x03, got[0][0]);
    TEST_ASSERT_EQUAL_INT(6, (int)p.skipped());
    TEST_ASSERT_EQUAL_INT(5, (int)p.messages());
}

void test_stream_parser_handles_the_longest_app_info() {
    // Every length byte at 255: 3 + 255 + 1 + 255 + 1 + 255 = 770 = kCap.
    std::vector<uint8_t> m;
    m.push_back(0x04);
    m.push_back(0x01);
    m.push_back(255);
    m.insert(m.end(), 255, 'a');
    m.push_back(255);
    m.insert(m.end(), 255, 'b');
    m.push_back(255);
    for (int i = 0; i < 255; ++i) m.push_back((uint8_t)i);
    TEST_ASSERT_EQUAL_INT((int)ObcStreamParser::kCap, (int)m.size());
    ObcStreamParser p;
    int n = 0;
    p.feed(m.data(), m.size(), [&](const uint8_t* d, size_t len) {
        ++n;
        ObcAppInfo a;
        TEST_ASSERT_TRUE(decodeAppInfo(d, len, a));
        TEST_ASSERT_EQUAL_INT(32, (int)std::string(a.appId).size());
        TEST_ASSERT_TRUE(a.supports(0x00));
        TEST_ASSERT_TRUE(a.supports(0xFE));
        TEST_ASSERT_FALSE(a.supports(0xFF));
    });
    TEST_ASSERT_EQUAL_INT(1, n);
}

void test_app_state_per_link() {
    ObcAppState s;
    TEST_ASSERT_FALSE(s.hasAppInfo());
    TEST_ASSERT_TRUE(s.appInfo().supports(0x30));  // absent -> all buttons
    TEST_ASSERT_EQUAL_INT((int)ObcAppMsg::AppInfo, (int)s.onMessage(7, kZwiftAppInfo, sizeof(kZwiftAppInfo)));
    TEST_ASSERT_TRUE(s.hasAppInfo());
    TEST_ASSERT_EQUAL_STRING("zwift", s.appInfo().appId);
    TEST_ASSERT_EQUAL_INT(7, s.appInfoLink());
    const uint8_t hap[] = {0x03, 0x01, 0x00, 0x00};
    TEST_ASSERT_EQUAL_INT((int)ObcAppMsg::Haptic, (int)s.onMessage(3, hap, 4));
    TEST_ASSERT_EQUAL_INT(1, (int)s.hapticCount());
    TEST_ASSERT_EQUAL_UINT8(0x01, s.lastHaptic().pattern);
    const uint8_t junk[] = {0x01, 0x01, 0x01};
    TEST_ASSERT_EQUAL_INT((int)ObcAppMsg::Ignored, (int)s.onMessage(3, junk, 3));
    // Another link dropping (e.g. the SB20 itself) keeps this app's info...
    s.onDisconnect(3);
    TEST_ASSERT_TRUE(s.hasAppInfo());
    // ...the app's own link dropping empties it (BLE.md: emptied on every disconnect).
    s.onDisconnect(7);
    TEST_ASSERT_FALSE(s.hasAppInfo());
    TEST_ASSERT_EQUAL_STRING("", s.appInfo().appId);
}

// ---- the spec's MUST: every physical press is its own press/release ---------------------------

void test_rapid_presses_are_never_coalesced() {
    // Five rapid presses of LEFT up bound to ERG Up (0x30), back to back with no time passing: the real
    // session-3 frames (held 01 00 01 00, terminator 04 00 01 00). Five presses -> five PRESSED and five
    // RELEASED messages, strictly alternating, each through the fan-out queue unchanged.
    ObcShifterSource src;
    src.setBindings(Sb20ButtonMap::fromString("erg_up,erg_down,lap,shift_up,shift_down,menu"));
    ObcOutbox<32> box;
    const uint8_t held[] = {0x01, 0x00, 0x01, 0x00};
    const uint8_t term[] = {0x04, 0x00, 0x01, 0x00};
    for (int press = 0; press < 5; ++press) {
        src.feed(held, 4, [&](const uint8_t* d, size_t n) { TEST_ASSERT_TRUE(box.push(d, n)); },
                 [](int8_t) {});
        src.feed(held, 4, [&](const uint8_t* d, size_t n) { box.push(d, n); }, [](int8_t) {});  // repeat
        src.feed(term, 4, [&](const uint8_t* d, size_t n) { box.push(d, n); }, [](int8_t) {});
    }
    TEST_ASSERT_EQUAL_INT(10, (int)box.size());
    for (int i = 0; i < 10; ++i) {
        uint8_t out[OBC_MAX_MSG];
        size_t n = 0;
        TEST_ASSERT_TRUE(box.pop(out, n));
        const uint8_t want[] = {0x01, 0x30, (uint8_t)(i % 2 == 0 ? 0x01 : 0x00)};
        TEST_ASSERT_EQUAL_INT(3, (int)n);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(want, out, 3);
    }
    TEST_ASSERT_EQUAL_INT(0, (int)box.dropped());
}

void test_outbox_fifo_and_overflow_refuses_new() {
    ObcOutbox<4> box;
    for (uint8_t i = 1; i <= 5; ++i) {
        const uint8_t m[] = {0x01, i, 0x01};
        TEST_ASSERT_EQUAL(i <= 4, box.push(m, 3));
    }
    TEST_ASSERT_EQUAL_INT(1, (int)box.dropped());
    uint8_t out[OBC_MAX_MSG];
    size_t n = 0;
    for (uint8_t i = 1; i <= 4; ++i) {
        TEST_ASSERT_TRUE(box.pop(out, n));
        TEST_ASSERT_EQUAL_UINT8(i, out[1]);  // oldest first; the 5th was the one refused
    }
    TEST_ASSERT_FALSE(box.pop(out, n));
    const uint8_t tooLong[OBC_MAX_MSG + 1] = {0x01};
    TEST_ASSERT_FALSE(box.push(tooLong, sizeof(tooLong)));
}

// ---- multi-action bindings ---------------------------------------------------------------------

void test_multi_action_binding_emits_one_combined_click() {
    ObcShifterSource src;
    // LEFT up: Shift Up + ERG Up + a local +10 W nudge; RIGHT up: Take a break (catalog-only token).
    src.setBindings(Sb20ButtonMap::fromString("shift_up+erg_up+bias_up,none,lap,take_break,none,none"));
    Msgs msgs;
    int erg = 0;
    const auto emit = [&](const uint8_t* d, size_t n) { msgs.push_back(std::vector<uint8_t>(d, d + n)); };
    const auto onErg = [&](int8_t d) { erg += d; };
    const uint8_t leftUp[] = {0x01, 0x00, 0x01, 0x00};
    src.feed(leftUp, 4, emit, onErg);
    TEST_ASSERT_EQUAL_INT(2, (int)msgs.size());
    const uint8_t p[] = {0x01, 0x01, 0x01, 0x30, 0x01};
    const uint8_t r[] = {0x01, 0x01, 0x00, 0x30, 0x00};
    TEST_ASSERT_EQUAL_INT(5, (int)msgs[0].size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(p, msgs[0].data(), 5);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(r, msgs[1].data(), 5);
    TEST_ASSERT_EQUAL_INT(10, erg);
    msgs.clear();
    const uint8_t rightUp[] = {0x01, 0x00, 0x08, 0x00};
    src.feed(rightUp, 4, emit, onErg);
    const uint8_t tb[] = {0x01, 0x39, 0x01};
    TEST_ASSERT_EQUAL_INT(2, (int)msgs.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tb, msgs[0].data(), 3);
}

void test_training_controls_are_bindable() {
    const char* tokens[] = {"erg_up", "erg_down", "skip_interval", "pause", "resume", "lap", "prev_interval",
                            "u_turn", "change_mode", "take_break", "join_rider", "change_route",
                            "cruise_control"};
    for (uint8_t i = 0; i < 13; ++i) {
        const Sb20ActionSpec s = sb20SpecForToken(tokens[i]);
        TEST_ASSERT_EQUAL_INT((int)Sb20ActionKind::Obc, (int)s.kind);
        TEST_ASSERT_EQUAL_HEX8(0x30 + i, s.obcId);
    }
    // Gear selection is not a momentary press -> not bindable.
    TEST_ASSERT_EQUAL_INT((int)Sb20ActionKind::None, (int)sb20SpecForToken("gear_set").kind);
    TEST_ASSERT_EQUAL_INT((int)Sb20ActionKind::None, (int)sb20SpecForToken("bogus").kind);
}

void test_slot_cap_and_resolve_first() {
    Sb20ButtonMap m = Sb20ButtonMap::fromString("erg_up+shift_up,,,,,");
    TEST_ASSERT_EQUAL_UINT8(OBC_BTN_ERG_UP, m.resolve(ShifterButton::LeftUp).obcId);  // first action
    Sb20ActionSpec specs[kSb20MaxActionsPerButton];
    TEST_ASSERT_EQUAL_INT(2, (int)m.resolveAll(ShifterButton::LeftUp, specs, kSb20MaxActionsPerButton));
    // More than nine actions: resolution stops at nine (one Button-State message's worth).
    m.token[0] = "nav_up+nav_down+nav_left+nav_right+select+back+menu+home+lap+pause+resume";
    TEST_ASSERT_EQUAL_INT(9, (int)m.resolveAll(ShifterButton::LeftUp, specs, kSb20MaxActionsPerButton));
}

void test_old_saved_bindings_load_unchanged() {
    // A binding saved before multi-action existed (one token per slot) is the same string after a
    // load/save round trip through the NVS line, and resolves to the same single actions.
    const std::string legacy = "bias_up,bias_down,lap,erg_up,erg_down,none";
    const std::string line = "v2|aa:bb:cc:dd:ee:ff|ASSIOMA|0|Stages 62145|SER|0||||0||1|21587|0|1|" + legacy + "|0";
    RuntimeConfig c = RuntimeConfig::fromLine(line);
    TEST_ASSERT_EQUAL_STRING(legacy.c_str(), c.obcButtons.toString().c_str());
    TEST_ASSERT_EQUAL_STRING(line.c_str(), c.toLine().c_str());  // byte-identical re-save
    Sb20ActionSpec s[kSb20MaxActionsPerButton];
    TEST_ASSERT_EQUAL_INT(1, (int)c.obcButtons.resolveAll(ShifterButton::LeftUp, s, kSb20MaxActionsPerButton));
    TEST_ASSERT_EQUAL_INT((int)Sb20ActionKind::ErgBias, (int)s[0].kind);
    TEST_ASSERT_EQUAL_INT(1, (int)c.obcButtons.resolveAll(ShifterButton::RightUp, s, kSb20MaxActionsPerButton));
    TEST_ASSERT_EQUAL_UINT8(OBC_BTN_ERG_UP, s[0].obcId);
    TEST_ASSERT_EQUAL_INT((int)Sb20ActionKind::None, (int)c.obcButtons.resolve(ShifterButton::Right3).kind);
    // The shipped default and its wire indices are unchanged.
    uint8_t idx[6];
    Sb20ButtonMap::defaults().toIndices(idx);
    const uint8_t wantIdx[] = {1, 2, 5, 1, 2, 6};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(wantIdx, idx, 6);
    // And a multi-action binding survives the NVS line too.
    c.obcButtons = Sb20ButtonMap::fromString("shift_up+erg_up,shift_down+erg_down,take_break,none,none,menu");
    const RuntimeConfig back = RuntimeConfig::fromLine(c.toLine());
    TEST_ASSERT_EQUAL_STRING(c.obcButtons.toString().c_str(), back.obcButtons.toString().c_str());
}

void test_buttons_json_tokens() {
    const Sb20ButtonMap m = Sb20ButtonMap::fromString("shift_up+erg_up,none,take_break,lap,none,menu");
    const std::string js = buttonsToJson(true, m);
    // "actions" stays the indexed first action (today's SPA); "tokens" is the full binding.
    TEST_ASSERT_EQUAL_STRING(
        "{\"enabled\":true,\"actions\":[1,0,0,5,0,6],"
        "\"tokens\":[\"shift_up+erg_up\",\"none\",\"take_break\",\"lap\",\"none\",\"menu\"]}",
        js.c_str());
    bool en = false;
    Sb20ButtonMap back;
    TEST_ASSERT_TRUE(buttonsFromJson(js, en, back));
    TEST_ASSERT_TRUE(en);
    TEST_ASSERT_EQUAL_STRING(m.toString().c_str(), back.toString().c_str());
    // Today's SPA body (actions only) behaves exactly as before.
    Sb20ButtonMap a;
    TEST_ASSERT_TRUE(buttonsFromJson("{\"enabled\":false,\"actions\":[3,4,0,0,0,0]}", en, a));
    TEST_ASSERT_FALSE(en);
    TEST_ASSERT_EQUAL_STRING("erg_up,erg_down,none,none,none,none", a.toString().c_str());
    // Tokens are sanitised: no delimiter or quote can reach the NVS line.
    Sb20ButtonMap t;
    TEST_ASSERT_TRUE(buttonsFromJson("{\"enabled\":true,\"tokens\":[\"erg_up|x,y\",\"\"]}", en, t));
    TEST_ASSERT_EQUAL_STRING("erg_upxy,none,none,none,none,none", t.toString().c_str());
    TEST_ASSERT_FALSE(buttonsFromJson("{\"enabled\":true}", en, t));
}

// ---- identity + TXT --------------------------------------------------------------------------

constexpr uint8_t kS3Mac[6] = {0xA4, 0xCB, 0x8F, 0xDA, 0xE9, 0xCC};     // BOARDS.md, 0xE9CC -> 9852
constexpr uint8_t kC3o96Mac[6] = {0x10, 0xB4, 0x1D, 0xBA, 0xC9, 0x0C};  // 0xC90C -> 1468

void test_obc_name_and_id_per_board() {
    TEST_ASSERT_EQUAL_STRING("OBC-SB20-9852", defaultObcName(kS3Mac).c_str());
    TEST_ASSERT_EQUAL_STRING("OBC-SB20-1468", defaultObcName(kC3o96Mac).c_str());
    // Same digits as the board's derived crank name, so the two read as one board.
    TEST_ASSERT_EQUAL_STRING("Stages 99852", defaultSpoofName(kS3Mac).c_str());
    TEST_ASSERT_EQUAL_STRING("a4cb8fdae9cc", obcDeviceId(kS3Mac).c_str());
    TEST_ASSERT_EQUAL_STRING("10b41dbac90c", obcDeviceId(kC3o96Mac).c_str());
    // Still "OBC-"-prefixed (our qz fork's name matcher) and short enough for the primary advert.
    TEST_ASSERT_EQUAL_INT(0, (int)defaultObcName(kS3Mac).find("OBC-"));
    TEST_ASSERT_TRUE(defaultObcName(kS3Mac).size() <= 20);
}

void test_txt_record_has_every_required_key() {
    const std::vector<std::pair<std::string, std::string> > t = obcTxtRecord("OBC-SB20-9852", "a4cb8fdae9cc");
    const char* keys[] = {"version", "id", "name", "service-uuids", "manufacturer", "model"};  // MDNS.md
    TEST_ASSERT_EQUAL_INT(6, (int)t.size());
    for (int i = 0; i < 6; ++i) TEST_ASSERT_EQUAL_STRING(keys[i], t[i].first.c_str());
    TEST_ASSERT_EQUAL_STRING("1", t[0].second.c_str());
    TEST_ASSERT_EQUAL_STRING("a4cb8fdae9cc", t[1].second.c_str());
    TEST_ASSERT_EQUAL_STRING("OBC-SB20-9852", t[2].second.c_str());
    TEST_ASSERT_EQUAL_STRING("d273f680-d548-419d-b9d1-fa0472345229", t[3].second.c_str());
    TEST_ASSERT_FALSE(t[4].second.empty());
    TEST_ASSERT_FALSE(t[5].second.empty());
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_catalog_has_every_spec_id);
    RUN_TEST(test_new_ids_have_their_spec_values);
    RUN_TEST(test_multi_action_click_matches_spec_example);
    RUN_TEST(test_device_status_not_battery_powered);
    RUN_TEST(test_haptic_spec_examples);
    RUN_TEST(test_app_info_spec_example);
    RUN_TEST(test_app_info_edges);
    RUN_TEST(test_stream_parser_frames_split_and_glued_messages);
    RUN_TEST(test_stream_parser_handles_the_longest_app_info);
    RUN_TEST(test_app_state_per_link);
    RUN_TEST(test_rapid_presses_are_never_coalesced);
    RUN_TEST(test_outbox_fifo_and_overflow_refuses_new);
    RUN_TEST(test_multi_action_binding_emits_one_combined_click);
    RUN_TEST(test_training_controls_are_bindable);
    RUN_TEST(test_slot_cap_and_resolve_first);
    RUN_TEST(test_old_saved_bindings_load_unchanged);
    RUN_TEST(test_buttons_json_tokens);
    RUN_TEST(test_obc_name_and_id_per_board);
    RUN_TEST(test_txt_record_has_every_required_key);
    return UNITY_END();
}
