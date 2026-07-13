#pragma once
// LcdTheme.h — the LVGL on-device colour palette, GENERATED from design/tokens.json.
//
// The single source of truth for the UI palette is design/tokens.json. Run
// `python design/gen_tokens.py` to regenerate the block below; CI (code/tests/test_tokens_sync.py)
// fails if it drifts. DO NOT hand-edit between the TOKENS-GEN markers.
//
// This is the LVGL sibling of the RGB565 block in lib/proxy/LcdCanvas.h and the web `:root` blocks:
// the on-device LVGL UI (src/ui/LvglUi.cpp) shares this one palette via `using namespace theme;`
// instead of hand-duplicating the colours (which had drifted into a 4th copy).
#include <lvgl.h>

namespace sb20proxy {
namespace theme {

// TOKENS-GEN:START (design/gen_tokens.py) — generated from design/tokens.json; do not edit by hand
inline lv_color_t C_BG()      { return lv_color_hex(0x0f1320); }  // --bg
inline lv_color_t C_CARD()    { return lv_color_hex(0x1a2030); }  // --card
inline lv_color_t C_FG()      { return lv_color_hex(0xe8ecf4); }  // --fg
inline lv_color_t C_MUT()     { return lv_color_hex(0x8b93a7); }  // --mut
inline lv_color_t C_OK()      { return lv_color_hex(0x22c55e); }  // --ok
inline lv_color_t C_ACCENT()  { return lv_color_hex(0x3b82f6); }  // --accent
inline lv_color_t C_BAD()     { return lv_color_hex(0xef4444); }  // --bad
inline lv_color_t C_LINE()    { return lv_color_hex(0x1c2334); }  // --line
inline lv_color_t C_CHIP2()   { return lv_color_hex(0x2a3142); }  // --chip2
inline lv_color_t C_TITLE()   { return lv_color_hex(0x151d2e); }  // --title
// TOKENS-GEN:END

}  // namespace theme
}  // namespace sb20proxy
