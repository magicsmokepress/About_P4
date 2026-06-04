// board_config.h - Elecrow CrowPanel Advanced 7"
// ESP32-P4 | EK79007 1024x600 MIPI-DSI | GT911 Touch
// Book: "Programming the ESP32-P4" by Marko Vasilj
//
// Usage: Place this file in your sketch folder alongside your .ino file.
//        All application code should use these #defines, never raw GPIO numbers.
//
// The book's reduced listings open with `#include "board_config.h"` and
// assume this header also pulls in the display driver layer below
// (see "How to Read This Book"). Requires ESP32_Display_Panel 1.x.

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

// ─── Panel / Touch driver layer ─────────────────────────────────────
// A using-directive in a header is normally bad manners; in the
// single-translation-unit world of an .ino sketch it is exactly what
// the book's listings expect (bare TouchGT911, LCD_EK79007, ...).
#include <esp_display_panel.hpp>
#include <esp_lcd_panel_ops.h>
using namespace esp_panel::drivers;

// ─── Board Identifier ───────────────────────────────────────────────
#define BOARD_CROWPANEL_7

// ─── Display: EK79007 MIPI-DSI ──────────────────────────────────────
#define DISPLAY_WIDTH          1024
#define DISPLAY_HEIGHT         600
#define DISPLAY_COLOR_DEPTH    16          // RGB565
#define DISPLAY_DPI_CLK_MHZ   52
#define DISPLAY_DSI_LANES      2
#define DISPLAY_DSI_LANE_RATE  1000        // Mbps
#define DISPLAY_DSI_PHY_LDO   3

// Timing parameters
#define DISPLAY_HPW            10          // Horizontal Pulse Width
#define DISPLAY_HBP            160         // Horizontal Back Porch
#define DISPLAY_HFP            160         // Horizontal Front Porch
#define DISPLAY_VPW            1           // Vertical Pulse Width
#define DISPLAY_VBP            23          // Vertical Back Porch
#define DISPLAY_VFP            12          // Vertical Front Porch

// Control pins
#define DISPLAY_RST_PIN        -1          // No hardware reset
#define DISPLAY_BL_PIN         31          // Backlight, PWM, active HIGH
#define DISPLAY_BL_ON          HIGH
#define DISPLAY_BL_OFF         LOW

// ─── Touch: GT911 Capacitive ────────────────────────────────────────
#define TOUCH_SDA              45
#define TOUCH_SCL              46
#define TOUCH_I2C_FREQ         400000      // 400 kHz
#define TOUCH_RST              40
#define TOUCH_INT              42
#define TOUCH_MAX_POINTS       5

// ─── Audio Output: I2S + NS4168 ─────────────────────────────────────
#define AUDIO_I2S_BCLK         22
#define AUDIO_I2S_LRCLK        21
#define AUDIO_I2S_DOUT         23
#define AUDIO_AMP_ENABLE       30
#define AUDIO_AMP_ON           LOW         // NS4168: active LOW enable
#define AUDIO_AMP_OFF          HIGH

// ─── Audio Input: PDM Microphone ────────────────────────────────────
#define MIC_PDM_CLK            24
#define MIC_PDM_DIN            26
#define MIC_SAMPLE_RATE        16000       // 16 kHz
#define MIC_CHANNELS           1           // Mono

// ─── SD Card: SD_MMC 1-bit ──────────────────────────────────────────
// IMPORTANT: SD and display share LDO4, so init order is part of a
// sketch's tested behavior. The ch18/ch19 sketches mount SD FIRST,
// before any display call. Keep the order of the sketch you started
// from; if you reorder, re-verify on your own bench.
#define SD_CLK                 43
#define SD_CMD                 44
#define SD_D0                  39
#define SD_MODE_1BIT           true

// ─── Ethernet: W5500 External Module (Bit-Bang SPI) ─────────────────
#define ETH_W5500_SCK          4
#define ETH_W5500_MOSI         3
#define ETH_W5500_MISO         2
#define ETH_W5500_CS           5
#define ETH_W5500_RST          25

// ─── LVGL Configuration Helpers ─────────────────────────────────────
// Frame buffer size: one-tenth of screen for partial rendering
#define LVGL_BUF_LINES         (DISPLAY_HEIGHT / 10)
#define LVGL_BUF_SIZE          (DISPLAY_WIDTH * LVGL_BUF_LINES * (DISPLAY_COLOR_DEPTH / 8))

// ─── Book-listing aliases ───────────────────────────────────────────
// In-book listings (and the full repo sketches) use the LCD_*/TOUCH_*
// names below; they map onto the canonical DISPLAY_*/TOUCH_* values above.
#define LCD_WIDTH              DISPLAY_WIDTH
#define LCD_HEIGHT             DISPLAY_HEIGHT
#define LCD_DSI_LANE_NUM       DISPLAY_DSI_LANES
#define LCD_DSI_LANE_RATE      DISPLAY_DSI_LANE_RATE
#define LCD_DPI_CLK_MHZ        DISPLAY_DPI_CLK_MHZ
#define LCD_COLOR_BITS         ESP_PANEL_LCD_COLOR_BITS_RGB565
#define LCD_DPI_HPW            DISPLAY_HPW
#define LCD_DPI_HBP            DISPLAY_HBP
#define LCD_DPI_HFP            DISPLAY_HFP
#define LCD_DPI_VPW            DISPLAY_VPW
#define LCD_DPI_VBP            DISPLAY_VBP
#define LCD_DPI_VFP            DISPLAY_VFP
#define LCD_DSI_PHY_LDO_ID     DISPLAY_DSI_PHY_LDO
#define LCD_RST_IO             DISPLAY_RST_PIN
#define LCD_BL_IO              DISPLAY_BL_PIN
#define LCD_BL_ON_LEVEL        1
#define TOUCH_I2C_SDA          TOUCH_SDA
#define TOUCH_I2C_SCL          TOUCH_SCL
#define TOUCH_RST_IO           TOUCH_RST
#define TOUCH_INT_IO           TOUCH_INT

#endif // BOARD_CONFIG_H
