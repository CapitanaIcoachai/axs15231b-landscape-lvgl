/*
 * Minimal lv_conf.h for LVGL v8.x used by this example.
 *
 * INSTALLATION:
 *   1. Install "lvgl" 8.x via the Arduino Library Manager.
 *   2. Copy this file next to the LVGL library folder, i.e. place it at
 *          <Arduino>/libraries/lv_conf.h
 *      (one level ABOVE the lvgl/ folder), OR keep LV_CONF_INCLUDE_SIMPLE
 *      and ensure this file is on the include path.
 *   3. LV_CONF_SKIP must remain 0 (default) so LVGL picks up this config.
 *
 * Only the options relevant to this demo are set; everything else uses
 * the LVGL defaults.
 */
#if 1 /* Set this to "1" to enable content */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Color depth: 16-bit (RGB565) to match the AXS15231B panel */
#define LV_COLOR_DEPTH 16

/* The panel expects the native RGB565 byte order for this Canvas setup.
 * If your colors look wrong (e.g. red/blue swapped), toggle this to 1. */
#define LV_COLOR_16_SWAP 0

/* Memory: LVGL manages its own heap for UI objects */
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (48U * 1024U)

/* Tick source: use Arduino millis() so we don't need lv_tick_inc() calls */
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

/* A default font so labels render out of the box */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* Default (simple) theme */
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

#endif /* LV_CONF_H */

#endif /* End of "Content enable" */
