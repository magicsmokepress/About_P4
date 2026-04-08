/**
 * CrowPanel Advanced 7" ESP32-P4 — MPU6050 6-Axis Motion Sensor
 *
 * Reads accelerometer + gyroscope data from MPU6050 over the hardware
 * I2C1 bus (shared with GT911 touch controller) and displays it on
 * screen via LVGL with a digital spirit level.
 *
 * Uses the LEGACY i2c driver API (driver/i2c.h) which is what BusI2C
 * uses internally — avoids the "driver_ng CONFLICT" crash.
 *
 * Wiring (I2C1 header):
 *   MPU6050 SDA → SDA  (I2C1 header, GPIO 45)
 *   MPU6050 SCL → SCL  (I2C1 header, GPIO 46)
 *   MPU6050 VCC → 3V3  (I2C1 header)
 *   MPU6050 GND → GND  (I2C1 header)
 *   (AD0 pin left floating or tied to GND for address 0x68)
 *
 * BOARD SETTINGS:
 *   Board:      "ESP32P4 Dev Module"
 *   USB Mode:   "Hardware CDC and JTAG"
 *   PSRAM:      "OPI PSRAM"
 *   Flash Mode: "QIO 80MHz"
 *   Partition:  "Huge APP (3MB No OTA/1MB SPIFFS)"
 */

#include <esp_display_panel.hpp>
#include <esp_lcd_panel_ops.h>
#include <lvgl.h>
#include "driver/i2c.h"               // legacy API — matches what BusI2C uses

using namespace esp_panel::drivers;

// ═══════════════════════════════════════════════════════════
//  Display + Touch config (proven boilerplate)
// ═══════════════════════════════════════════════════════════
#define LCD_WIDTH 1024
#define LCD_HEIGHT 600
#define LCD_DSI_LANE_NUM 2
#define LCD_DSI_LANE_RATE 1000
#define LCD_DPI_CLK_MHZ 52
#define LCD_COLOR_BITS ESP_PANEL_LCD_COLOR_BITS_RGB565
#define LCD_DPI_HPW 10
#define LCD_DPI_HBP 160
#define LCD_DPI_HFP 160
#define LCD_DPI_VPW 1
#define LCD_DPI_VBP 23
#define LCD_DPI_VFP 12
#define LCD_DSI_PHY_LDO_ID 3
#define LCD_RST_IO -1
#define LCD_BL_IO 31
#define LCD_BL_ON_LEVEL 1
#define TOUCH_I2C_SDA 45
#define TOUCH_I2C_SCL 46
#define TOUCH_I2C_FREQ (400 * 1000)
#define TOUCH_RST_IO 40
#define TOUCH_INT_IO 42
#define LVGL_BUF_LINES 60

// ═══════════════════════════════════════════════════════════
//  MPU6050 config — hardware I2C1 (shared with GT911)
// ═══════════════════════════════════════════════════════════
#define MPU6050_ADDR 0x68
#define MPU6050_ADDR_ALT 0x69          // AD0 pin HIGH
#define MPU6050_READ_INTERVAL 50       // ms — 20Hz update rate

// MPU6050 registers
#define MPU6050_REG_PWR_MGMT_1   0x6B
#define MPU6050_REG_WHO_AM_I     0x75
#define MPU6050_REG_ACCEL_XOUT_H 0x3B
#define MPU6050_REG_CONFIG       0x1A
#define MPU6050_REG_GYRO_CONFIG  0x1B
#define MPU6050_REG_ACCEL_CONFIG 0x1C

// ═══════════════════════════════════════════════════════════
//  Globals
// ═══════════════════════════════════════════════════════════
static LCD_EK79007 *g_lcd = NULL;
static TouchGT911 *g_touch = NULL;
static lv_display_t *lvgl_disp = NULL;
static lv_indev_t *lvgl_touch_dev = NULL;
static volatile bool touch_pressed = false;
static volatile int16_t touch_x = 0, touch_y = 0;

// I2C port — BusI2C uses I2C_NUM_0 by default
static i2c_port_t mpu_i2c_port = I2C_NUM_0;
static uint8_t mpu_addr = MPU6050_ADDR;

