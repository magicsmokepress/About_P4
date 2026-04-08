/**
 * lv_conf.h — LVGL 9.x configuration for CrowPanel ESP32-P4
 *
 * Tuned for the CrowPanel dashboard:
 *   - Chart widget enabled (RTT sparklines, NQS history)
 *   - Tabview enabled (Dashboard / Graphs / Events / Setup navigation)
 *   - Table enabled (event log, probe config)
 *   - Larger font range (48pt for NQS score, 12pt for fine details)
 *   - Dark theme default (network operations aesthetic)
 *   - 256KB LVGL heap (complex UI with multiple screens)
 *
 * INSTALLATION:
 *   Copy this file to your Arduino/libraries/ folder (next to the lvgl folder),
 *   NOT inside the lvgl folder itself.
 *
 *   Your folder structure should look like:
 *     Arduino/
 *       libraries/
 *         lv_conf.h          <── this file
 *         lvgl/
 *         ESP32_Display_Panel/
 */

#ifndef LV_CONF_H
#define LV_CONF_H

/* ── Core Settings ──────────────────────────────────────────────── */
#define LV_CONF_SKIP                0

/* Color depth: RGB565 — matches MIPI-DSI config, saves PSRAM */
#define LV_COLOR_DEPTH              16

/* Memory — 256KB for multi-screen UI */
#define LV_MEM_CUSTOM               0
#define LV_MEM_SIZE                 (256 * 1024)

#define LV_MEM_ADR                  0
#define LV_MEM_POOL_INCLUDE         <stdlib.h>
#define LV_MEM_POOL_ALLOC           malloc

/* ── Display ────────────────────────────────────────────────────── */
#define LV_HOR_RES_MAX              1024
#define LV_VER_RES_MAX              600
#define LV_DPI_DEF                  130

/* ── Tick ───────────────────────────────────────────────────────── */
#define LV_TICK_CUSTOM              1
#define LV_TICK_CUSTOM_INCLUDE      <Arduino.h>
#define LV_TICK_CUSTOM_SYS_TIME_EXPR  (millis())

/* ── Logging ────────────────────────────────────────────────────── */
#define LV_USE_LOG                  1
#define LV_LOG_LEVEL                LV_LOG_LEVEL_WARN   /* WARN + ERROR only */
#define LV_LOG_PRINTF               1                   /* Use Serial.printf */

/* ── GPU / Draw engine ──────────────────────────────────────────── */
#define LV_USE_DRAW_SW              1

/* ── Built-in Fonts ─────────────────────────────────────────────── */
/* Font requirements:
 *   48pt — Primary NQS score (readable from 2m away)
 *   36pt — Secondary NQS / large status
 *   24pt — RTT values, target names, time display
 *   20pt — Section headers
 *   16pt — Status text, timestamps
 *   14pt — Default body text
 *   12pt — Fine details, axis labels
 */
#define LV_FONT_MONTSERRAT_8        0
#define LV_FONT_MONTSERRAT_10       0
#define LV_FONT_MONTSERRAT_12       1
#define LV_FONT_MONTSERRAT_14       1
#define LV_FONT_MONTSERRAT_16       1
#define LV_FONT_MONTSERRAT_18       0
#define LV_FONT_MONTSERRAT_20       1
#define LV_FONT_MONTSERRAT_22       0
#define LV_FONT_MONTSERRAT_24       1
#define LV_FONT_MONTSERRAT_26       0
#define LV_FONT_MONTSERRAT_28       0
#define LV_FONT_MONTSERRAT_30       0
#define LV_FONT_MONTSERRAT_32       0
#define LV_FONT_MONTSERRAT_34       0
#define LV_FONT_MONTSERRAT_36       1
#define LV_FONT_MONTSERRAT_38       0
#define LV_FONT_MONTSERRAT_40       0
#define LV_FONT_MONTSERRAT_42       0
#define LV_FONT_MONTSERRAT_44       0
#define LV_FONT_MONTSERRAT_46       0
#define LV_FONT_MONTSERRAT_48       1

#define LV_FONT_DEFAULT             &lv_font_montserrat_14

/* ── Widgets ────────────────────────────────────────────────────── */
/* Core widgets */
#define LV_USE_LABEL                1
#define LV_USE_BUTTON               1
#define LV_USE_BUTTONMATRIX         1
#define LV_USE_IMAGE                1
#define LV_USE_LINE                 1
#define LV_USE_LED                  1
#define LV_USE_ARC                  1
#define LV_USE_BAR                  1
#define LV_USE_SLIDER               1
#define LV_USE_SWITCH               1
#define LV_USE_CHECKBOX             1
#define LV_USE_DROPDOWN             1
#define LV_USE_ROLLER               1
#define LV_USE_TEXTAREA             1
#define LV_USE_KEYBOARD             1
#define LV_USE_MSGBOX               1
#define LV_USE_SPINNER              1
#define LV_USE_ANIMIMG              1

/* Dashboard widgets (enabled vs. proof-of-concept sketches) */
#define LV_USE_CHART                1       /* RTT sparklines, NQS history graph */
#define LV_USE_TABLE                1       /* Event log, probe configuration */
#define LV_USE_TABVIEW              1       /* Dashboard/Graphs/Events/Setup nav */
#define LV_USE_MENU                 1       /* Setup menu hierarchy */
#define LV_USE_SCALE                1       /* NQS gauge/meter */

/* Not needed for this project */
#define LV_USE_CALENDAR             0
#define LV_USE_CANVAS               0
#define LV_USE_IMAGEBUTTON          0
#define LV_USE_LIST                 0       /* Use table instead for event log */
#define LV_USE_OBJID                0
#define LV_USE_SPAN                 0
#define LV_USE_SPINBOX              0
#define LV_USE_TILEVIEW             0
#define LV_USE_WIN                  0

/* ── Themes ─────────────────────────────────────────────────────── */
#define LV_USE_THEME_DEFAULT        1
#define LV_THEME_DEFAULT_DARK       1       /* Dark theme — NOC / operations aesthetic */

/* ── Layouts ────────────────────────────────────────────────────── */
#define LV_USE_FLEX                 1
#define LV_USE_GRID                 1

/* ── OS / Threading ─────────────────────────────────────────────── */
#define LV_USE_OS                   LV_OS_NONE

/* ── File system ────────────────────────────────────────────────── */
#define LV_USE_FS_STDIO             0
#define LV_USE_FS_POSIX             0
#define LV_USE_FS_FATFS             0

/* ── Other features ─────────────────────────────────────────────── */
#define LV_USE_SNAPSHOT             0
#define LV_USE_SYSMON               0
#define LV_USE_PROFILER             0
#define LV_USE_OBSERVER             1
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_OBJ           0

#endif /* LV_CONF_H */
