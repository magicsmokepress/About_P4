/**
 * CrowPanel Advanced 7" ESP32-P4 — BLE Test
 *
 * Tests whether BLE works on ESP32-P4 via the ESP32-C6 co-processor.
 * The P4 has no native Bluetooth — all BLE goes through C6 via SDIO.
 *
 * Arduino-ESP32 core has partial BLE support for P4 via hosted HCI.
 * This sketch tests if BLEDevice::init() actually works.
 *
 * If it crashes, the old assessment stands (BLE not ready).
 * If it works, we get a BLE scanner!
 *
 * BOARD SETTINGS: Default partition, 16MB flash, OPI PSRAM
 */

#include <esp_display_panel.hpp>
#include <esp_lcd_panel_ops.h>
#include <lvgl.h>

// BLE includes — these may or may not compile on P4
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

using namespace esp_panel::drivers;

// ═══════════════════════════════════════════════════════════
//  Display + Touch config (proven)
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
//  Globals
// ═══════════════════════════════════════════════════════════
static LCD_EK79007 *g_lcd = NULL;
static TouchGT911 *g_touch = NULL;
static lv_display_t *lvgl_disp = NULL;
static lv_indev_t *lvgl_touch_dev = NULL;
static volatile bool touch_pressed = false;
static volatile int16_t touch_x = 0, touch_y = 0;

// BLE state
static BLEScan *pBLEScan = NULL;
static bool ble_ready = false;
static bool ble_scanning = false;

// Device list
#define MAX_DEVICES 30
struct BLEDev {
  char name[40];
  char addr[20];
  int rssi;
};
static BLEDev devices[MAX_DEVICES];
static int device_count = 0;

// UI
static lv_obj_t *title_lbl = NULL;
static lv_obj_t *status_lbl = NULL;
static lv_obj_t *list_lbl = NULL;
static lv_obj_t *count_lbl = NULL;