// Sensor data
static float accel_x = 0, accel_y = 0, accel_z = 0;
static float gyro_x = 0, gyro_y = 0, gyro_z = 0;
static float mpu_temp = 0;
static float pitch = 0, roll = 0;
static bool mpu_ok = false;

// UI
static lv_obj_t *accel_lbl = NULL;
static lv_obj_t *gyro_lbl = NULL;
static lv_obj_t *angle_lbl = NULL;
static lv_obj_t *temp_lbl = NULL;
static lv_obj_t *status_lbl = NULL;
static lv_obj_t *bubble = NULL;
static lv_obj_t *level_bg = NULL;

// ═══════════════════════════════════════════════════════════
//  Hardware I2C helpers (legacy driver API — matches BusI2C)
// ═══════════════════════════════════════════════════════════

static bool mpu_write_reg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    esp_err_t err = i2c_master_write_to_device(
        mpu_i2c_port, mpu_addr, buf, 2, pdMS_TO_TICKS(100));
    return (err == ESP_OK);
}

static bool mpu_read_reg(uint8_t reg, uint8_t *data, uint8_t len) {
    esp_err_t err = i2c_master_write_read_device(
        mpu_i2c_port, mpu_addr, &reg, 1, data, len, pdMS_TO_TICKS(100));
    return (err == ESP_OK);
}

// ═══════════════════════════════════════════════════════════
//  MPU6050 init & read
// ═══════════════════════════════════════════════════════════

static bool mpu6050_init_at_addr(uint8_t addr) {
    mpu_addr = addr;
    Serial.printf("[MPU6050] Trying address 0x%02X on port %d...\n", addr, mpu_i2c_port);

    // Check WHO_AM_I register (should be 0x68 for MPU6050, 0x71 for MPU6500, 0x19 for MPU6886)
    uint8_t who = 0;
    if (!mpu_read_reg(MPU6050_REG_WHO_AM_I, &who, 1)) {
        Serial.printf("[MPU6050] No response at 0x%02X\n", addr);
        return false;
    }
    Serial.printf("[MPU6050] WHO_AM_I: 0x%02X", who);
    if (who == 0x68) Serial.println(" (MPU6050)");
    else if (who == 0x71) Serial.println(" (MPU6500)");
    else if (who == 0x19) Serial.println(" (MPU6886)");
    else if (who == 0x98) Serial.println(" (ICM-20689)");
    else { Serial.println(" (unknown — trying anyway)"); }

    // Wake up (clear sleep bit, use internal 8MHz oscillator)
    if (!mpu_write_reg(MPU6050_REG_PWR_MGMT_1, 0x00)) {
        Serial.println("[MPU6050] Failed to wake device");
        return false;
    }
    delay(100);

    // Configure: DLPF 44Hz (smooth data), ±2g accel, ±250 deg/s gyro
    mpu_write_reg(MPU6050_REG_CONFIG, 0x03);       // DLPF_CFG = 3 (44Hz)
    mpu_write_reg(MPU6050_REG_GYRO_CONFIG, 0x00);  // FS_SEL = 0 (±250 deg/s)
    mpu_write_reg(MPU6050_REG_ACCEL_CONFIG, 0x00); // AFS_SEL = 0 (±2g)

    // Verify device is responding post-config
    uint8_t verify = 0;
    if (!mpu_read_reg(MPU6050_REG_PWR_MGMT_1, &verify, 1)) {
        Serial.println("[MPU6050] Post-config verify failed");
        return false;
    }

    Serial.printf("[MPU6050] Initialized OK at 0x%02X — sharing I2C bus with GT911\n", addr);
    return true;
}

static bool mpu6050_init() {
    Serial.printf("[MPU6050] Using legacy I2C API on port %d (shared with GT911)\n",
                  mpu_i2c_port);

    // Try standard address first, then alternate
    if (mpu6050_init_at_addr(MPU6050_ADDR)) return true;
    if (mpu6050_init_at_addr(MPU6050_ADDR_ALT)) return true;

    Serial.println("[MPU6050] Not found on either address");
    return false;
}

static bool mpu6050_init_with_fallback() {
    if (mpu6050_init()) return true;

    Serial.println("[MPU6050] Port 0 failed, trying I2C_NUM_1...");
    mpu_i2c_port = I2C_NUM_1;
    return mpu6050_init();
}

