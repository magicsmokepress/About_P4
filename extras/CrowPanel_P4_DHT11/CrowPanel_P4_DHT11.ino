/**
 * CrowPanel Advanced 7" ESP32-P4 — DHT11 Temperature & Humidity
 *
 * Reads a DHT11 sensor on GPIO 9 (wireless module header, right side)
 * and displays temperature + humidity on screen via LVGL.
 *
 * Wiring (wireless module header):
 *   DHT11 VCC  → 3.3V (left column, row 5)
 *   DHT11 DATA → IO2
 *   DHT11 GND  → GND  (left column, row 6)
 *   Add a 10kΩ pull-up resistor between DATA and VCC if your
 *   module doesn't have one built in (most breakout boards do).
 *
 * BOARD SETTINGS:
 *   Board:      "ESP32P4 Dev Module"
 *   PSRAM:      "OPI PSRAM"
 *   Flash Mode: "QIO 80MHz"
 *   Partition:  "Huge APP (3MB No OTA/1MB SPIFFS)"
 */

#include <esp_display_panel.hpp>
#include <esp_lcd_panel_ops.h>
#include <lvgl.h>

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
//  DHT11 config
// ═══════════════════════════════════════════════════════════
#define DHT11_PIN 2
#define DHT11_READ_INTERVAL 2000  // ms between reads (DHT11 min ~1s)

// ═══════════════════════════════════════════════════════════
//  Globals
// ═══════════════════════════════════════════════════════════
static LCD_EK79007 *g_lcd = NULL;
static TouchGT911 *g_touch = NULL;
static lv_display_t *lvgl_disp = NULL;
static lv_indev_t *lvgl_touch_dev = NULL;
static volatile bool touch_pressed = false;
static volatile int16_t touch_x = 0, touch_y = 0;

// Sensor data
static float dht_temp = 0.0;
static float dht_hum = 0.0;
static bool dht_ok = false;
static int dht_errors = 0;
static int dht_reads = 0;

// UI labels
static lv_obj_t *temp_lbl = NULL;
static lv_obj_t *hum_lbl = NULL;
static lv_obj_t *status_lbl = NULL;
static lv_obj_t *temp_icon = NULL;
static lv_obj_t *hum_icon = NULL;

// ═══════════════════════════════════════════════════════════
//  DHT11 bit-bang protocol — hardware timer version
//  (ESP32-P4 at 400MHz is too fast for loop-counting)
// ═══════════════════════════════════════════════════════════
#include "esp_timer.h"

// Get current time in microseconds (hardware timer, not loop counting)
static inline int64_t micros64() {
  return esp_timer_get_time();
}

// Wait for pin to reach expected level, with timeout in microseconds.
// Returns actual time spent waiting in microseconds, or -1 on timeout.
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
  // Pull data line LOW for at least 18ms, then release
  pinMode(DHT11_PIN, OUTPUT);
  digitalWrite(DHT11_PIN, LOW);
  delay(20);  // 20ms start pulse
  digitalWrite(DHT11_PIN, HIGH);
  delayMicroseconds(40);
  pinMode(DHT11_PIN, INPUT_PULLUP);

  // === Wait for DHT11 response ===
  // DHT11 pulls LOW for ~80us, then HIGH for ~80us
  if (dht_wait(DHT11_PIN, LOW, 200) < 0) { Serial.println("[DHT11] No response (wait LOW)"); return false; }
  if (dht_wait(DHT11_PIN, HIGH, 200) < 0) { Serial.println("[DHT11] No response (LOW phase)"); return false; }
  if (dht_wait(DHT11_PIN, LOW, 200) < 0) { Serial.println("[DHT11] No response (HIGH phase)"); return false; }

  // === Read 40 bits (5 bytes) ===
  for (int i = 0; i < 40; i++) {
    // Each bit starts with ~50us LOW
    if (dht_wait(DHT11_PIN, HIGH, 100) < 0) return false;

    // Then HIGH: ~26-28us = 0, ~70us = 1
    int high_us = dht_wait(DHT11_PIN, LOW, 150);
    if (high_us < 0) return false;

    data[i / 8] <<= 1;
    if (high_us > 40) {
      data[i / 8] |= 1;
    }
  }

  // === Debug: print raw bytes ===
  Serial.printf("[DHT11] Raw bytes: %02X %02X %02X %02X %02X\n",
                data[0], data[1], data[2], data[3], data[4]);

  // === Verify checksum ===
  uint8_t checksum = data[0] + data[1] + data[2] + data[3];
  if (checksum != data[4]) {
    Serial.printf("[DHT11] Checksum FAIL: expected %02X got %02X\n", checksum, data[4]);
    return false;
  }

  // DHT11: data[0] = humidity integer, data[1] = humidity decimal (usually 0)
  //         data[2] = temp integer,     data[3] = temp decimal (usually 0)
  hum  = (float)data[0] + (float)data[1] * 0.1;
  temp = (float)data[2] + (float)data[3] * 0.1;

  return true;
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
  lv_obj_t *title = lv_label_create(scr);
  lv_label_set_text(title, "DHT11 Sensor");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x44AACC), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

  // ── Temperature section ──
  temp_icon = lv_label_create(scr);
  lv_label_set_text(temp_icon, LV_SYMBOL_WARNING);  // placeholder icon
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
  lv_obj_set_style_text_font(temp_sub, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(temp_sub, lv_color_hex(0x886644), 0);
  lv_obj_set_pos(temp_sub, 260, 200);

  // ── Humidity section ──
  hum_icon = lv_label_create(scr);
  lv_label_set_text(hum_icon, LV_SYMBOL_WARNING);  // placeholder icon
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
  lv_obj_set_style_text_font(hum_sub, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(hum_sub, lv_color_hex(0x446688), 0);
  lv_obj_set_pos(hum_sub, 620, 200);

  // ── Separator line ──
  lv_obj_t *line = lv_obj_create(scr);
  lv_obj_set_size(line, 700, 2);
  lv_obj_align(line, LV_ALIGN_CENTER, 0, 60);
  lv_obj_set_style_bg_color(line, lv_color_hex(0x333355), 0);
  lv_obj_set_style_border_width(line, 0, 0);
  lv_obj_set_style_radius(line, 0, 0);

  // ── Status line ──
  status_lbl = lv_label_create(scr);
  lv_label_set_text(status_lbl, "Waiting for first reading...");
  lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x888888), 0);
  lv_obj_align(status_lbl, LV_ALIGN_CENTER, 0, 100);

  // ── Pin info (small, bottom) ──
  lv_obj_t *pin_lbl = lv_label_create(scr);
  lv_label_set_text(pin_lbl, "DHT11 on GPIO 2  |  Reads every 2s");
  lv_obj_set_style_text_font(pin_lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(pin_lbl, lv_color_hex(0x555555), 0);
  lv_obj_align(pin_lbl, LV_ALIGN_BOTTOM_MID, 0, -15);
}

