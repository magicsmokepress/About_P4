/**
 * CrowPanel Advanced 7" ESP32-P4 - NTP Clock with Timezone Selector
 *
 * Connects to Wi-Fi, syncs time via NTP, and displays a live clock
 * with date. Tap the timezone button to cycle through common zones.
 * Auto-handles DST transitions using POSIX timezone strings.
 *
 * REQUIRED LIBRARIES:
 *   1. ESP32_Display_Panel (v1.0.4+)
 *   2. lvgl (v9.x)
 *   3. Wi-Fi (built-in with ESP32 Arduino Core 3.x)
 *
 * BOARD SETTINGS:
 *   Board: ESP32P4 Dev Module | PSRAM: OPI PSRAM | Flash: QIO 80MHz
 *   Partition: Huge APP (3MB No OTA/1MB SPIFFS) - same as Wi-Fi scanner
 *
 * USAGE:
 *   1. Set your Wi-Fi credentials below (WIFI_SSID / WIFI_PASS)
 *   2. Flash and the clock will auto-sync via NTP
 *   3. Tap the timezone label at bottom to cycle zones
 */

#include <esp_display_panel.hpp>
#include <esp_lcd_panel_ops.h>
#include <lvgl.h>
#include <WiFi.h>
#include <time.h>
#include <sys/time.h>

using namespace esp_panel::drivers;

// ─── Wi-Fi credentials (CHANGE THESE) ───────────────────────────────
#define WIFI_SSID "YOUR_SSID"
#define WIFI_PASS "YOUR_PASSWORD"

// ─── NTP servers ───────────────────────────────────────────────────
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.nist.gov"
#define NTP_SERVER_3 "time.google.com"

// ─── Timezone definitions (POSIX format - auto-handles DST) ───────
struct TZInfo {
  const char *label;      // display name
  const char *posix;      // POSIX TZ string
  const char *abbr;       // short abbreviation
};

static const TZInfo timezones[] = {
  { "US Eastern",   "EST5EDT,M3.2.0,M11.1.0",   "ET"  },
  { "US Central",   "CST6CDT,M3.2.0,M11.1.0",   "CT"  },
  { "US Mountain",  "MST7MDT,M3.2.0,M11.1.0",   "MT"  },
  { "US Pacific",   "PST8PDT,M3.2.0,M11.1.0",   "PT"  },
  { "UTC",          "UTC0",                       "UTC" },
  { "UK GMT/BST",   "GMT0BST,M3.5.0/1,M10.5.0",  "UK"  },
  { "Europe CET",   "CET-1CEST,M3.5.0,M10.5.0/3","CET" },
  { "Japan JST",    "JST-9",                      "JST" },
};
static const int tz_count = sizeof(timezones) / sizeof(timezones[0]);
static int current_tz = 1;  // default: US Central

// ─── Display (proven config) ───────────────────────────────────────
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

// ─── Globals ───────────────────────────────────────────────────────
static LCD_EK79007 *g_lcd = NULL;
static TouchGT911 *g_touch = NULL;
static lv_display_t *lvgl_disp = NULL;
static lv_indev_t *lvgl_touch_dev = NULL;
static volatile bool touch_pressed = false;
static volatile int16_t touch_x = 0, touch_y = 0;

// UI elements
static lv_obj_t *time_lbl = NULL;
static lv_obj_t *date_lbl = NULL;
static lv_obj_t *tz_btn_lbl = NULL;
static lv_obj_t *status_lbl = NULL;
static lv_obj_t *ip_lbl = NULL;

static bool time_synced = false;

// ─── Display + Touch init ──────────────────────────────────────────
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  esp_lcd_panel_handle_t panel = g_lcd->getHandle();
  if (panel) {
    esp_err_t ret;
    do {
      ret = esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1,
                                       area->x2 + 1, area->y2 + 1, px_map);
      if (ret == ESP_ERR_INVALID_STATE) {
        delay(1);
      }
    } while (ret == ESP_ERR_INVALID_STATE);
  }
  lv_display_flush_ready(disp);
}

