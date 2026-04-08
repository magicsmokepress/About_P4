/**
 * CrowPanel Advanced 7" ESP32-P4 — LVGL LED Control via Touch
 *
 * Two large buttons (ON / OFF) control the onboard LED on GPIO 48.
 * A status indicator and label show the current LED state.
 * Touch coordinates are logged to serial for debugging.
 *
 * Based on the confirmed-working Hello World sketch.
 *
 * REQUIRED LIBRARIES (install via Arduino Library Manager):
 *   1. ESP32_Display_Panel   by esp-arduino-libs (v1.0.4+)
 *   2. lvgl                  (v9.x)
 *
 * BOARD SETTINGS in Arduino IDE (all critical!):
 *   Board:      "ESP32P4 Dev Module"  (requires ESP32 Arduino Core 3.x)
 *   PSRAM:      "OPI PSRAM"          ← MUST be enabled
 *   Flash Mode: "QIO 80MHz"
 *   Partition:  "Huge APP (3MB No OTA/1MB SPIFFS)"
 *
 * NOTE: lv_conf.h must be in Arduino/libraries/ folder.
 */

#include <esp_display_panel.hpp>
#include <lvgl.h>

using namespace esp_panel::drivers;

// ─── Display Timings (confirmed working) ───────────────────────────
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

// ─── Backlight ─────────────────────────────────────────────────────
#define LCD_BL_IO 31
#define LCD_BL_ON_LEVEL 1

// ─── Touch (GT911) ─────────────────────────────────────────────────
#define TOUCH_I2C_SDA 45
#define TOUCH_I2C_SCL 46
#define TOUCH_I2C_FREQ (400 * 1000)
#define TOUCH_RST_IO 40
#define TOUCH_INT_IO 42

// ─── LED ───────────────────────────────────────────────────────────
#define LED_GPIO 48

// ─── LVGL ──────────────────────────────────────────────────────────
#define LVGL_BUF_LINES 60

// Global driver pointers
static LCD_EK79007 *g_lcd = NULL;
static TouchGT911 *g_touch = NULL;
static lv_display_t *lvgl_disp = NULL;
static lv_indev_t *lvgl_touch = NULL;

// UI elements we need to update
static lv_obj_t *status_led = NULL;
static lv_obj_t *status_label = NULL;
static bool led_state = false;

// ─── LVGL flush callback ──────────────────────────────────────────
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  g_lcd->drawBitmap(area->x1, area->y1,
                    lv_area_get_width(area), lv_area_get_height(area),
                    (const uint8_t *)px_map);
  lv_display_flush_ready(disp);
}

// ─── Touch state (fed manually from loop) ─────────────────────────
static volatile bool     touch_pressed = false;
static volatile int16_t  touch_x = 0;
static volatile int16_t  touch_y = 0;

// LVGL read callback — just returns the last polled state
static void lvgl_touch_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  if (touch_pressed) {
    data->point.x = touch_x;
    data->point.y = touch_y;
    data->state   = LV_INDEV_STATE_PRESSED;
  } else {
    data->state   = LV_INDEV_STATE_RELEASED;
  }
}

// ─── LED helpers ──────────────────────────────────────────────────
static void set_led(bool on) {
  led_state = on;
  digitalWrite(LED_GPIO, on ? HIGH : LOW);

  // Update UI
  if (status_led) {
    lv_obj_set_style_bg_color(status_led,
                              on ? lv_color_hex(0x00E676) : lv_color_hex(0x616161), 0);
  }
  if (status_label) {
    lv_label_set_text(status_label, on ? "LED is ON" : "LED is OFF");
    lv_obj_set_style_text_color(status_label,
                                on ? lv_color_hex(0x00E676) : lv_color_hex(0x616161), 0);
  }

  Serial.printf("LED → %s\n", on ? "ON" : "OFF");
}

// ─── LVGL button event handlers ───────────────────────────────────
static void btn_on_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    set_led(true);
  }
}

