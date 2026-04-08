/**
 * CrowPanel Advanced 7" ESP32-P4 — BME280/BMP280 Environment Sensor
 *
 * Auto-detects BME280 (temp+hum+press) or BMP280 (temp+press only)
 * and adapts the UI accordingly. Shares the hardware I2C bus with
 * the GT911 touch controller.
 *
 * Uses the LEGACY i2c driver API (driver/i2c.h) which is what BusI2C uses
 * internally — avoids the "driver_ng CONFLICT" crash that happens when
 * mixing the new i2c_master.h API with the old driver on the same port.
 *
 * Wiring (I2C1 header):
 *   BME/BMP280 SDA → SDA  (I2C1 header, GPIO 45)
 *   BME/BMP280 SCL → SCL  (I2C1 header, GPIO 46)
 *   BME/BMP280 VCC → 3V3  (I2C1 header)
 *   BME/BMP280 GND → GND  (I2C1 header)
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
//  BME280 config
// ═══════════════════════════════════════════════════════════
#define BME280_ADDR 0x76
#define BME280_READ_INTERVAL 2000

#define BME280_REG_ID         0xD0
#define BME280_REG_RESET      0xE0
#define BME280_REG_CTRL_HUM   0xF2
#define BME280_REG_STATUS     0xF3
#define BME280_REG_CTRL_MEAS  0xF4
#define BME280_REG_CONFIG     0xF5
#define BME280_REG_PRESS_MSB  0xF7
#define BME280_REG_CALIB00    0x88
#define BME280_REG_CALIB26    0xE1

// ═══════════════════════════════════════════════════════════
//  Globals
// ═══════════════════════════════════════════════════════════
static LCD_EK79007 *g_lcd = NULL;
static TouchGT911 *g_touch = NULL;
static lv_display_t *lvgl_disp = NULL;
static lv_indev_t *lvgl_touch_dev = NULL;
static volatile bool touch_pressed = false;
static volatile int16_t touch_x = 0, touch_y = 0;

// I2C port number — BusI2C uses I2C_NUM_0 by default
static i2c_port_t bme_i2c_port = I2C_NUM_0;

// Sensor data
static float bme_temp = 0;
static float bme_hum = 0;
static float bme_press = 0;
static float bme_alt = 0;
static bool bme_ok = false;
static bool has_humidity = false;   // true = BME280, false = BMP280
static uint8_t chip_id = 0;
static int bme_reads = 0;

// BME280 calibration
static uint16_t dig_T1;
static int16_t  dig_T2, dig_T3;
static uint16_t dig_P1;
static int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
static uint8_t  dig_H1, dig_H3;
static int16_t  dig_H2, dig_H4, dig_H5;
static int8_t   dig_H6;
static int32_t  t_fine;

// UI
static lv_obj_t *title_lbl = NULL;
static lv_obj_t *temp_lbl = NULL;
static lv_obj_t *hum_title_lbl = NULL;
static lv_obj_t *hum_lbl = NULL;
static lv_obj_t *press_lbl = NULL;
static lv_obj_t *alt_lbl = NULL;
static lv_obj_t *status_lbl = NULL;
static lv_obj_t *temp_f_lbl = NULL;

// ═══════════════════════════════════════════════════════════
//  Hardware I2C helpers (legacy driver API — matches BusI2C)
// ═══════════════════════════════════════════════════════════

static bool bme_write_reg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    esp_err_t err = i2c_master_write_to_device(
        bme_i2c_port, BME280_ADDR, buf, 2, pdMS_TO_TICKS(100));
    return (err == ESP_OK);
}

static bool bme_read_reg(uint8_t reg, uint8_t *data, uint8_t len) {
    esp_err_t err = i2c_master_write_read_device(
        bme_i2c_port, BME280_ADDR, &reg, 1, data, len, pdMS_TO_TICKS(100));
    return (err == ESP_OK);
}

// ═══════════════════════════════════════════════════════════
//  BME280 compensation (Bosch datasheet algorithms)
// ═══════════════════════════════════════════════════════════

static bool bme280_read_calibration() {
    uint8_t buf[26];
    if (!bme_read_reg(BME280_REG_CALIB00, buf, 26)) return false;

    dig_T1 = buf[0] | (buf[1] << 8);
    dig_T2 = buf[2] | (buf[3] << 8);
    dig_T3 = buf[4] | (buf[5] << 8);
    dig_P1 = buf[6] | (buf[7] << 8);
    dig_P2 = buf[8] | (buf[9] << 8);
    dig_P3 = buf[10] | (buf[11] << 8);
    dig_P4 = buf[12] | (buf[13] << 8);
    dig_P5 = buf[14] | (buf[15] << 8);
    dig_P6 = buf[16] | (buf[17] << 8);
    dig_P7 = buf[18] | (buf[19] << 8);
    dig_P8 = buf[20] | (buf[21] << 8);
    dig_P9 = buf[22] | (buf[23] << 8);

    // Humidity calibration — only present on BME280 (chip ID 0x60)
    if (has_humidity) {
        dig_H1 = buf[25];

        uint8_t hbuf[7];
        if (!bme_read_reg(BME280_REG_CALIB26, hbuf, 7)) return false;

        dig_H2 = hbuf[0] | (hbuf[1] << 8);
        dig_H3 = hbuf[2];
        dig_H4 = (hbuf[3] << 4) | (hbuf[4] & 0x0F);
        dig_H5 = (hbuf[5] << 4) | (hbuf[4] >> 4);
        dig_H6 = hbuf[6];
    }

    return true;
}

static float bme280_compensate_temp(int32_t adc_T) {
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) *
                      ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) *
                    ((int32_t)dig_T3)) >> 14;
    t_fine = var1 + var2;
    return (t_fine * 5 + 128) / 256 / 100.0;
}

static float bme280_compensate_press(int32_t adc_P) {
    int64_t var1 = ((int64_t)t_fine) - 128000;
    int64_t var2 = var1 * var1 * (int64_t)dig_P6;
    var2 = var2 + ((var1 * (int64_t)dig_P5) << 17);
    var2 = var2 + (((int64_t)dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)dig_P3) >> 8) + ((var1 * (int64_t)dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)dig_P1) >> 33;
    if (var1 == 0) return 0;
    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)dig_P7) << 4);
    return (float)p / 256.0 / 100.0;
}

static float bme280_compensate_hum(int32_t adc_H) {
    int32_t v = t_fine - 76800;
    v = (((((adc_H << 14) - (((int32_t)dig_H4) << 20) - (((int32_t)dig_H5) * v)) +
           16384) >> 15) *
         (((((((v * ((int32_t)dig_H6)) >> 10) *
              (((v * ((int32_t)dig_H3)) >> 11) + 32768)) >> 10) + 2097152) *
            ((int32_t)dig_H2) + 8192) >> 14));
    v = v - (((((v >> 15) * (v >> 15)) >> 7) * ((int32_t)dig_H1)) >> 4);
    v = (v < 0) ? 0 : v;
    v = (v > 419430400) ? 419430400 : v;
    return (float)(v >> 12) / 1024.0;
}

// ═══════════════════════════════════════════════════════════
//  BME280 init & read
// ═══════════════════════════════════════════════════════════

static bool bme280_init() {
    // The legacy I2C driver is already configured by BusI2C (for touch).
    // We just talk to the BME280 on the same port using the legacy API
    // functions (i2c_master_write_read_device, etc.) — no bus handle
    // or device registration needed.
    Serial.printf("[BME280] Using legacy I2C API on port %d (shared with GT911)\n",
                  bme_i2c_port);

    // Check chip ID
    if (!bme_read_reg(BME280_REG_ID, &chip_id, 1)) {
        Serial.println("[SENSOR] I2C read failed — check wiring");
        return false;
    }
    Serial.printf("[SENSOR] Chip ID: 0x%02X", chip_id);
    if (chip_id == 0x60) {
        has_humidity = true;
        Serial.println(" (BME280 — temp + humidity + pressure)");
    } else if (chip_id == 0x58) {
        has_humidity = false;
        Serial.println(" (BMP280 — temp + pressure only, no humidity)");
    } else {
        Serial.println(" (unknown chip)");
        return false;
    }

    // Soft reset
    bme_write_reg(BME280_REG_RESET, 0xB6);
    delay(10);

    // Wait for NVM copy
    uint8_t status;
    for (int i = 0; i < 10; i++) {
        bme_read_reg(BME280_REG_STATUS, &status, 1);
        if (!(status & 0x01)) break;
        delay(10);
    }

    // Read calibration
    if (!bme280_read_calibration()) {
        Serial.println("[BME280] Failed to read calibration");
        return false;
    }
    Serial.println("[BME280] Calibration loaded");

    // Configure: standby 1000ms, filter off, temp x1, press x1, normal mode
    if (has_humidity) {
        bme_write_reg(BME280_REG_CTRL_HUM, 0x01);  // humidity x1 (BME280 only)
    }
    bme_write_reg(BME280_REG_CONFIG, 0xA0);
    bme_write_reg(BME280_REG_CTRL_MEAS, 0x27);
    delay(50);

    const char *name = has_humidity ? "BME280" : "BMP280";
    Serial.printf("[SENSOR] %s initialized OK — sharing I2C bus with GT911 touch\n", name);
    return true;
}

// Try the other I2C port if default fails
static bool bme280_init_with_fallback() {
    if (bme280_init()) return true;

    Serial.println("[BME280] Port 0 failed, trying I2C_NUM_1...");
    bme_i2c_port = I2C_NUM_1;
    return bme280_init();
}

static void bme280_read() {
    uint8_t read_len = has_humidity ? 8 : 6;  // BMP280: 6 bytes (P+T), BME280: 8 (P+T+H)
    uint8_t buf[8] = {0};
    if (!bme_read_reg(BME280_REG_PRESS_MSB, buf, read_len)) return;

    int32_t adc_P = ((int32_t)buf[0] << 12) | ((int32_t)buf[1] << 4) | (buf[2] >> 4);
    int32_t adc_T = ((int32_t)buf[3] << 12) | ((int32_t)buf[4] << 4) | (buf[5] >> 4);

    bme_temp = bme280_compensate_temp(adc_T);
    bme_press = bme280_compensate_press(adc_P);

    if (has_humidity) {
        int32_t adc_H = ((int32_t)buf[6] << 8) | buf[7];
        bme_hum = bme280_compensate_hum(adc_H);
    }

    bme_alt = 44330.0 * (1.0 - pow(bme_press / 1013.25, 0.1903));
    bme_reads++;
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

    title_lbl = lv_label_create(scr);
    lv_label_set_text(title_lbl, "Environment Sensor");
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(0x44AACC), 0);
    lv_obj_align(title_lbl, LV_ALIGN_TOP_MID, 0, 20);

    // ── Temperature ──
    lv_obj_t *t_title = lv_label_create(scr);
    lv_label_set_text(t_title, "Temperature");
    lv_obj_set_style_text_font(t_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(t_title, lv_color_hex(0xFF8844), 0);
    lv_obj_set_pos(t_title, 80, 90);

    temp_lbl = lv_label_create(scr);
    lv_label_set_text(temp_lbl, "-- . - °C");
    lv_obj_set_style_text_font(temp_lbl, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(temp_lbl, lv_color_hex(0xFF8844), 0);
    lv_obj_set_pos(temp_lbl, 80, 115);

    temp_f_lbl = lv_label_create(scr);
    lv_label_set_text(temp_f_lbl, "-- . - °F");
    lv_obj_set_style_text_font(temp_f_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(temp_f_lbl, lv_color_hex(0xCC7744), 0);
    lv_obj_set_pos(temp_f_lbl, 80, 175);

    // ── Humidity (hidden if BMP280) ──
    hum_title_lbl = lv_label_create(scr);
    lv_label_set_text(hum_title_lbl, "Humidity");
    lv_obj_set_style_text_font(hum_title_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(hum_title_lbl, lv_color_hex(0x44AAFF), 0);
    lv_obj_set_pos(hum_title_lbl, 550, 90);

    hum_lbl = lv_label_create(scr);
    lv_label_set_text(hum_lbl, "-- . - %%");
    lv_obj_set_style_text_font(hum_lbl, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(hum_lbl, lv_color_hex(0x44AAFF), 0);
    lv_obj_set_pos(hum_lbl, 550, 115);

    // ── Separator ──
    lv_obj_t *line = lv_obj_create(scr);
    lv_obj_set_size(line, 860, 2);
    lv_obj_align(line, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_style_bg_color(line, lv_color_hex(0x333355), 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 0, 0);

    // ── Pressure ──
    lv_obj_t *p_title = lv_label_create(scr);
    lv_label_set_text(p_title, "Barometric Pressure");
    lv_obj_set_style_text_font(p_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(p_title, lv_color_hex(0xAACC44), 0);
    lv_obj_set_pos(p_title, 80, 290);

    press_lbl = lv_label_create(scr);
    lv_label_set_text(press_lbl, "---- . -- hPa");
    lv_obj_set_style_text_font(press_lbl, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(press_lbl, lv_color_hex(0xAACC44), 0);
    lv_obj_set_pos(press_lbl, 80, 315);

    // ── Altitude ──
    lv_obj_t *a_title = lv_label_create(scr);
    lv_label_set_text(a_title, "Est. Altitude");
    lv_obj_set_style_text_font(a_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(a_title, lv_color_hex(0xCC88FF), 0);
    lv_obj_set_pos(a_title, 550, 290);

    alt_lbl = lv_label_create(scr);
    lv_label_set_text(alt_lbl, "-- m / -- ft");
    lv_obj_set_style_text_font(alt_lbl, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(alt_lbl, lv_color_hex(0xCC88FF), 0);
    lv_obj_set_pos(alt_lbl, 550, 315);

    // ── Status ──
    status_lbl = lv_label_create(scr);
    lv_label_set_text(status_lbl, "Initializing BME280...");
    lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x555555), 0);
    lv_obj_align(status_lbl, LV_ALIGN_BOTTOM_MID, 0, -15);
}

static void update_ui() {
    char buf[64];

    // Temperature with comfort color
    snprintf(buf, sizeof(buf), "%.1f °C", bme_temp);
    lv_label_set_text(temp_lbl, buf);
    uint32_t tc = 0xFF8844;
    if (bme_temp < 18.0) tc = 0x4488FF;
    else if (bme_temp < 24.0) tc = 0x44CC88;
    else if (bme_temp > 30.0) tc = 0xFF4444;
    lv_obj_set_style_text_color(temp_lbl, lv_color_hex(tc), 0);

    float temp_f = bme_temp * 9.0 / 5.0 + 32.0;
    snprintf(buf, sizeof(buf), "%.1f °F", temp_f);
    lv_label_set_text(temp_f_lbl, buf);

    if (has_humidity) {
        snprintf(buf, sizeof(buf), "%.1f %%", bme_hum);
        lv_label_set_text(hum_lbl, buf);
    }

    snprintf(buf, sizeof(buf), "%.2f hPa", bme_press);
    lv_label_set_text(press_lbl, buf);

    float alt_ft = bme_alt * 3.28084;
    snprintf(buf, sizeof(buf), "%.0f m / %.0f ft", bme_alt, alt_ft);
    lv_label_set_text(alt_lbl, buf);

    const char *cname = has_humidity ? "BME280" : "BMP280";
    snprintf(buf, sizeof(buf), "%s on I2C1 (shared w/ GT911)  |  %.1f°F  |  Reads: %d",
        cname, temp_f, bme_reads);
    lv_label_set_text(status_lbl, buf);
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x44AA44), 0);
}

// ═══════════════════════════════════════════════════════════
//  Setup & Loop
// ═══════════════════════════════════════════════════════════

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n╔═══════════════════════════════════════════╗");
    Serial.println("║  CrowPanel P4 — BME280/BMP280 Sensor Test  ║");
    Serial.println("╚═══════════════════════════════════════════╝\n");

    init_hardware();
    build_ui();
    lv_timer_handler();

    bme_ok = bme280_init_with_fallback();

    if (bme_ok) {
        // Update title to show detected chip
        if (has_humidity) {
            lv_label_set_text(title_lbl, "BME280 Environment Sensor");
            lv_label_set_text(status_lbl, "BME280 ready — reading every 2s");
        } else {
            lv_label_set_text(title_lbl, "BMP280 Barometric Sensor");
            // Grey out humidity section for BMP280
            lv_label_set_text(hum_title_lbl, "Humidity");
            lv_obj_set_style_text_color(hum_title_lbl, lv_color_hex(0x333344), 0);
            lv_label_set_text(hum_lbl, "N/A");
            lv_obj_set_style_text_color(hum_lbl, lv_color_hex(0x333344), 0);
            lv_label_set_text(status_lbl, "BMP280 ready (no humidity) — reading every 2s");
        }
        lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x44AA44), 0);
    } else {
        lv_label_set_text(title_lbl, "Sensor NOT FOUND");
        lv_label_set_text(status_lbl, "BME/BMP280 FAILED — check I2C1 header wiring");
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

    static uint32_t last_bme = 0;
    if (bme_ok && millis() - last_bme >= BME280_READ_INTERVAL) {
        last_bme = millis();
        bme280_read();
        update_ui();

        float temp_f = bme_temp * 9.0 / 5.0 + 32.0;
        if (has_humidity) {
            Serial.printf("[BME280] T=%.1f°C (%.1f°F)  H=%.1f%%  P=%.2f hPa  Alt=%.0fm\n",
                bme_temp, temp_f, bme_hum, bme_press, bme_alt);
        } else {
            Serial.printf("[BMP280] T=%.1f°C (%.1f°F)  P=%.2f hPa  Alt=%.0fm\n",
                bme_temp, temp_f, bme_press, bme_alt);
        }
    }

    lv_timer_handler();
    delay(5);
}
