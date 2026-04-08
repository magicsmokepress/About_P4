/**
 * CrowPanel Advanced 7" ESP32-P4 — USB Host Test
 *
 * Tests USB 2.0 Host mode on ESP32-P4.
 * Detects USB devices plugged in, shows device info (VID/PID/class),
 * and handles HID keyboard input as a demo.
 *
 * USB 2.0 OTG pins: GPIO 49 (D-), GPIO 50 (D+)
 * Connect a USB device (keyboard, mouse, flash drive) to the
 * USB Type-C port labeled "USB" (not the UART port).
 *
 * BOARD SETTINGS:
 *   Board: CrowPanel Advanced 7"  (or ESP32-P4 Dev)
 *   USB Mode: "USB OTG (TinyUSB)" in Tools menu
 *   Partition: Default, 16MB flash, OPI PSRAM
 */

#include <esp_display_panel.hpp>
#include <esp_lcd_panel_ops.h>
#include <lvgl.h>

// USB Host includes (ESP-IDF)
#include "usb/usb_host.h"

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
//  Globals
// ═══════════════════════════════════════════════════════════
static LCD_EK79007 *g_lcd = NULL;
static TouchGT911 *g_touch = NULL;
static lv_display_t *lvgl_disp = NULL;
static lv_indev_t *lvgl_touch_dev = NULL;
static volatile bool touch_pressed = false;
static volatile int16_t touch_x = 0, touch_y = 0;

// USB state
static bool usb_host_ready = false;
static bool device_connected = false;
static int devices_seen = 0;
static char device_info[512] = "No device";
static char last_input[256] = "";

// UI
static lv_obj_t *status_lbl = NULL;
static lv_obj_t *devinfo_lbl = NULL;
static lv_obj_t *input_lbl = NULL;
static lv_obj_t *log_lbl = NULL;
static char log_text[2048] = "";
static int log_len = 0;

// ═══════════════════════════════════════════════════════════
//  Logging
// ═══════════════════════════════════════════════════════════
static void logmsg(const char *fmt, ...) {
  char buf[200];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.print(buf);

  int slen = strlen(buf);
  if (log_len + slen >= (int)sizeof(log_text) - 10) {
    int half = log_len / 2;
    memmove(log_text, log_text + half, log_len - half + 1);
    log_len -= half;
  }
  memcpy(log_text + log_len, buf, slen);
  log_len += slen;
  log_text[log_len] = 0;
  if (log_lbl) lv_label_set_text(log_lbl, log_text);
}

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
    data->point.x = touch_x; data->point.y = touch_y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else { data->state = LV_INDEV_STATE_RELEASED; }
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
//  USB Host — Client event handler
// ═══════════════════════════════════════════════════════════

static usb_host_client_handle_t client_hdl = NULL;
static usb_device_handle_t dev_hdl = NULL;

static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg) {
  switch (event_msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV: {
      uint8_t addr = event_msg->new_dev.address;
      devices_seen++;
      logmsg("[USB] New device at addr %d\n", addr);

      // Open the device
      esp_err_t err = usb_host_device_open(client_hdl, addr, &dev_hdl);
      if (err != ESP_OK) {
        logmsg("[USB] Failed to open device: %s\n", esp_err_to_name(err));
        break;
      }

      // Get device descriptor
      const usb_device_desc_t *desc;
      err = usb_host_get_device_descriptor(dev_hdl, &desc);
      if (err == ESP_OK) {
        snprintf(device_info, sizeof(device_info),
                 "VID: 0x%04X  PID: 0x%04X\n"
                 "Class: %d  SubClass: %d  Protocol: %d\n"
                 "USB ver: %d.%d  #Configs: %d",
                 desc->idVendor, desc->idProduct,
                 desc->bDeviceClass, desc->bDeviceSubClass, desc->bDeviceProtocol,
                 (desc->bcdUSB >> 8), (desc->bcdUSB & 0xFF) >> 4,
                 desc->bNumConfigurations);
        logmsg("[USB] VID=0x%04X PID=0x%04X Class=%d\n",
               desc->idVendor, desc->idProduct, desc->bDeviceClass);

        // Get string descriptors if available
        const usb_config_desc_t *config_desc;
        err = usb_host_get_active_config_descriptor(dev_hdl, &config_desc);
        if (err == ESP_OK) {
          logmsg("[USB] Config: %d interfaces, %dmA max power\n",
                 config_desc->bNumInterfaces, config_desc->bMaxPower * 2);
        }
      }

      device_connected = true;
      break;
    }
    case USB_HOST_CLIENT_EVENT_DEV_GONE: {
      logmsg("[USB] Device disconnected\n");
      if (dev_hdl) {
        usb_host_device_close(client_hdl, dev_hdl);
        dev_hdl = NULL;
      }
      device_connected = false;
      snprintf(device_info, sizeof(device_info), "Device disconnected");
      break;
    }
    default:
      logmsg("[USB] Unknown event: %d\n", event_msg->event);
      break;
  }
}

// ═══════════════════════════════════════════════════════════
//  USB Host task — runs on separate core
// ═══════════════════════════════════════════════════════════

