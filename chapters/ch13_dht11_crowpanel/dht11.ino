#include <esp_display_panel.hpp>
#include <esp_lcd_panel_ops.h>
#include <lvgl.h>
#include "esp_timer.h"

using namespace esp_panel::drivers;

// ─── Display + Touch config (standard boilerplate) ────────────────
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

// ─── DHT11 config ─────────────────────────────────────────────────
#define DHT11_PIN 2
#define DHT11_READ_INTERVAL 2000  // ms between reads

// ─── Globals ──────────────────────────────────────────────────────
static LCD_EK79007 *g_lcd = NULL;
static TouchGT911 *g_touch = NULL;
static lv_display_t *lvgl_disp = NULL;
static lv_indev_t *lvgl_touch_dev = NULL;
static volatile bool touch_pressed = false;
static volatile int16_t touch_x = 0, touch_y = 0;

static float dht_temp = 0.0;
static float dht_hum = 0.0;
static bool dht_ok = false;
static int dht_errors = 0;
static int dht_reads = 0;

static lv_obj_t *temp_lbl = NULL;
static lv_obj_t *hum_lbl = NULL;
static lv_obj_t *status_lbl = NULL;

// ─── Hardware timer-based pulse measurement ───────────────────────
static inline int64_t micros64() {
    return esp_timer_get_time();
}

// Wait for pin to reach target level. Returns time spent waiting
// in microseconds, or -1 on timeout.
static int dht_wait(int pin, int level, int timeout_us) {
    int64_t start = micros64();
    while (digitalRead(pin) != level) {
        if ((micros64() - start) > timeout_us) return -1;
    }
    return (int)(micros64() - start);
}

// Read 5 bytes from DHT11. Returns true on success.
static bool dht11_read(float &temp, float &hum) {
    uint8_t data[5] = {0};

    // === Start signal ===
    // Pull data line LOW for 20ms, then release
    pinMode(DHT11_PIN, OUTPUT);
    digitalWrite(DHT11_PIN, LOW);
    delay(20);
    digitalWrite(DHT11_PIN, HIGH);
    delayMicroseconds(40);
    pinMode(DHT11_PIN, INPUT_PULLUP);

    // === Wait for DHT11 response ===
    // DHT11 pulls LOW ~80µs, then HIGH ~80µs
    if (dht_wait(DHT11_PIN, LOW, 200) < 0) return false;
    if (dht_wait(DHT11_PIN, HIGH, 200) < 0) return false;
    if (dht_wait(DHT11_PIN, LOW, 200) < 0) return false;

    // === Read 40 bits (5 bytes) ===
    for (int i = 0; i < 40; i++) {
        // Each bit starts with ~50µs LOW
        if (dht_wait(DHT11_PIN, HIGH, 100) < 0) return false;

        // Then HIGH: ~26µs = 0, ~70µs = 1
        int high_us = dht_wait(DHT11_PIN, LOW, 150);
        if (high_us < 0) return false;

        // Build each byte one bit at a time: shift existing bits left
        // to make room, then OR in the new bit (1 if HIGH was long)
        data[i / 8] <<= 1;
        if (high_us > 40) {
            data[i / 8] |= 1;
        }
    }

    // === Verify checksum ===
    uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    if (checksum != data[4]) return false;

    hum  = (float)data[0] + (float)data[1] * 0.1;
    temp = (float)data[2] + (float)data[3] * 0.1;
    return true;
}

// (init_hardware() - same boilerplate as Chapter 7, omitted for brevity)

