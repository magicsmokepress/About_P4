/**
 * CrowPanel Advanced 7" ESP32-P4 — SD Card File Browser
 *
 * Confirmed SD config:
 *   SD_MMC 1-bit mode: CLK=43, CMD=44, D0=39
 *   LDO4 handled internally by SD_MMC library
 *   MUST init SD before display (LDO4 kills DSI if done after)
 *
 * Touch navigation: tap files to select, tap again to open
 * Touch zones: [BACK] button top-left, [UP][DOWN] arrows right side
 *
 * BOARD SETTINGS: Default partition, 16MB flash, OPI PSRAM
 */

#include <esp_display_panel.hpp>
#include <esp_lcd_panel_ops.h>
#include <lvgl.h>
#include <SD_MMC.h>
#include <FS.h>

using namespace esp_panel::drivers;

// ═══════════════════════════════════════════════════════════
//  Display + Touch config
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

// ─── Globals ─────────────────────────────────────────────
static LCD_EK79007 *g_lcd = NULL;
static TouchGT911 *g_touch = NULL;
static lv_display_t *lvgl_disp = NULL;
static lv_indev_t *lvgl_touch_dev = NULL;
static volatile bool touch_pressed = false;
static volatile int16_t touch_x = 0, touch_y = 0;

// UI labels (directly on screen — no containers)
static lv_obj_t *title_lbl = NULL;
static lv_obj_t *path_lbl = NULL;
static lv_obj_t *list_lbl = NULL;
static lv_obj_t *preview_lbl = NULL;
static lv_obj_t *status_lbl = NULL;
// Touch buttons
static lv_obj_t *btn_back = NULL;
static lv_obj_t *btn_up = NULL;
static lv_obj_t *btn_down = NULL;
static lv_obj_t *btn_open = NULL;

// State
static char cur_path[256] = "/";
static bool sd_ready = false;

#define MAX_FILES 80
struct FileEntry {
  char name[100];
  bool isDir;
  size_t size;
};
static FileEntry files[MAX_FILES];
static int file_count = 0;
static int selected_idx = -1;
static int scroll_offset = 0;
#define VISIBLE_LINES 20

// Line height for touch hit detection
#define LIST_TOP_Y 68
#define LINE_HEIGHT 24  // approx height of montserrat_16 line

// Forward declarations
static void nav_up();
static void nav_down();
static void nav_enter();
static void nav_back();
static void refresh_list();

// ═══════════════════════════════════════════════════════════
//  Display + Touch init
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
//  Helpers
// ═══════════════════════════════════════════════════════════

static void fmt_size(char *out, size_t sz, size_t bytes) {
  if (bytes >= 1024UL * 1024 * 1024)
    snprintf(out, sz, "%.1fGB", bytes / (1024.0 * 1024.0 * 1024.0));
  else if (bytes >= 1024 * 1024)
    snprintf(out, sz, "%.1fMB", bytes / (1024.0 * 1024.0));
  else if (bytes >= 1024)
    snprintf(out, sz, "%.1fKB", bytes / 1024.0);
  else
    snprintf(out, sz, "%dB", (int)bytes);
}

// ═══════════════════════════════════════════════════════════
//  File Operations
// ═══════════════════════════════════════════════════════════

static void load_directory(const char *path) {
  file_count = 0;
  selected_idx = -1;
  scroll_offset = 0;
  strncpy(cur_path, path, sizeof(cur_path) - 1);

  File dir = SD_MMC.open(path);
  if (!dir || !dir.isDirectory()) {
    Serial.printf("Cannot open: %s\n", path);
    return;
  }

  if (strcmp(path, "/") != 0) {
    strncpy(files[file_count].name, "..", sizeof(files[0].name));
    files[file_count].isDir = true;
    files[file_count].size = 0;
    file_count++;
  }

  File entry;
  while ((entry = dir.openNextFile()) && file_count < MAX_FILES) {
    const char *n = entry.name();
    const char *sl = strrchr(n, '/');
    const char *fn = sl ? sl + 1 : n;
    strncpy(files[file_count].name, fn, sizeof(files[0].name) - 1);
    files[file_count].isDir = entry.isDirectory();
    files[file_count].size = entry.isDirectory() ? 0 : entry.size();
    file_count++;
  }
  Serial.printf("Loaded %d entries from %s\n", file_count, path);
}