static void mpu6050_read() {
    uint8_t buf[14];
    if (!mpu_read_reg(MPU6050_REG_ACCEL_XOUT_H, buf, 14)) return;

    int16_t raw_ax = (buf[0] << 8) | buf[1];
    int16_t raw_ay = (buf[2] << 8) | buf[3];
    int16_t raw_az = (buf[4] << 8) | buf[5];
    accel_x = raw_ax / 16384.0;  // ±2g range
    accel_y = raw_ay / 16384.0;
    accel_z = raw_az / 16384.0;

    int16_t raw_temp = (buf[6] << 8) | buf[7];
    mpu_temp = raw_temp / 340.0 + 36.53;

    int16_t raw_gx = (buf[8] << 8) | buf[9];
    int16_t raw_gy = (buf[10] << 8) | buf[11];
    int16_t raw_gz = (buf[12] << 8) | buf[13];
    gyro_x = raw_gx / 131.0;  // ±250 deg/s range
    gyro_y = raw_gy / 131.0;
    gyro_z = raw_gz / 131.0;

    pitch = atan2(accel_x, sqrt(accel_y * accel_y + accel_z * accel_z)) * 180.0 / M_PI;
    roll  = atan2(accel_y, sqrt(accel_x * accel_x + accel_z * accel_z)) * 180.0 / M_PI;
}

// ═══════════════════════════════════════════════════════════
//  Display + Touch init (proven boilerplate)
// ═══════════════════════════════════════════════════════════
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *a, uint8_t *px) {
    esp_lcd_panel_handle_t p = g_lcd->getHandle();
    if (p) {
        esp_err_t r;
        do {
            r = esp_lcd_panel_draw_bitmap(p, a->x1, a->y1, a->x2 + 1, a->y2 + 1, px);
            if (r == ESP_ERR_INVALID_STATE) delay(1);
        } while (r == ESP_ERR_INVALID_STATE);
    }
    lv_display_flush_ready(disp);
}