// ─── UI ───────────────────────────────────────────────────────────
static void build_ui() {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A0A1A), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "DHT11 Sensor");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x44AACC), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Temperature (left side)
    lv_obj_t *temp_icon = lv_label_create(scr);
    lv_label_set_text(temp_icon, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_font(temp_icon, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(temp_icon, lv_color_hex(0xFF6644), 0);
    lv_obj_set_pos(temp_icon, 180, 140);

    temp_lbl = lv_label_create(scr);
    lv_label_set_text(temp_lbl, "-- . - °C");
    lv_obj_set_style_text_font(temp_lbl, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(temp_lbl, lv_color_hex(0xFF6644), 0);
    lv_obj_set_pos(temp_lbl, 260, 140);

    lv_obj_t *temp_sub = lv_label_create(scr);
    lv_label_set_text(temp_sub, "Temperature");
    lv_obj_set_style_text_font(temp_sub, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(temp_sub, lv_color_hex(0x886644), 0);
    lv_obj_set_pos(temp_sub, 260, 200);

    // Humidity (right side)
    lv_obj_t *hum_icon = lv_label_create(scr);
    lv_label_set_text(hum_icon, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_font(hum_icon, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(hum_icon, lv_color_hex(0x44AAFF), 0);
    lv_obj_set_pos(hum_icon, 540, 140);

    hum_lbl = lv_label_create(scr);
    lv_label_set_text(hum_lbl, "-- . - %%");
    lv_obj_set_style_text_font(hum_lbl, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(hum_lbl, lv_color_hex(0x44AAFF), 0);
    lv_obj_set_pos(hum_lbl, 620, 140);

    lv_obj_t *hum_sub = lv_label_create(scr);
    lv_label_set_text(hum_sub, "Humidity");
    lv_obj_set_style_text_font(hum_sub, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(hum_sub, lv_color_hex(0x446688), 0);
    lv_obj_set_pos(hum_sub, 620, 200);

    // Status line
    status_lbl = lv_label_create(scr);
    lv_label_set_text(status_lbl, "Waiting for first reading...");
    lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x888888), 0);
    lv_obj_align(status_lbl, LV_ALIGN_CENTER, 0, 100);

    // Pin info
    lv_obj_t *pin_lbl = lv_label_create(scr);
    lv_label_set_text(pin_lbl,
        "DHT11 on GPIO 2  |  Reads every 2s");
    lv_obj_set_style_text_font(pin_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pin_lbl, lv_color_hex(0x555555), 0);
    lv_obj_align(pin_lbl, LV_ALIGN_BOTTOM_MID, 0, -15);
}

static void update_ui() {
    if (dht_ok) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f °C", dht_temp);
        lv_label_set_text(temp_lbl, buf);

        snprintf(buf, sizeof(buf), "%.1f %%", dht_hum);
        lv_label_set_text(hum_lbl, buf);

        // Color temperature by comfort zone
        uint32_t tc = 0xFF6644;                       // warm orange
        if (dht_temp < 18.0) tc = 0x4488FF;           // cold blue
        else if (dht_temp < 24.0) tc = 0x44CC88;      // comfortable green
        else if (dht_temp > 30.0) tc = 0xFF4444;       // hot red
        lv_obj_set_style_text_color(temp_lbl,
            lv_color_hex(tc), 0);

        char status[80];
        snprintf(status, sizeof(status),
            "OK  |  Reads: %d  |  Errors: %d",
            dht_reads, dht_errors);
        lv_label_set_text(status_lbl, status);
        lv_obj_set_style_text_color(status_lbl,
            lv_color_hex(0x44AA44), 0);
    } else {
        char status[80];
        snprintf(status, sizeof(status),
            "Read failed  |  Reads: %d  |  Errors: %d",
            dht_reads, dht_errors);
        lv_label_set_text(status_lbl, status);
        lv_obj_set_style_text_color(status_lbl,
            lv_color_hex(0xFF4444), 0);
    }
}

// ─── Setup ────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("CrowPanel P4 - DHT11 Sensor");
    Serial.printf("DHT11 data pin: GPIO %d\n", DHT11_PIN);

    // Check if data line has a pull-up
    pinMode(DHT11_PIN, INPUT_PULLUP);
    delay(50);
    int pin_state = digitalRead(DHT11_PIN);
    Serial.printf("Pin idle state: %s\n",
        pin_state ? "HIGH (good)" : "LOW (problem - check wiring)");

    init_hardware();
    build_ui();
    lv_timer_handler();
}

// ─── Loop ─────────────────────────────────────────────────────────
void loop() {
    // Touch polling
    static uint32_t lt = 0;
    if (millis() - lt > 30) {
        lt = millis();
        if (g_touch) {
            esp_panel::drivers::TouchPoint tp[1];
            int pts = g_touch->readPoints(tp, 1);
            touch_pressed = (pts > 0);
            if (touch_pressed) {
                touch_x = tp[0].x;
                touch_y = tp[0].y;
            }
        }
    }

    // DHT11 read every 2 seconds
    static uint32_t last_dht = 0;
    if (millis() - last_dht >= DHT11_READ_INTERVAL) {
        last_dht = millis();
        dht_reads++;

        float t, h;
        dht_ok = dht11_read(t, h);
        if (dht_ok) {
            dht_temp = t;
            dht_hum = h;
            Serial.printf("[DHT11] Temp: %.1f°C  Humidity: %.1f%%\n",
                          t, h);
        } else {
            dht_errors++;
            Serial.printf("[DHT11] Read failed (%d/%d errors)\n",
                          dht_errors, dht_reads);
        }

        update_ui();
    }

    lv_timer_handler();
    delay(10);
}