static void lvgl_touch_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  if (touch_pressed) {
    data->point.x = touch_x;
    data->point.y = touch_y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

static void init_hardware() {
  BusDSI *bus = new BusDSI(
    LCD_DSI_LANE_NUM, LCD_DSI_LANE_RATE,
    LCD_DPI_CLK_MHZ, LCD_COLOR_BITS,
    LCD_WIDTH, LCD_HEIGHT,
    LCD_DPI_HPW, LCD_DPI_HBP, LCD_DPI_HFP,
    LCD_DPI_VPW, LCD_DPI_VBP, LCD_DPI_VFP,
    LCD_DSI_PHY_LDO_ID);
  bus->configDpiFrameBufferNumber(1);
  assert(bus->begin());

  g_lcd = new LCD_EK79007(bus, LCD_WIDTH, LCD_HEIGHT, LCD_COLOR_BITS, LCD_RST_IO);
  assert(g_lcd->begin());

  BacklightPWM_LEDC *bl = new BacklightPWM_LEDC(LCD_BL_IO, LCD_BL_ON_LEVEL);
  assert(bl->begin());
  assert(bl->on());

  BusI2C *touch_bus = new BusI2C(
    TOUCH_I2C_SCL, TOUCH_I2C_SDA,
    (BusI2C::ControlPanelFullConfig)ESP_PANEL_TOUCH_I2C_CONTROL_PANEL_CONFIG(GT911));
  touch_bus->configI2C_FreqHz(TOUCH_I2C_FREQ);
  touch_bus->configI2C_PullupEnable(true, true);
  g_touch = new TouchGT911(touch_bus, LCD_WIDTH, LCD_HEIGHT, TOUCH_RST_IO, TOUCH_INT_IO);
  g_touch->begin();

  lv_init();
  lv_tick_set_cb((lv_tick_get_cb_t)millis);

  size_t buf_size = LCD_WIDTH * LVGL_BUF_LINES * sizeof(lv_color_t);
  uint8_t *buf1 = (uint8_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
  assert(buf1);

  lvgl_disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  lv_display_set_flush_cb(lvgl_disp, lvgl_flush_cb);
  lv_display_set_buffers(lvgl_disp, buf1, NULL, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lvgl_touch_dev = lv_indev_create();
  lv_indev_set_type(lvgl_touch_dev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(lvgl_touch_dev, lvgl_touch_cb);
  lv_indev_set_display(lvgl_touch_dev, lvgl_disp);
}

// ─── Timezone helpers ─────────────────────────────────────────────
static void apply_timezone() {
  setenv("TZ", timezones[current_tz].posix, 1);
  tzset();
  Serial.printf("Timezone set to: %s (%s)\n",
                timezones[current_tz].label, timezones[current_tz].posix);
}

static void tz_cycle_cb(lv_event_t *e) {
  current_tz = (current_tz + 1) % tz_count;
  apply_timezone();

  // Update the button label
  char buf[48];
  snprintf(buf, sizeof(buf), LV_SYMBOL_SETTINGS "  %s", timezones[current_tz].label);
  if (tz_btn_lbl) lv_label_set_text(tz_btn_lbl, buf);
}

// ─── Update clock display ─────────────────────────────────────────
static void update_clock() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    if (time_lbl) lv_label_set_text(time_lbl, "--:--:--");
    if (date_lbl) lv_label_set_text(date_lbl, "Waiting for NTP sync...");
    return;
  }

  if (!time_synced) {
    time_synced = true;
    if (status_lbl) {
      lv_label_set_text(status_lbl, LV_SYMBOL_OK "  NTP synced");
      lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x00E676), 0);
    }
    Serial.println("NTP time synced successfully");
  }

  // Time (HH:MM:SS)
  char time_buf[16];
  strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &timeinfo);
  if (time_lbl) lv_label_set_text(time_lbl, time_buf);

  // Date line
  char date_buf[48];
  strftime(date_buf, sizeof(date_buf), "%a %b %d, %Y", &timeinfo);
  if (date_lbl) lv_label_set_text(date_lbl, date_buf);

  // Also print to serial once per minute
  static int last_min = -1;
  if (timeinfo.tm_min != last_min) {
    last_min = timeinfo.tm_min;
    char full[48];
    strftime(full, sizeof(full), "%Y-%m-%d %H:%M:%S %Z", &timeinfo);
    Serial.printf("Time: %s\n", full);
  }
}