static void btn_off_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    set_led(false);
  }
}

// ─── Build the UI ─────────────────────────────────────────────────
static void build_ui(void) {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x1A1A2E), 0);

  // ── Title ──
  lv_obj_t *title = lv_label_create(scr);
  lv_label_set_text(title, "CrowPanel LED Control");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

  // ── Status LED indicator (circle) ──
  status_led = lv_obj_create(scr);
  lv_obj_set_size(status_led, 80, 80);
  lv_obj_set_style_radius(status_led, 40, 0);                        // circle
  lv_obj_set_style_bg_color(status_led, lv_color_hex(0x616161), 0);  // grey = off
  lv_obj_set_style_border_width(status_led, 3, 0);
  lv_obj_set_style_border_color(status_led, lv_color_hex(0x333333), 0);
  lv_obj_set_style_shadow_width(status_led, 20, 0);
  lv_obj_set_style_shadow_color(status_led, lv_color_hex(0x00E676), 0);
  lv_obj_set_style_shadow_opa(status_led, 0, 0);  // hidden until ON
  lv_obj_align(status_led, LV_ALIGN_TOP_MID, 0, 70);
  lv_obj_remove_flag(status_led, LV_OBJ_FLAG_SCROLLABLE);

  // ── Status label ──
  status_label = lv_label_create(scr);
  lv_label_set_text(status_label, "LED is OFF");
  lv_obj_set_style_text_font(status_label, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(status_label, lv_color_hex(0x616161), 0);
  lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 165);

  // ── ON button ──
  lv_obj_t *btn_on = lv_button_create(scr);
  lv_obj_set_size(btn_on, 280, 130);
  lv_obj_align(btn_on, LV_ALIGN_BOTTOM_LEFT, 120, -80);
  lv_obj_set_style_bg_color(btn_on, lv_color_hex(0x00C853), 0);  // green
  lv_obj_set_style_bg_color(btn_on, lv_color_hex(0x00E676), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_on, 20, 0);
  lv_obj_set_style_shadow_width(btn_on, 10, 0);
  lv_obj_set_style_shadow_color(btn_on, lv_color_hex(0x00C853), 0);
  lv_obj_add_event_cb(btn_on, btn_on_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *lbl_on = lv_label_create(btn_on);
  lv_label_set_text(lbl_on, "ON");
  lv_obj_set_style_text_font(lbl_on, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_color(lbl_on, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(lbl_on);

  // ── OFF button ──
  lv_obj_t *btn_off = lv_button_create(scr);
  lv_obj_set_size(btn_off, 280, 130);
  lv_obj_align(btn_off, LV_ALIGN_BOTTOM_RIGHT, -120, -80);
  lv_obj_set_style_bg_color(btn_off, lv_color_hex(0xD32F2F), 0);  // red
  lv_obj_set_style_bg_color(btn_off, lv_color_hex(0xEF5350), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_off, 20, 0);
  lv_obj_set_style_shadow_width(btn_off, 10, 0);
  lv_obj_set_style_shadow_color(btn_off, lv_color_hex(0xD32F2F), 0);
  lv_obj_add_event_cb(btn_off, btn_off_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *lbl_off = lv_label_create(btn_off);
  lv_label_set_text(lbl_off, "OFF");
  lv_obj_set_style_text_font(lbl_off, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_color(lbl_off, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(lbl_off);

  // ── GPIO label (small, bottom) ──
  lv_obj_t *gpio_lbl = lv_label_create(scr);
  lv_label_set_text(gpio_lbl, "GPIO 48");
  lv_obj_set_style_text_font(gpio_lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(gpio_lbl, lv_color_hex(0x888888), 0);
  lv_obj_align(gpio_lbl, LV_ALIGN_BOTTOM_MID, 0, -20);
}

// ─── Setup ────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("CrowPanel ESP32-P4 LED Control");

  // ── 0. GPIO for LED ─────────────────────────────────────────────
  pinMode(LED_GPIO, OUTPUT);
  digitalWrite(LED_GPIO, LOW);

  // ── 1. MIPI-DSI bus ─────────────────────────────────────────────
  BusDSI *bus = new BusDSI(
    LCD_DSI_LANE_NUM, LCD_DSI_LANE_RATE,
    LCD_DPI_CLK_MHZ, LCD_COLOR_BITS,
    LCD_WIDTH, LCD_HEIGHT,
    LCD_DPI_HPW, LCD_DPI_HBP, LCD_DPI_HFP,
    LCD_DPI_VPW, LCD_DPI_VBP, LCD_DPI_VFP,
    LCD_DSI_PHY_LDO_ID);
  bus->configDpiFrameBufferNumber(1);
  assert(bus->begin());

  // ── 2. LCD (EK79007) ────────────────────────────────────────────
  g_lcd = new LCD_EK79007(bus, LCD_WIDTH, LCD_HEIGHT, LCD_COLOR_BITS, LCD_RST_IO);
  assert(g_lcd->begin());

  // ── 3. Backlight ────────────────────────────────────────────────
  BacklightPWM_LEDC *backlight = new BacklightPWM_LEDC(LCD_BL_IO, LCD_BL_ON_LEVEL);
  assert(backlight->begin());
  assert(backlight->on());

  // ── 4. Touch (GT911) ────────────────────────────────────────────
  Serial.println("Initializing touch...");
  BusI2C *touch_bus = new BusI2C(
    TOUCH_I2C_SCL, TOUCH_I2C_SDA,
    (BusI2C::ControlPanelFullConfig)ESP_PANEL_TOUCH_I2C_CONTROL_PANEL_CONFIG(GT911));
  touch_bus->configI2C_FreqHz(TOUCH_I2C_FREQ);
  touch_bus->configI2C_PullupEnable(true, true);

  g_touch = new TouchGT911(touch_bus, LCD_WIDTH, LCD_HEIGHT, TOUCH_RST_IO, TOUCH_INT_IO);
  bool touch_ok = g_touch->begin();
  Serial.println(touch_ok ? "  Touch OK" : "  Touch FAILED");

  // ── 5. LVGL ─────────────────────────────────────────────────────
  lv_init();

  // LVGL 9 needs an explicit tick callback — the lv_conf.h macros alone
  // may not work. This tells LVGL to use millis() for its tick source.
  lv_tick_set_cb((lv_tick_get_cb_t)millis);

  size_t buf_size = LCD_WIDTH * LVGL_BUF_LINES * sizeof(lv_color_t);
  uint8_t *buf1 = (uint8_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
  assert(buf1);

  lvgl_disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  lv_display_set_flush_cb(lvgl_disp, lvgl_flush_cb);
  lv_display_set_buffers(lvgl_disp, buf1, NULL, buf_size,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  // Register LVGL input device — fed manually from loop()
  lvgl_touch = lv_indev_create();
  lv_indev_set_type(lvgl_touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(lvgl_touch, lvgl_touch_cb);
  lv_indev_set_display(lvgl_touch, lvgl_disp);
  Serial.println("  LVGL indev registered");

  // ── 6. Build UI ─────────────────────────────────────────────────
  build_ui();

  Serial.println("Setup complete — tap ON/OFF to control LED");
}

// ─── Loop ─────────────────────────────────────────────────────────
void loop() {
  // Poll GT911 directly and update touch state for LVGL
  if (g_touch) {
    esp_lcd_touch_handle_t tp = g_touch->getHandle();
    if (tp) {
      uint16_t x[1], y[1], strength[1];
      uint8_t cnt = 0;
      esp_lcd_touch_read_data(tp);
      if (esp_lcd_touch_get_coordinates(tp, x, y, strength, &cnt, 1) && cnt > 0) {
        touch_pressed = true;
        touch_x = x[0];
        touch_y = y[0];
      } else {
        touch_pressed = false;
      }
    }
  }

  lv_timer_handler();
  delay(5);
}
