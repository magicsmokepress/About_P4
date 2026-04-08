// board_config.h - Elecrow CrowPanel Advanced 7"
// ESP32-P4 | EK79007 1024x600 MIPI-DSI | GT911 Touch
// Book: "Programming the ESP32-P4" by Marko Vasilj
//
// Usage: Place this file in your sketch folder alongside your .ino file.
//        All application code should use these #defines, never raw GPIO numbers.

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

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
// IMPORTANT: Initialize SD card AFTER display - LDO4 dependency
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

#endif // BOARD_CONFIG_H
