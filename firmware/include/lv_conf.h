/**
 * lv_conf.h — LVGL v9 configuration for the SB20 head-units (CYD 240x320 + S3-Touch 172x320).
 *
 * Minimal-override style: lv_conf_internal.h supplies defaults for everything not defined here,
 * so this file only pins what matters to us:
 *   - RGB565 (the panels' native format; the SPI seams' writePixels() does the byte swap)
 *   - LVGL's builtin allocator with a bounded pool — the no-PSRAM CYD budgets ~48 KB for UI heap
 *     on top of a ~1/8-frame partial draw buffer (LVGL's standard low-RAM mode)
 *   - software rendering incl. the complex path (rounded corners / gradients per the HTML design)
 *   - the widgets the five screens use; theme off — we style everything from the design tokens
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (48 * 1024U)

#define LV_USE_OS LV_OS_NONE
#define LV_USE_DRAW_SW 1
#define LV_DRAW_SW_COMPLEX 1

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 0
#define LV_USE_ASSERT_MALLOC 0

/* Widgets the screens use (unused ones default off or cost nothing when unreferenced) */
#define LV_USE_LABEL 1
#define LV_USE_BUTTON 1
#define LV_USE_BAR 1
#define LV_USE_LINE 1
#define LV_USE_CHART 1

#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_SIMPLE 0

/* lv_font_conv emits compressed glyph bitmaps by default — without this they silently don't draw */
#define LV_USE_FONT_COMPRESSED 1

/* Default font: keep LVGL's builtin so internals always have one; the UI sets Inter per style */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#endif /* LV_CONF_H */