static void preview_file(const char *path) {
  if (!preview_lbl) return;

  File f = SD_MMC.open(path, FILE_READ);
  if (!f) {
    lv_label_set_text(preview_lbl, "Cannot open file");
    return;
  }

  size_t fsize = f.size();
  char szstr[16];
  fmt_size(szstr, sizeof(szstr), fsize);

  const int MAXP = 1500;
  char *buf = (char *)malloc(MAXP + 100);
  if (!buf) { f.close(); return; }

  int pos = snprintf(buf, MAXP, "%s  (%s)\n", path, szstr);

  uint8_t samp[32];
  int slen = f.read(samp, sizeof(samp));
  f.seek(0);
  bool text = true;
  for (int i = 0; i < slen; i++) {
    if (samp[i] == 0 || (samp[i] < 0x20 && samp[i] != '\n' && samp[i] != '\r' && samp[i] != '\t')) {
      text = false; break;
    }
  }

  if (text) {
    int rd = f.read((uint8_t *)(buf + pos), MAXP - pos - 20);
    buf[pos + rd] = '\0';
    if ((int)fsize > MAXP - pos) strcat(buf, "\n...(truncated)");
  } else {
    pos += snprintf(buf + pos, MAXP - pos, "[Binary]\n");
    uint8_t hx[128];
    int hl = f.read(hx, sizeof(hx));
    for (int i = 0; i < hl && pos < MAXP - 10; i++) {
      pos += snprintf(buf + pos, MAXP - pos, "%02X ", hx[i]);
      if ((i + 1) % 16 == 0) buf[pos++] = '\n';
    }
    buf[pos] = '\0';
  }

  lv_label_set_text(preview_lbl, buf);
  free(buf);
  f.close();
  lv_timer_handler();
}

// ═══════════════════════════════════════════════════════════
//  LVGL Button Callbacks
// ═══════════════════════════════════════════════════════════

static void btn_back_cb(lv_event_t *e) { nav_back(); }
static void btn_up_cb(lv_event_t *e)   { nav_up(); }
static void btn_down_cb(lv_event_t *e) { nav_down(); }
static void btn_open_cb(lv_event_t *e) { nav_enter(); }

// ═══════════════════════════════════════════════════════════
//  UI
// ═══════════════════════════════════════════════════════════

static lv_obj_t* make_btn(lv_obj_t *parent, const char *text,
                           lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                           lv_event_cb_t cb) {
  lv_obj_t *btn = lv_button_create(parent);
  lv_obj_set_size(btn, w, h);
  lv_obj_set_pos(btn, x, y);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A2A4A), 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x4A4A7A), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn, 6, 0);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xDDDDDD), 0);
  lv_obj_center(lbl);

  return btn;
}

static void refresh_list() {
  if (!list_lbl) return;

  static char lbuf[3000];
  int pos = 0;
  lbuf[0] = '\0';

  int end = scroll_offset + VISIBLE_LINES;
  if (end > file_count) end = file_count;

  for (int i = scroll_offset; i < end && pos < 2800; i++) {
    bool sel = (i == selected_idx);
    char szstr[16] = "";
    if (!files[i].isDir && files[i].size > 0)
      fmt_size(szstr, sizeof(szstr), files[i].size);

    if (sel) {
      pos += snprintf(lbuf + pos, sizeof(lbuf) - pos,
        "> %s %s  %s\n",
        files[i].isDir ? "[DIR]" : "     ",
        files[i].name, szstr);
    } else {
      pos += snprintf(lbuf + pos, sizeof(lbuf) - pos,
        "  %s %s  %s\n",
        files[i].isDir ? "[DIR]" : "     ",
        files[i].name, szstr);
    }
  }

  lv_label_set_text(list_lbl, lbuf);

  char ptxt[300];
  snprintf(ptxt, sizeof(ptxt), "%s  (%d items)", cur_path, file_count);
  lv_label_set_text(path_lbl, ptxt);

  lv_timer_handler();
}

