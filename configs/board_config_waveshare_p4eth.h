// board_config.h - Waveshare ESP32-P4-ETH
// ESP32-P4 | ILI9488 480x320 SPI | XPT2046 Touch | IP101 Ethernet
// Book: "Programming the ESP32-P4" by Marko Vasilj
//
// NOTE: This board uses LovyanGFX for display, not ESP_Display_Panel.
//       The LGFX class definition is included at the bottom of this file.

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

// ─── Board Identifier ───────────────────────────────────────────────
#define BOARD_WAVESHARE_P4_ETH

// ─── Display: ILI9488 SPI ───────────────────────────────────────────
#define DISPLAY_WIDTH          480
#define DISPLAY_HEIGHT         320
#define DISPLAY_COLOR_DEPTH    16          // RGB565
#define DISPLAY_SPI_HOST       SPI2_HOST
#define DISPLAY_SPI_FREQ       40000000   // 40 MHz write
#define DISPLAY_SPI_READ_FREQ  16000000   // 16 MHz read

// SPI pins
#define DISPLAY_SCLK           26
#define DISPLAY_MOSI           23
#define DISPLAY_MISO           27
#define DISPLAY_DC             22
#define DISPLAY_CS             20
#define DISPLAY_RST            21

// No backlight control - always on via hardware pull-up

// ─── Touch: XPT2046 Resistive SPI ──────────────────────────────────
#define TOUCH_CS               33
#define TOUCH_SPI_FREQ         1000000    // 1 MHz
#define TOUCH_OFFSET_ROTATION  6          // For landscape setRotation(1)

// ─── Ethernet: IP101 RMII (On-Board) ───────────────────────────────
// CRITICAL: These #defines MUST appear BEFORE #include <ETH.h>
#define ETH_PHY_TYPE           ETH_PHY_IP101
#define ETH_PHY_ADDR           1
#define ETH_PHY_MDC            31
#define ETH_PHY_MDIO           52
#define ETH_PHY_POWER          51
#define ETH_CLK_MODE           EMAC_CLK_EXT_IN

// ─── LVGL Configuration Helpers ─────────────────────────────────────
#define LVGL_BUF_LINES         (DISPLAY_HEIGHT / 10)
#define LVGL_BUF_SIZE          (DISPLAY_WIDTH * LVGL_BUF_LINES * (DISPLAY_COLOR_DEPTH / 8))

// ─── No Audio, SD Card, or Camera on this board ─────────────────────

#endif // BOARD_CONFIG_H