// ═══════════════════════════════════════════════════════════
//  Display + Touch init (proven)
// ═══════════════════════════════════════════════════════════
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *a, uint8_t *px) {
  esp_lcd_panel_handle_t p = g_lcd->getHandle();
  if (p) {
    esp_err_t r;
    do {
      r = esp_lcd_panel_draw_bitmap(p, a->x1, a->y1, a->x2+1, a->y2+1, px);
      if (r == ESP_ERR_INVALID_STATE) delay(1);
    } while (r == ESP_ERR_INVALID_STATE);
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

  BusI2C *tb = new BusI2C(
    TOUCH_I2C_SCL, TOUCH_I2C_SDA,
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
//  BLE
// ═══════════════════════════════════════════════════════════

class ScanCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (device_count >= MAX_DEVICES) return;

    // Check if already in list
    const char *addr = advertisedDevice.getAddress().toString().c_str();
    for (int i = 0; i < device_count; i++) {
      if (strcmp(devices[i].addr, addr) == 0) {
        devices[i].rssi = advertisedDevice.getRSSI();  // Update RSSI
        return;
      }
    }

    // Add new device
    String name = advertisedDevice.getName().c_str();
    if (name.length() == 0) name = "(unknown)";
    strncpy(devices[device_count].name, name.c_str(), sizeof(devices[0].name) - 1);
    strncpy(devices[device_count].addr, addr, sizeof(devices[0].addr) - 1);
    devices[device_count].rssi = advertisedDevice.getRSSI();
    device_count++;

    Serial.printf("[BLE] Found: %s (%s) RSSI=%d\n",
                  devices[device_count - 1].name,
                  devices[device_count - 1].addr,
                  devices[device_count - 1].rssi);
  }
};

static bool init_ble() {
  Serial.println("[BLE] Attempting BLEDevice::init()...");
  Serial.println("[BLE] (This will crash if BLE is not supported)");

  BLEDevice::init("CrowPanel-P4");

  Serial.println("[BLE] init() returned OK!");

  pBLEScan = BLEDevice::getScan();
  if (!pBLEScan) {
    Serial.println("[BLE] getScan() failed");
    return false;
  }

  pBLEScan->setAdvertisedDeviceCallbacks(new ScanCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  Serial.println("[BLE] Scanner configured OK");
  return true;
}

static void do_scan() {
  if (!ble_ready) {
    Serial.println("[BLE] Not ready");
    return;
  }

  Serial.println("[BLE] Starting 5-second scan...");
  ble_scanning = true;
  device_count = 0;

  BLEScanResults *results = pBLEScan->start(5, false);
  ble_scanning = false;

  Serial.printf("[BLE] Scan complete: %d devices found\n", device_count);
  pBLEScan->clearResults();
}

// ═══════════════════════════════════════════════════════════
//  UI
// ═══════════════════════════════════════════════════════════

static void cb_scan(lv_event_t *e) { do_scan(); }

static void build_ui() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A0A1A), 0);

  title_lbl = lv_label_create(scr);
  lv_label_set_text(title_lbl, LV_SYMBOL_BLUETOOTH " BLE Scanner");
  lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(title_lbl, lv_color_hex(0x4488FF), 0);
  lv_obj_set_pos(title_lbl, 20, 15);

  status_lbl = lv_label_create(scr);
  lv_label_set_text(status_lbl, "Initializing BLE...");
  lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xFFAA00), 0);
  lv_obj_set_pos(status_lbl, 20, 55);

  count_lbl = lv_label_create(scr);
  lv_label_set_text(count_lbl, "");
  lv_obj_set_style_text_font(count_lbl, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(count_lbl, lv_color_hex(0xCCCCCC), 0);
  lv_obj_set_pos(count_lbl, 20, 85);

  // Scan button
  lv_obj_t *btn = lv_button_create(scr);
  lv_obj_set_size(btn, 180, 55);
  lv_obj_set_pos(btn, 800, 15);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x2244AA), 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x4466CC), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn, 8, 0);
  lv_obj_add_event_cb(btn, cb_scan, LV_EVENT_CLICKED, NULL);
  lv_obj_t *bl = lv_label_create(btn);
  lv_label_set_text(bl, LV_SYMBOL_REFRESH " Scan");
  lv_obj_set_style_text_font(bl, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(bl, lv_color_hex(0xDDDDDD), 0);
  lv_obj_center(bl);

  // Device list
  list_lbl = lv_label_create(scr);
  lv_label_set_text(list_lbl, "");
  lv_obj_set_style_text_font(list_lbl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(list_lbl, lv_color_hex(0xAABBCC), 0);
  lv_obj_set_pos(list_lbl, 20, 120);
  lv_obj_set_width(list_lbl, 980);
  lv_label_set_long_mode(list_lbl, LV_LABEL_LONG_WRAP);

  // Serial help
  lv_obj_t *help = lv_label_create(scr);
  lv_label_set_text(help, "Serial: s=scan  ?=help");
  lv_obj_set_style_text_font(help, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(help, lv_color_hex(0x555555), 0);
  lv_obj_set_pos(help, 20, 570);
}

static void update_ui() {
  if (ble_ready) {
    if (ble_scanning) {
      lv_label_set_text(status_lbl, "Scanning...");
      lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x4488FF), 0);
    } else {
      lv_label_set_text(status_lbl, "BLE Ready (via C6 co-processor)");
      lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x00FF88), 0);
    }
  } else {
    lv_label_set_text(status_lbl, "BLE FAILED — not supported on this core version");
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xFF4444), 0);
  }

  // Device count
  char buf[64];
  snprintf(buf, sizeof(buf), "Devices found: %d", device_count);
  lv_label_set_text(count_lbl, buf);

  // Device list
  if (device_count > 0) {
    static char list_buf[3000];
    int pos = 0;
    for (int i = 0; i < device_count && pos < (int)sizeof(list_buf) - 100; i++) {
      pos += snprintf(list_buf + pos, sizeof(list_buf) - pos,
                      "%2d. %-25s %s  %ddBm\n",
                      i + 1, devices[i].name, devices[i].addr, devices[i].rssi);
    }
    list_buf[pos] = 0;
    lv_label_set_text(list_lbl, list_buf);
  }
}

// ═══════════════════════════════════════════════════════════
//  Setup & Loop
// ═══════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n╔══════════════════════════════════════╗");
  Serial.println("║  CrowPanel P4 — BLE Test               ║");
  Serial.println("╚══════════════════════════════════════╝\n");
  Serial.println("ESP32-P4 has no Bluetooth hardware.");
  Serial.println("BLE goes through ESP32-C6 co-processor via SDIO.\n");

  // Display + touch
  init_hardware();
  build_ui();
  lv_timer_handler();

  // Try BLE init
  ble_ready = init_ble();

  if (ble_ready) {
    Serial.println("\n[BLE] *** BLE IS WORKING! ***");
    Serial.println("[BLE] Press 's' to scan, or tap Scan button.\n");

    // Auto-scan on startup
    do_scan();
  } else {
    Serial.println("\n[BLE] *** BLE FAILED ***");
    Serial.println("[BLE] Not supported on this Arduino core version.\n");
  }

  update_ui();
  lv_timer_handler();
}

void loop() {
  // Touch
  static uint32_t lt = 0;
  if (millis() - lt > 30) {
    lt = millis();
    if (g_touch) {
      esp_panel::drivers::TouchPoint tp[1];
      int pts = g_touch->readPoints(tp, 1);
      if (pts > 0) {
        touch_pressed = true;
        touch_x = tp[0].x;
        touch_y = tp[0].y;
      } else {
        touch_pressed = false;
      }
    }
  }

  // Serial
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 's') do_scan();
  }

  // UI
  static uint32_t lui = 0;
  if (millis() - lui > 300) {
    lui = millis();
    update_ui();
  }

  lv_timer_handler();
  delay(10);
}
