/**
 * CrowPanel Advanced 7" ESP32-P4 — GY-30 (BH1750) Light Sensor
 *
 * Reads ambient light intensity in lux from a GY-30 / BH1750 module
 * connected to the I2C1 header, sharing the bus with the GT911 touch
 * controller.
 *
 * Uses the LEGACY i2c driver API (driver/i2c.h) which is what BusI2C
 * uses internally — avoids the "driver_ng CONFLICT" crash.
 *
 * Wiring (I2C1 header):
 *   GY-30 SDA  → SDA  (I2C1 header, GPIO 45)
 *   GY-30 SCL  → SCL  (I2C1 header, GPIO 46)
 *   GY-30 VCC  → 3V3  (I2C1 header)
 *   GY-30 GND  → GND  (I2C1 header)
 *   GY-30 ADDR → GND  (or leave floating → address 0x23)
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
//  BH1750 config
// ═══════════════════════════════════════════════════════════
#define BH1750_ADDR_LOW   0x23    // ADDR pin LOW or floating
#define BH1750_ADDR_HIGH  0x5C    // ADDR pin HIGH

// BH1750 commands
#define BH1750_POWER_ON   0x01
#define BH1750_RESET      0x07
#define BH1750_CONT_HRES  0x10    // Continuous high-res mode (1 lx resolution, 120ms)
#define BH1750_CONT_HRES2 0x11    // Continuous high-res mode 2 (0.5 lx, 120ms)
#define BH1750_CONT_LRES  0x13    // Continuous low-res mode (4 lx, 16ms)

#define BH1750_READ_INTERVAL 500  // ms between reads

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
static i2c_port_t bh_i2c_port = I2C_NUM_0;
static uint8_t bh_addr = BH1750_ADDR_LOW;

// Sensor data
static float lux = 0;
static float lux_min = 99999;
static float lux_max = 0;
static bool bh_ok = false;
static int bh_reads = 0;

// UI labels
static lv_obj_t *title_lbl = NULL;
static lv_obj_t *lux_lbl = NULL;
static lv_obj_t *level_lbl = NULL;
static lv_obj_t *bar_obj = NULL;
static lv_obj_t *min_lbl = NULL;
static lv_obj_t *max_lbl = NULL;
static lv_obj_t *status_lbl = NULL;
static lv_obj_t *fc_lbl = NULL;

// ═══════════════════════════════════════════════════════════
//  Hardware I2C helpers (legacy driver API — matches BusI2C)
// ═══════════════════════════════════════════════════════════

static bool bh_write_cmd(uint8_t cmd) {
    esp_err_t err = i2c_master_write_to_device(
        bh_i2c_port, bh_addr, &cmd, 1, pdMS_TO_TICKS(100));
    return (err == ESP_OK);
}

static bool bh_read_data(uint8_t *data, size_t len) {
    esp_err_t err = i2c_master_read_from_device(
        bh_i2c_port, bh_addr, data, len, pdMS_TO_TICKS(100));
    return (err == ESP_OK);
}

// ═══════════════════════════════════════════════════════════
//  BH1750 init & read
// ═══════════════════════════════════════════════════════════

static bool bh1750_init_at_addr(uint8_t addr) {
    bh_addr = addr;
    Serial.printf("[BH1750] Trying address 0x%02X on port %d...\n", addr, bh_i2c_port);

    // Power on
    if (!bh_write_cmd(BH1750_POWER_ON)) {
        Serial.printf("[BH1750] No response at 0x%02X\n", addr);
        return false;
    }
    delay(10);

    // Reset
    bh_write_cmd(BH1750_RESET);
    delay(10);

    // Start continuous high-resolution measurement
    if (!bh_write_cmd(BH1750_CONT_HRES)) {
        Serial.println("[BH1750] Failed to set measurement mode");
        return false;
    }
    delay(180);  // First measurement takes up to 180ms

    // Try a test read
    uint8_t buf[2];
    if (!bh_read_data(buf, 2)) {
        Serial.println("[BH1750] Test read failed");
        return false;
    }

    float test_lux = ((buf[0] << 8) | buf[1]) / 1.2;
    Serial.printf("[BH1750] Found at 0x%02X — test reading: %.1f lux\n", addr, test_lux);
    return true;
}

static bool bh1750_init() {
    Serial.printf("[BH1750] Using legacy I2C API on port %d (shared with GT911)\n",
                  bh_i2c_port);

    // Try ADDR LOW (0x23) first, then ADDR HIGH (0x5C)
    if (bh1750_init_at_addr(BH1750_ADDR_LOW)) return true;
    if (bh1750_init_at_addr(BH1750_ADDR_HIGH)) return true;

    Serial.println("[BH1750] Not found on either address");
    return false;
}

static bool bh1750_init_with_fallback() {
    if (bh1750_init()) return true;

    Serial.println("[BH1750] Port 0 failed, trying I2C_NUM_1...");
    bh_i2c_port = I2C_NUM_1;
    return bh1750_init();
}

static void bh1750_read() {
    uint8_t buf[2];
    if (!bh_read_data(buf, 2)) return;

    lux = ((buf[0] << 8) | buf[1]) / 1.2;
    bh_reads++;

    if (lux < lux_min) lux_min = lux;
    if (lux > lux_max) lux_max = lux;
}

// ═══════════════════════════════════════════════════════════
//  Light level description (split into two functions to avoid
//  Arduino auto-prototype issues with custom struct returns)
// ═══════════════════════════════════════════════════════════

static const char* get_light_name(float l) {
    if (l < 1)       return "Pitch Dark";
    if (l < 10)      return "Very Dark";
    if (l < 50)      return "Dark / Twilight";
    if (l < 200)     return "Dim Indoor";
    if (l < 500)     return "Normal Indoor";
    if (l < 1000)    return "Bright Indoor";
    if (l < 10000)   return "Overcast / Shade";
    if (l < 25000)   return "Daylight";
    return "Direct Sunlight";
}

static uint32_t get_light_color(float l) {
    if (l < 1)       return 0x222244;
    if (l < 10)      return 0x333366;
    if (l < 50)      return 0x555588;
    if (l < 200)     return 0x7788AA;
    if (l < 500)     return 0x88AACC;
    if (l < 1000)    return 0xAACC88;
    if (l < 10000)   return 0xCCDD66;
    if (l < 25000)   return 0xFFDD44;
    return 0xFFAA22;
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

    // Title
    title_lbl = lv_label_create(scr);
    lv_label_set_text(title_lbl, "GY-30 Light Sensor");
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(0xFFDD44), 0);
    lv_obj_align(title_lbl, LV_ALIGN_TOP_MID, 0, 15);

    // ── Main lux value ──
    lv_obj_t *lux_title = lv_label_create(scr);
    lv_label_set_text(lux_title, "Illuminance");
    lv_obj_set_style_text_font(lux_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lux_title, lv_color_hex(0xFFDD44), 0);
    lv_obj_set_pos(lux_title, 80, 70);

    lux_lbl = lv_label_create(scr);
    lv_label_set_text(lux_lbl, "-- lux");
    lv_obj_set_style_text_font(lux_lbl, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lux_lbl, lv_color_hex(0xFFDD44), 0);
    lv_obj_set_pos(lux_lbl, 80, 95);

    // Foot-candles
    fc_lbl = lv_label_create(scr);
    lv_label_set_text(fc_lbl, "-- fc");
    lv_obj_set_style_text_font(fc_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(fc_lbl, lv_color_hex(0xCCAA33), 0);
    lv_obj_set_pos(fc_lbl, 80, 155);

    // ── Light level description ──
    level_lbl = lv_label_create(scr);
    lv_label_set_text(level_lbl, "Waiting...");
    lv_obj_set_style_text_font(level_lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(level_lbl, lv_color_hex(0x88AACC), 0);
    lv_obj_set_pos(level_lbl, 550, 95);

    // ── Light bar (visual indicator) ──
    bar_obj = lv_obj_create(scr);
    lv_obj_set_size(bar_obj, 860, 30);
    lv_obj_align(bar_obj, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_bg_color(bar_obj, lv_color_hex(0xFFDD44), 0);
    lv_obj_set_style_border_width(bar_obj, 0, 0);
    lv_obj_set_style_radius(bar_obj, 6, 0);

    // ── Min / Max ──
    lv_obj_t *mm_title = lv_label_create(scr);
    lv_label_set_text(mm_title, "Session Min / Max");
    lv_obj_set_style_text_font(mm_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(mm_title, lv_color_hex(0x6688AA), 0);
    lv_obj_set_pos(mm_title, 80, 310);

    min_lbl = lv_label_create(scr);
    lv_label_set_text(min_lbl, "Min: -- lux");
    lv_obj_set_style_text_font(min_lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(min_lbl, lv_color_hex(0x4488CC), 0);
    lv_obj_set_pos(min_lbl, 80, 340);

    max_lbl = lv_label_create(scr);
    lv_label_set_text(max_lbl, "Max: -- lux");
    lv_obj_set_style_text_font(max_lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(max_lbl, lv_color_hex(0xCC8844), 0);
    lv_obj_set_pos(max_lbl, 550, 340);

    // ── Status ──
    status_lbl = lv_label_create(scr);
    lv_label_set_text(status_lbl, "Initializing BH1750...");
    lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x555555), 0);
    lv_obj_align(status_lbl, LV_ALIGN_BOTTOM_MID, 0, -15);
}

static void update_ui() {
    char buf[64];

    // Main lux display
    if (lux < 100) {
        snprintf(buf, sizeof(buf), "%.1f lux", lux);
    } else {
        snprintf(buf, sizeof(buf), "%.0f lux", lux);
    }
    lv_label_set_text(lux_lbl, buf);

    // Foot-candles (1 lux = 0.0929 fc)
    float fc = lux * 0.0929;
    if (fc < 10) {
        snprintf(buf, sizeof(buf), "%.2f fc", fc);
    } else {
        snprintf(buf, sizeof(buf), "%.1f fc", fc);
    }
    lv_label_set_text(fc_lbl, buf);

    // Light level with color
    const char *ll_name = get_light_name(lux);
    uint32_t ll_color = get_light_color(lux);
    lv_label_set_text(level_lbl, ll_name);
    lv_obj_set_style_text_color(level_lbl, lv_color_hex(ll_color), 0);
    lv_obj_set_style_text_color(lux_lbl, lv_color_hex(ll_color), 0);

    // Light bar — log scale, map 0-65535 lux to 10-860 px width
    // Using log10 scale: 0 lux=10px, 65535 lux=860px
    int bar_w = 10;
    if (lux > 0) {
        float log_lux = log10f(lux + 1);         // 0 to ~4.8
        float log_max = log10f(65536.0);          // ~4.8
        bar_w = (int)(10 + (850 * log_lux / log_max));
        if (bar_w > 860) bar_w = 860;
        if (bar_w < 10) bar_w = 10;
    }
    lv_obj_set_width(bar_obj, bar_w);
    lv_obj_set_style_bg_color(bar_obj, lv_color_hex(ll_color), 0);

    // Min / Max
    if (lux_min < 100) {
        snprintf(buf, sizeof(buf), "Min: %.1f lux", lux_min);
    } else {
        snprintf(buf, sizeof(buf), "Min: %.0f lux", lux_min);
    }
    lv_label_set_text(min_lbl, buf);

    if (lux_max < 100) {
        snprintf(buf, sizeof(buf), "Max: %.1f lux", lux_max);
    } else {
        snprintf(buf, sizeof(buf), "Max: %.0f lux", lux_max);
    }
    lv_label_set_text(max_lbl, buf);

    // Status bar
    snprintf(buf, sizeof(buf), "BH1750 @ 0x%02X on I2C1 (shared w/ GT911)  |  Reads: %d",
        bh_addr, bh_reads);
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
    Serial.println("║  CrowPanel P4 — GY-30 (BH1750) Light Test  ║");
    Serial.println("╚═══════════════════════════════════════════╝\n");

    init_hardware();
    build_ui();
    lv_timer_handler();

    bh_ok = bh1750_init_with_fallback();

    if (bh_ok) {
        lv_label_set_text(title_lbl, "GY-30 Light Sensor (BH1750)");
        lv_label_set_text(status_lbl, "BH1750 ready — reading every 500ms");
        lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x44AA44), 0);
    } else {
        lv_label_set_text(title_lbl, "Sensor NOT FOUND");
        lv_label_set_text(status_lbl, "BH1750 FAILED — check I2C1 header wiring (ADDR→GND for 0x23)");
        lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xFF4444), 0);
    }
    lv_timer_handler();

    Serial.println("Setup complete.\n");
}

void loop() {
    // Touch polling
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

    // Sensor read
    static uint32_t last_read = 0;
    if (bh_ok && millis() - last_read >= BH1750_READ_INTERVAL) {
        last_read = millis();
        bh1750_read();
        update_ui();

        float fc = lux * 0.0929;
        Serial.printf("[BH1750] %.1f lux (%.1f fc) — %s  |  Min=%.1f  Max=%.1f\n",
            lux, fc, get_light_name(lux), lux_min, lux_max);
    }

    lv_timer_handler();
    delay(5);
}
