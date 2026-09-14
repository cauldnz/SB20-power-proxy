/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "esp_err.h"
#include "esp_lcd_types.h"
#include "soc/soc_caps.h"

// SB20: upstream injects these via CMake (target_compile_definitions from the component version).
// This PlatformIO manifest build has no CMake, so define them here — they're only used in the
// driver's "LCD panel create success, version: x.y.z" log line. (Vendored component v1.0.1.)
#ifndef ESP_LCD_AXS15231B_VER_MAJOR
#define ESP_LCD_AXS15231B_VER_MAJOR 1
#define ESP_LCD_AXS15231B_VER_MINOR 0
#define ESP_LCD_AXS15231B_VER_PATCH 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if SOC_MIPI_DSI_SUPPORTED
/**
 * @brief  Initialize AXS15231B LCD panel with MIPI interface
 *
 * @param[in]  io LCD panel IO handle
 * @param[in]  panel_dev_config LCD panel device configuration
 * @param[out] ret_panel LCD panel handle
 * @return
 *      - ESP_OK:    Success
 *      - Otherwise: Fail
 */
esp_err_t esp_lcd_new_panel_axs15231b_mipi(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config,
                                           esp_lcd_panel_handle_t *ret_panel);
#endif

#ifdef __cplusplus
}
#endif