static void update_ui() {
  if (!dht_ok && dht_reads == 0) return;  // no data yet

  if (dht_ok) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f °C", dht_temp);
    lv_label_set_text(temp_lbl, buf);

    snprintf(buf, sizeof(buf), "%.1f %%", dht_hum);
    lv_label_set_text(hum_lbl, buf);

    // Color temperature based on value
    uint32_t temp_color = 0xFF6644;  // warm orange
    if (dht_temp < 18.0) temp_color = 0x4488FF;       // cool blue
    else if (dht_temp < 24.0) temp_color = 0x44CC88;  // comfortable green
    else if (dht_temp > 30.0) temp_color = 0xFF4444;   // hot red
    lv_obj_set_style_text_color(temp_lbl, lv_color_hex(temp_color), 0);
    lv_obj_set_style_text_color(temp_icon, lv_color_hex(temp_color), 0);

    char status[80];
    snprintf(status, sizeof(status), "OK  |  Reads: %d  |  Errors: %d", dht_reads, dht_errors);
    lv_label_set_text(status_lbl, status);
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x44AA44), 0);
  } else {
    char status[80];
    snprintf(status, sizeof(status), "Read failed  |  Reads: %d  |  Errors: %d", dht_reads, dht_errors);
    lv_label_set_text(status_lbl, status);
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xFF4444), 0);
  }
}

// ═══════════════════════════════════════════════════════════
//  Setup & Loop
// ═══════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n╔══════════════════════════════════════╗");
  Serial.println("║  CrowPanel P4 — DHT11 Sensor Test     ║");
  Serial.println("╚══════════════════════════════════════╝\n");
  Serial.printf("DHT11 data pin: GPIO %d\n", DHT11_PIN);

  // Check if the data line has a pull-up and sensor is connected
  pinMode(DHT11_PIN, INPUT_PULLUP);
  delay(50);
  int pin_state = digitalRead(DHT11_PIN);
  Serial.printf("DHT11 pin idle state: %s\n", pin_state ? "HIGH (good)" : "LOW (problem - check wiring/pull-up)");

  init_hardware();
  build_ui();
  lv_timer_handler();

  Serial.println("Setup complete. Reading sensor every 2 seconds.\n");
}

void loop() {
  // ── Touch polling ──
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

  // ── DHT11 read ──
  static uint32_t last_dht = 0;
  if (millis() - last_dht >= DHT11_READ_INTERVAL) {
    last_dht = millis();
    dht_reads++;

    float t, h;
    dht_ok = dht11_read(t, h);
    if (dht_ok) {
      dht_temp = t;
      dht_hum = h;
      Serial.printf("[DHT11] Temp: %.1f°C  Humidity: %.1f%%\n", t, h);
    } else {
      dht_errors++;
      Serial.printf("[DHT11] Read failed (error %d/%d)\n", dht_errors, dht_reads);
    }

    update_ui();
  }

  lv_timer_handler();
  delay(10);
}