static void build_ui() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A0A1A), 0);

  // Title
  title_lbl = lv_label_create(scr);
  lv_label_set_text(title_lbl, LV_SYMBOL_SD_CARD "  SD Card Browser");
  lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title_lbl, lv_color_hex(0x64B5F6), 0);
  lv_obj_align(title_lbl, LV_ALIGN_TOP_LEFT, 20, 8);

  // Path
  path_lbl = lv_label_create(scr);
  lv_label_set_text(path_lbl, "/");
  lv_obj_set_style_text_font(path_lbl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(path_lbl, lv_color_hex(0xFFEB3B), 0);
  lv_obj_align(path_lbl, LV_ALIGN_TOP_LEFT, 20, 42);

  // File list (left side)
  list_lbl = lv_label_create(scr);
  lv_label_set_text(list_lbl, "Loading...");
  lv_label_set_long_mode(list_lbl, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(list_lbl, 500);
  lv_obj_set_style_text_font(list_lbl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(list_lbl, lv_color_hex(0xCCCCCC), 0);
  lv_obj_set_pos(list_lbl, 20, LIST_TOP_Y);

  // Preview (right side)
  preview_lbl = lv_label_create(scr);
  lv_label_set_text(preview_lbl, "Tap a file to select\nTap OPEN to preview\n\nTap [DIR] to enter folder");
  lv_label_set_long_mode(preview_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(preview_lbl, 380);
  lv_obj_set_style_text_font(preview_lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(preview_lbl, lv_color_hex(0x999999), 0);
  lv_obj_set_pos(preview_lbl, 540, LIST_TOP_Y);

  // Touch buttons (right edge)
  btn_back = make_btn(scr, LV_SYMBOL_LEFT " Back", 540, 8, 120, 40, btn_back_cb);
  btn_up   = make_btn(scr, LV_SYMBOL_UP,      930, 100, 80, 70, btn_up_cb);
  btn_down = make_btn(scr, LV_SYMBOL_DOWN,    930, 190, 80, 70, btn_down_cb);
  btn_open = make_btn(scr, "OPEN",             930, 300, 80, 70, btn_open_cb);

  // Status
  status_lbl = lv_label_create(scr);
  lv_label_set_text(status_lbl, "Tap file to select | Buttons: Back / Up / Down / Open");
  lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x555555), 0);
  lv_obj_align(status_lbl, LV_ALIGN_BOTTOM_MID, 0, -5);
}

// ═══════════════════════════════════════════════════════════
//  Navigation
// ═══════════════════════════════════════════════════════════

static void nav_up() {
  if (file_count == 0) return;
  if (selected_idx < 0) selected_idx = 0;
  else if (selected_idx > 0) selected_idx--;
  if (selected_idx < scroll_offset) scroll_offset = selected_idx;
  refresh_list();
}

static void nav_down() {
  if (file_count == 0) return;
  if (selected_idx < 0) selected_idx = 0;
  else if (selected_idx < file_count - 1) selected_idx++;
  if (selected_idx >= scroll_offset + VISIBLE_LINES)
    scroll_offset = selected_idx - VISIBLE_LINES + 1;
  refresh_list();
}

static void nav_enter() {
  if (selected_idx < 0 || selected_idx >= file_count) return;
  FileEntry &fe = files[selected_idx];

  if (fe.isDir) {
    if (strcmp(fe.name, "..") == 0) {
      char *sl = strrchr(cur_path, '/');
      if (sl && sl != cur_path) *sl = '\0';
      else strcpy(cur_path, "/");
    } else {
      char np[256];
      if (strcmp(cur_path, "/") == 0)
        snprintf(np, sizeof(np), "/%s", fe.name);
      else
        snprintf(np, sizeof(np), "%s/%s", cur_path, fe.name);
      strncpy(cur_path, np, sizeof(cur_path) - 1);
    }
    load_directory(cur_path);
    refresh_list();
    lv_label_set_text(preview_lbl, "");
  } else {
    char fp[256];
    if (strcmp(cur_path, "/") == 0)
      snprintf(fp, sizeof(fp), "/%s", fe.name);
    else
      snprintf(fp, sizeof(fp), "%s/%s", cur_path, fe.name);
    preview_file(fp);
  }
  lv_timer_handler();
}

static void nav_back() {
  if (strcmp(cur_path, "/") == 0) return;
  char *sl = strrchr(cur_path, '/');
  if (sl && sl != cur_path) *sl = '\0';
  else strcpy(cur_path, "/");
  load_directory(cur_path);
  refresh_list();
  lv_label_set_text(preview_lbl, "");
  lv_timer_handler();
}

// Touch on file list area — select that line
static void handle_list_touch(int16_t y) {
  if (y < LIST_TOP_Y || y > LIST_TOP_Y + VISIBLE_LINES * LINE_HEIGHT) return;
  int line = (y - LIST_TOP_Y) / LINE_HEIGHT;
  int idx = scroll_offset + line;
  if (idx >= 0 && idx < file_count) {
    if (idx == selected_idx) {
      // Double-tap: open it
      nav_enter();
    } else {
      selected_idx = idx;
      refresh_list();
    }
  }
}

// ═══════════════════════════════════════════════════════════
//  Setup + Loop
// ═══════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n========================================");
  Serial.println("CrowPanel ESP32-P4 — SD Card Browser");
  Serial.println("========================================");

  // Init SD FIRST — before display, so LDO4 init doesn't kill DSI
  Serial.println("Init SD card first...");
  SD_MMC.setPins(43, 44, 39);
  if (SD_MMC.begin("/sdcard", true)) {
    sd_ready = true;
    Serial.printf("SD: SDHC %llu MB\n", SD_MMC.cardSize() / (1024*1024));
  } else {
    Serial.println("SD FAILED!");
  }
  delay(100);

  // Now init display + touch
  init_hardware();
  build_ui();
  lv_timer_handler();
  delay(50);
  lv_timer_handler();
  Serial.println("Display OK");

  if (sd_ready) {
    load_directory("/");
    refresh_list();
  } else {
    lv_label_set_text(list_lbl, "SD card not found!\nInsert card and reset.");
  }

  lv_timer_handler();
  Serial.println("Touch: tap file=select, tap again=open");
  Serial.println("Buttons: Back / Up / Down / Open");
  Serial.println("Serial: u/d/enter/b/r/p/q also work");
}

static uint32_t touch_release_time = 0;
static bool was_pressed = false;
static int16_t press_x = 0, press_y = 0;

void loop() {
  // Poll touch
  if (g_touch) {
    esp_lcd_touch_handle_t tp = g_touch->getHandle();
    if (tp) {
      uint16_t x[1], y[1], strength[1];
      uint8_t cnt = 0;
      esp_lcd_touch_read_data(tp);
      if (esp_lcd_touch_get_coordinates(tp, x, y, strength, &cnt, 1) && cnt > 0) {
        if (!touch_pressed || (millis() - touch_release_time) > 150) {
          touch_pressed = true;
          touch_x = x[0];
          touch_y = y[0];
          if (!was_pressed) {
            press_x = x[0];
            press_y = y[0];
          }
          was_pressed = true;
        }
      } else {
        if (was_pressed) {
          // Touch released — check if it was on the file list area
          // (only if not handled by LVGL buttons)
          if (press_x < 520 && press_y >= LIST_TOP_Y) {
            handle_list_touch(press_y);
          }
          touch_release_time = millis();
          was_pressed = false;
        }
        touch_pressed = false;
      }
    }
  }

  // Serial navigation (also works)
  if (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 'u': case 'U': case 'k': nav_up(); break;
      case 'd': case 'D': case 'j': nav_down(); break;
      case '\r': case '\n': case 'o': nav_enter(); break;
      case 'b': case 'B': case 'h': nav_back(); break;
      case 'r': case 'R':
        load_directory(cur_path);
        refresh_list();
        lv_timer_handler();
        break;
      case 'p': case 'P':
        for (int i = 0; i < VISIBLE_LINES; i++) nav_down();
        break;
      case 'q': case 'Q':
        for (int i = 0; i < VISIBLE_LINES; i++) nav_up();
        break;
    }
  }

  lv_timer_handler();
  delay(10);
}