static void usb_host_task(void *arg) {
  logmsg("[USB] Host task started on core %d\n", xPortGetCoreID());

  // Install USB Host Library
  usb_host_config_t host_config = {
    .skip_phy_setup = false,
    .intr_flags = ESP_INTR_FLAG_LEVEL1,
  };

  esp_err_t err = usb_host_install(&host_config);
  if (err != ESP_OK) {
    logmsg("[USB] usb_host_install FAILED: %s\n", esp_err_to_name(err));
    vTaskDelete(NULL);
    return;
  }
  logmsg("[USB] Host library installed OK\n");

  // Register client
  usb_host_client_config_t client_config = {
    .is_synchronous = false,
    .max_num_event_msg = 5,
    .async = {
      .client_event_callback = client_event_cb,
      .callback_arg = NULL,
    },
  };

  err = usb_host_client_register(&client_config, &client_hdl);
  if (err != ESP_OK) {
    logmsg("[USB] Client register FAILED: %s\n", esp_err_to_name(err));
    usb_host_uninstall();
    vTaskDelete(NULL);
    return;
  }
  logmsg("[USB] Client registered OK\n");
  usb_host_ready = true;

  // Main event loop
  while (true) {
    // Process USB host library events
    uint32_t event_flags;
    usb_host_lib_handle_events(0, &event_flags);

    // Process client events
    usb_host_client_handle_events(client_hdl, 0);

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ═══════════════════════════════════════════════════════════
//  USB Init
// ═══════════════════════════════════════════════════════════

static bool init_usb() {
  logmsg("[USB] Starting USB Host...\n");
  logmsg("[USB] Pins: D- = GPIO49, D+ = GPIO50\n");

  // Create USB host task on core 1 (keep display on core 0)
  BaseType_t ret = xTaskCreatePinnedToCore(
    usb_host_task,
    "usb_host",
    4096,
    NULL,
    5,
    NULL,
    1  // Core 1
  );

  if (ret != pdPASS) {
    logmsg("[USB] Failed to create USB host task\n");
    return false;
  }

  logmsg("[USB] Host task created, waiting for init...\n");

  // Wait a bit for the task to initialize
  for (int i = 0; i < 50; i++) {
    if (usb_host_ready) break;
    delay(100);
  }

  return usb_host_ready;
}

// ═══════════════════════════════════════════════════════════
//  UI
// ═══════════════════════════════════════════════════════════

static void build_ui() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A0A1A), 0);

  lv_obj_t *title = lv_label_create(scr);
  lv_label_set_text(title, LV_SYMBOL_USB " USB 2.0 Host Test");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x44AACC), 0);
  lv_obj_set_pos(title, 20, 15);

  status_lbl = lv_label_create(scr);
  lv_label_set_text(status_lbl, "Initializing USB Host...");
  lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_22, 0);
  lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xFFAA00), 0);
  lv_obj_set_pos(status_lbl, 20, 55);

  devinfo_lbl = lv_label_create(scr);
  lv_label_set_text(devinfo_lbl, "Plug in a USB device...");
  lv_obj_set_style_text_font(devinfo_lbl, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(devinfo_lbl, lv_color_hex(0xCCCCCC), 0);
  lv_obj_set_pos(devinfo_lbl, 20, 90);
  lv_obj_set_width(devinfo_lbl, 980);
  lv_label_set_long_mode(devinfo_lbl, LV_LABEL_LONG_WRAP);

  // Separator
  lv_obj_t *line = lv_obj_create(scr);
  lv_obj_set_size(line, 984, 2);
  lv_obj_set_pos(line, 20, 175);
  lv_obj_set_style_bg_color(line, lv_color_hex(0x333355), 0);
  lv_obj_set_style_border_width(line, 0, 0);
  lv_obj_set_style_radius(line, 0, 0);

  log_lbl = lv_label_create(scr);
  lv_label_set_text(log_lbl, "");
  lv_obj_set_style_text_font(log_lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(log_lbl, lv_color_hex(0xAABBCC), 0);
  lv_obj_set_pos(log_lbl, 20, 185);
  lv_obj_set_width(log_lbl, 980);
  lv_label_set_long_mode(log_lbl, LV_LABEL_LONG_WRAP);
}

static void update_ui() {
  if (!usb_host_ready) {
    lv_label_set_text(status_lbl, "USB Host FAILED to initialize");
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xFF4444), 0);
  } else if (device_connected) {
    char buf[64];
    snprintf(buf, sizeof(buf), "USB Device Connected (total seen: %d)", devices_seen);
    lv_label_set_text(status_lbl, buf);
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x00FF88), 0);
  } else {
    lv_label_set_text(status_lbl, "USB Host Ready — waiting for device...");
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xFFAA00), 0);
  }

  lv_label_set_text(devinfo_lbl, device_info);
}

// ═══════════════════════════════════════════════════════════
//  Setup & Loop
// ═══════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n╔══════════════════════════════════════╗");
  Serial.println("║  CrowPanel P4 — USB 2.0 Host Test     ║");
  Serial.println("╚══════════════════════════════════════╝\n");

  init_hardware();
  build_ui();
  lv_timer_handler();

  bool ok = init_usb();

  if (ok) {
    Serial.println("\n[USB] *** USB HOST IS READY ***");
    Serial.println("[USB] Plug in a USB device (keyboard, mouse, flash drive).");
    Serial.println("[USB] Device info will appear on screen + serial.\n");
    logmsg("USB Host ready! Plug in a device...\n");
  } else {
    Serial.println("\n[USB] *** USB HOST INIT FAILED ***\n");
    logmsg("USB Host init failed.\n");
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
      touch_pressed = (pts > 0);
      if (touch_pressed) { touch_x = tp[0].x; touch_y = tp[0].y; }
    }
  }

  // UI update
  static uint32_t lui = 0;
  if (millis() - lui > 500) {
    lui = millis();
    update_ui();
  }

  lv_timer_handler();
  delay(10);
}