// ─── Build UI ─────────────────────────────────────────────────────
static void build_ui() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A0A1A), 0);

  // ─── Top bar: Wi-Fi status + IP ───
  status_lbl = lv_label_create(scr);
  lv_label_set_text(status_lbl, LV_SYMBOL_WIFI "  Connecting...");
  lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xFFEB3B), 0);
  lv_obj_align(status_lbl, LV_ALIGN_TOP_LEFT, 20, 12);

  ip_lbl = lv_label_create(scr);
  lv_label_set_text(ip_lbl, "");
  lv_obj_set_style_text_font(ip_lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(ip_lbl, lv_color_hex(0x888888), 0);
  lv_obj_align(ip_lbl, LV_ALIGN_TOP_RIGHT, -20, 12);

  // ─── Time display (HH:MM:SS) ───
  time_lbl = lv_label_create(scr);
  lv_label_set_text(time_lbl, "--:--:--");
  lv_obj_set_style_text_font(time_lbl, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(time_lbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(time_lbl, LV_ALIGN_CENTER, 0, -40);

  // ─── Date line ───
  date_lbl = lv_label_create(scr);
  lv_label_set_text(date_lbl, "Waiting for NTP sync...");
  lv_obj_set_style_text_font(date_lbl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(date_lbl, lv_color_hex(0xBBBBBB), 0);
  lv_obj_align(date_lbl, LV_ALIGN_CENTER, 0, 10);

  // ─── Timezone selector button (bottom) ───
  lv_obj_t *tz_btn = lv_button_create(scr);
  lv_obj_set_size(tz_btn, 350, 44);
  lv_obj_align(tz_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_set_style_bg_color(tz_btn, lv_color_hex(0x1A1A3E), 0);
  lv_obj_set_style_bg_color(tz_btn, lv_color_hex(0x2A2A5E), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(tz_btn, lv_color_hex(0x444488), 0);
  lv_obj_set_style_border_width(tz_btn, 1, 0);
  lv_obj_set_style_radius(tz_btn, 10, 0);
  lv_obj_add_event_cb(tz_btn, tz_cycle_cb, LV_EVENT_CLICKED, NULL);

  tz_btn_lbl = lv_label_create(tz_btn);
  char buf[48];
  snprintf(buf, sizeof(buf), LV_SYMBOL_SETTINGS "  %s", timezones[current_tz].label);
  lv_label_set_text(tz_btn_lbl, buf);
  lv_obj_set_style_text_font(tz_btn_lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(tz_btn_lbl, lv_color_hex(0x64B5F6), 0);
  lv_obj_center(tz_btn_lbl);
}

// ─── Setup - EXACT same order as working Wi-Fi scanner ─────────────
static bool ntp_configured = false;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("CrowPanel ESP32-P4 NTP Clock");
  Serial.printf("Free heap: %u  Free PSRAM: %u\n",
                ESP.getFreeHeap(), ESP.getFreePsram());

  // Display + touch first (same as Wi-Fi scanner)
  init_hardware();
  build_ui();

  Serial.printf("After display - Free heap: %u  Free PSRAM: %u\n",
                ESP.getFreeHeap(), ESP.getFreePsram());

  // Apply timezone
  apply_timezone();

  // Wi-Fi init - EXACT same pattern as working Wi-Fi scanner
  Serial.println("Initialising Wi-Fi (SDIO -> C6)...");
  if (status_lbl) lv_label_set_text(status_lbl, LV_SYMBOL_WIFI "  SDIO init...");
  lv_timer_handler();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(3000);  // SDIO settling

  Serial.printf("Wi-Fi status after init: %d\n", WiFi.status());

  // Now connect
  Serial.printf("Connecting to Wi-Fi: %s\n", WIFI_SSID);
  if (status_lbl) lv_label_set_text(status_lbl, LV_SYMBOL_WIFI "  Connecting...");
  lv_timer_handler();

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
    Serial.print(".");
    lv_timer_handler();
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    if (status_lbl) {
      lv_label_set_text(status_lbl, LV_SYMBOL_WIFI "  Connected");
      lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x00E676), 0);
    }
    if (ip_lbl) {
      char buf[48];
      snprintf(buf, sizeof(buf), "IP: %s  |  RSSI: %d dBm",
               WiFi.localIP().toString().c_str(), WiFi.RSSI());
      lv_label_set_text(ip_lbl, buf);
    }

    // Configure NTP
    configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
    ntp_configured = true;
    Serial.println("NTP configured, waiting for sync...");
    if (date_lbl) lv_label_set_text(date_lbl, "Syncing with NTP server...");
  } else {
    Serial.println("Wi-Fi connection failed!");
    if (status_lbl) {
      lv_label_set_text(status_lbl, LV_SYMBOL_CLOSE "  Wi-Fi failed");
      lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xFF5252), 0);
    }
    if (date_lbl) lv_label_set_text(date_lbl, "No Wi-Fi - clock will show --:--");
  }

  Serial.println("Setup complete");
}

// ─── Loop ─────────────────────────────────────────────────────────
static uint32_t last_clock_update = 0;
static uint32_t touch_release_time = 0;
static const uint32_t TOUCH_DEBOUNCE_MS = 150;

void loop() {
  // Poll touch with debounce
  if (g_touch) {
    esp_lcd_touch_handle_t tp = g_touch->getHandle();
    if (tp) {
      uint16_t x[1], y[1], strength[1];
      uint8_t cnt = 0;
      esp_lcd_touch_read_data(tp);
      if (esp_lcd_touch_get_coordinates(tp, x, y, strength, &cnt, 1) && cnt > 0) {
        if (touch_pressed || (millis() - touch_release_time) > TOUCH_DEBOUNCE_MS) {
          touch_pressed = true;
          touch_x = x[0];
          touch_y = y[0];
        }
      } else {
        if (touch_pressed) {
          touch_release_time = millis();
        }
        touch_pressed = false;
      }
    }
  }

  // Update clock display every 200ms (smooth seconds update)
  if (millis() - last_clock_update > 200) {
    last_clock_update = millis();
    update_clock();
  }

  lv_timer_handler();
  delay(10);
}