static void lvgl_touch_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    if (touch_pressed) {
        data->point.x = touch_x; data->point.y = touch_y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void init_hardware() {
    BusDSI *bus = new BusDSI(LCD_DSI_LANE_NUM, LCD_DSI_LANE_RATE,
        LCD_DPI_CLK_MHZ, LCD_COLOR_BITS, LCD_WIDTH, LCD_HEIGHT,
        LCD_DPI_HPW, LCD_DPI_HBP, LCD_DPI_HFP,
        LCD_DPI_VPW, LCD_DPI_VBP, LCD_DPI_VFP, LCD_DSI_PHY_LDO_ID);
    bus->configDpiFrameBufferNumber(1);
    assert(bus->begin());

    g_lcd = new LCD_EK79007(bus, LCD_WIDTH, LCD_HEIGHT, LCD_COLOR_BITS, LCD_RST_IO);
    assert(g_lcd->begin());

    BacklightPWM_LEDC *bl = new BacklightPWM_LEDC(LCD_BL_IO, LCD_BL_ON_LEVEL);
    assert(bl->begin()); assert(bl->on());

    BusI2C *tb = new BusI2C(TOUCH_I2C_SCL, TOUCH_I2C_SDA,
        (BusI2C::ControlPanelFullConfig)ESP_PANEL_TOUCH_I2C_CONTROL_PANEL_CONFIG(GT911));
    tb->configI2C_FreqHz(TOUCH_I2C_FREQ);
    tb->configI2C_PullupEnable(true, true);

    g_touch = new TouchGT911(tb, LCD_WIDTH, LCD_HEIGHT, TOUCH_RST_IO, TOUCH_INT_IO);
    g_touch->begin();

    lv_init();
    lv_tick_set_cb((lv_tick_get_cb_t)millis);
    size_t bsz = LCD_WIDTH * LVGL_BUF_LINES * sizeof(lv_color_t);
    uint8_t *buf1 = (uint8_t *)heap_caps_malloc(bsz, MALLOC_CAP_SPIRAM);
    assert(buf1);
    lvgl_disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
    lv_display_set_flush_cb(lvgl_disp, lvgl_flush_cb);
    lv_display_set_buffers(lvgl_disp, buf1, NULL, bsz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lvgl_touch_dev = lv_indev_create();
    lv_indev_set_type(lvgl_touch_dev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lvgl_touch_dev, lvgl_touch_cb);
    lv_indev_set_display(lvgl_touch_dev, lvgl_disp);
}

// ═══════════════════════════════════════════════════════════
//  UI
// ═══════════════════════════════════════════════════════════
static void build_ui() {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A0A1A), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "MPU6050 Motion Sensor");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x44AACC), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    lv_obj_t *accel_title = lv_label_create(scr);
    lv_label_set_text(accel_title, "Accelerometer (g)");
    lv_obj_set_style_text_font(accel_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(accel_title, lv_color_hex(0xFF8844), 0);
    lv_obj_set_pos(accel_title, 30, 65);

    accel_lbl = lv_label_create(scr);
    lv_label_set_text(accel_lbl, "X: --    Y: --    Z: --");
    lv_obj_set_style_text_font(accel_lbl, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(accel_lbl, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_pos(accel_lbl, 30, 90);

    lv_obj_t *gyro_title = lv_label_create(scr);
    lv_label_set_text(gyro_title, "Gyroscope (deg/s)");
    lv_obj_set_style_text_font(gyro_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(gyro_title, lv_color_hex(0x44FF88), 0);
    lv_obj_set_pos(gyro_title, 30, 135);

    gyro_lbl = lv_label_create(scr);
    lv_label_set_text(gyro_lbl, "X: --    Y: --    Z: --");
    lv_obj_set_style_text_font(gyro_lbl, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(gyro_lbl, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_pos(gyro_lbl, 30, 160);

    lv_obj_t *angle_title = lv_label_create(scr);
    lv_label_set_text(angle_title, "Tilt Angles");
    lv_obj_set_style_text_font(angle_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(angle_title, lv_color_hex(0xAAAAFF), 0);
    lv_obj_set_pos(angle_title, 30, 205);

    angle_lbl = lv_label_create(scr);
    lv_label_set_text(angle_lbl, "Pitch: --    Roll: --");
    lv_obj_set_style_text_font(angle_lbl, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(angle_lbl, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_pos(angle_lbl, 30, 230);

    temp_lbl = lv_label_create(scr);
    lv_label_set_text(temp_lbl, "Sensor temp: --");
    lv_obj_set_style_text_font(temp_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(temp_lbl, lv_color_hex(0x888888), 0);
    lv_obj_set_pos(temp_lbl, 30, 275);

    // Spirit level
    level_bg = lv_obj_create(scr);
    lv_obj_set_size(level_bg, 260, 260);
    lv_obj_set_style_radius(level_bg, 130, 0);
    lv_obj_set_style_bg_color(level_bg, lv_color_hex(0x1A2A1A), 0);
    lv_obj_set_style_border_color(level_bg, lv_color_hex(0x336633), 0);
    lv_obj_set_style_border_width(level_bg, 2, 0);
    lv_obj_set_pos(level_bg, 700, 80);
    lv_obj_remove_flag(level_bg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *h_line = lv_obj_create(level_bg);
    lv_obj_set_size(h_line, 220, 1);
    lv_obj_set_style_bg_color(h_line, lv_color_hex(0x334433), 0);
    lv_obj_set_style_border_width(h_line, 0, 0);
    lv_obj_set_style_radius(h_line, 0, 0);
    lv_obj_center(h_line);

    lv_obj_t *v_line = lv_obj_create(level_bg);
    lv_obj_set_size(v_line, 1, 220);
    lv_obj_set_style_bg_color(v_line, lv_color_hex(0x334433), 0);
    lv_obj_set_style_border_width(v_line, 0, 0);
    lv_obj_set_style_radius(v_line, 0, 0);
    lv_obj_center(v_line);

    bubble = lv_obj_create(level_bg);
    lv_obj_set_size(bubble, 30, 30);
    lv_obj_set_style_radius(bubble, 15, 0);
    lv_obj_set_style_bg_color(bubble, lv_color_hex(0x00FF66), 0);
    lv_obj_set_style_border_width(bubble, 0, 0);
    lv_obj_set_style_shadow_width(bubble, 15, 0);
    lv_obj_set_style_shadow_color(bubble, lv_color_hex(0x00FF66), 0);
    lv_obj_set_style_shadow_opa(bubble, 150, 0);
    lv_obj_center(bubble);
    lv_obj_remove_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);

    status_lbl = lv_label_create(scr);
    lv_label_set_text(status_lbl, "Initializing MPU6050...");
    lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x555555), 0);
    lv_obj_align(status_lbl, LV_ALIGN_BOTTOM_MID, 0, -15);
}

static void update_ui() {
    if (!mpu_ok) return;
    char buf[100];

    snprintf(buf, sizeof(buf), "X: %+.2f    Y: %+.2f    Z: %+.2f", accel_x, accel_y, accel_z);
    lv_label_set_text(accel_lbl, buf);

    snprintf(buf, sizeof(buf), "X: %+.1f    Y: %+.1f    Z: %+.1f", gyro_x, gyro_y, gyro_z);
    lv_label_set_text(gyro_lbl, buf);

    snprintf(buf, sizeof(buf), "Pitch: %+.1f°    Roll: %+.1f°", pitch, roll);
    lv_label_set_text(angle_lbl, buf);

    snprintf(buf, sizeof(buf), "Sensor temp: %.1f°C", mpu_temp);
    lv_label_set_text(temp_lbl, buf);

    // Spirit level bubble position
    float bx = constrain(roll, -45.0, 45.0) * (100.0 / 45.0);
    float by = constrain(pitch, -45.0, 45.0) * (100.0 / 45.0);
    lv_obj_set_pos(bubble, 115 + (int)bx, 115 - (int)by);

    // Bubble color: green=level, orange=tilted, red=very tilted
    float total_tilt = sqrt(pitch * pitch + roll * roll);
    uint32_t color;
    if (total_tilt < 3.0) color = 0x00FF66;
    else if (total_tilt < 15.0) color = 0xFFAA00;
    else color = 0xFF4444;
    lv_obj_set_style_bg_color(bubble, lv_color_hex(color), 0);
    lv_obj_set_style_shadow_color(bubble, lv_color_hex(color), 0);

    snprintf(buf, sizeof(buf), "MPU6050 @ 0x%02X on I2C1 (shared w/ GT911)  |  20Hz  |  Tilt: %.1f°",
        mpu_addr, total_tilt);
    lv_label_set_text(status_lbl, buf);
}

// ═══════════════════════════════════════════════════════════
//  Setup & Loop
// ═══════════════════════════════════════════════════════════

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n╔═══════════════════════════════════════════╗");
    Serial.println("║  CrowPanel P4 — MPU6050 Motion Sensor Test  ║");
    Serial.println("╚═══════════════════════════════════════════╝\n");

    init_hardware();
    build_ui();
    lv_timer_handler();

    mpu_ok = mpu6050_init_with_fallback();

    if (mpu_ok) {
        lv_label_set_text(status_lbl, "MPU6050 ready — 20Hz update");
        lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x44AA44), 0);
    } else {
        lv_label_set_text(status_lbl, "MPU6050 FAILED — check I2C1 header wiring (AD0→GND for 0x68)");
        lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xFF4444), 0);
    }
    lv_timer_handler();

    Serial.println("Setup complete.\n");
}

void loop() {
    static uint32_t lt = 0;
    if (millis() - lt > 30) {
        lt = millis();
        if (g_touch) {
            esp_panel::drivers::TouchPoint tp[1];
            int pts = g_touch->readPoints(tp, 1);
            touch_pressed = (pts > 0);
            if (touch_pressed) { touch_x = tp[0].x; touch_y = tp[0].y; }
        }
    }

    static uint32_t last_mpu = 0;
    if (mpu_ok && millis() - last_mpu >= MPU6050_READ_INTERVAL) {
        last_mpu = millis();
        mpu6050_read();
        update_ui();

        static int log_div = 0;
        if (++log_div >= 10) {  // Print to serial every 500ms (not every 50ms)
            log_div = 0;
            Serial.printf("[MPU6050] P=%+.1f° R=%+.1f° | A: %+.2f %+.2f %+.2f | G: %+.1f %+.1f %+.1f | T=%.1f°C\n",
                pitch, roll, accel_x, accel_y, accel_z, gyro_x, gyro_y, gyro_z, mpu_temp);
        }
    }

    lv_timer_handler();
    delay(5);
}
