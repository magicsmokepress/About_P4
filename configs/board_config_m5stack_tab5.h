// board_config.h - M5Stack Tab5
// ESP32-P4 | ST7123 1024x600 MIPI-DSI | FT5x06 Touch | SC202CS Camera
// Book: "Programming the ESP32-P4" by Marko Vasilj
//
// NOTE: Display and touch are managed by M5Unified / M5GFX.
// This config provides camera pins and I2C addresses for direct access.
// Most projects should use M5Unified APIs rather than raw GPIO access.

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

// ─── Board Identifier ───────────────────────────────────────────────
#define BOARD_M5STACK_TAB5

// ─── Display (M5GFX-managed) ────────────────────────────────────────
// Display is initialized by M5.begin() - no manual pin config needed
#define DISPLAY_WIDTH          1024
#define DISPLAY_HEIGHT         600
#define DISPLAY_COLOR_DEPTH    16

// ─── Camera: SC202CS via MIPI-CSI ───────────────────────────────────
#define CAM_SCCB_SDA           7
#define CAM_SCCB_SCL           8
#define CAM_I2C_ADDR           0x36
#define CAM_XCLK_PIN           36
#define CAM_XCLK_FREQ         24000000    // 24 MHz
#define CAM_CSI_LANES          1           // MUST be 1 - 2-lane produces no frames
#define CAM_CHIP_ID            0xEB52
#define CAM_MAX_WIDTH          1280
#define CAM_MAX_HEIGHT         720

// ─── Internal I2C Bus ───────────────────────────────────────────────
#define INTERNAL_I2C_SDA       31
#define INTERNAL_I2C_SCL       32
#define I2C_ADDR_POWER_MON     0x28
#define I2C_ADDR_RTC           0x32
#define I2C_ADDR_AUDIO_CODEC   0x40
#define I2C_ADDR_IO_EXP1       0x43
#define I2C_ADDR_IO_EXP2       0x44
#define I2C_ADDR_TOUCH         0x55
#define I2C_ADDR_IMU           0x68

// ─── LVGL Configuration Helpers ─────────────────────────────────────
// Note: When using LVGL with Tab5, you must use M5GFX as the display backend.
// The lv_port callbacks differ from the ESP_Display_Panel projects.
#define LVGL_BUF_LINES         (DISPLAY_HEIGHT / 10)
#define LVGL_BUF_SIZE          (DISPLAY_WIDTH * LVGL_BUF_LINES * (DISPLAY_COLOR_DEPTH / 8))

#endif // BOARD_CONFIG_H
