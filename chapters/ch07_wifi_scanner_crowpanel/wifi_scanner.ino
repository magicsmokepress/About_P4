#include <esp_display_panel.hpp>
#include <esp_lcd_panel_ops.h>
#include <lvgl.h>
#include <WiFi.h>

using namespace esp_panel::drivers;

// ─── Display (proven config) ──────────────────────────────────────
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
static lv_obj_t *status_bar = NULL;
static lv_obj_t *net_list = NULL;
static lv_obj_t *scan_btn = NULL;
static lv_obj_t *kb = NULL;
static lv_obj_t *pwd_ta = NULL;
static lv_obj_t *pwd_panel = NULL;

static String selected_ssid = "";

// ─── Display + Touch init (reusable boilerplate) ───────────────────
static void lvgl_flush_cb(lv_display_t *disp,
                          const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_handle_t panel = g_lcd->getHandle();
    if (panel) {
        esp_err_t ret;
        do {
            ret = esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1,
                area->x2 + 1, area->y2 + 1, px_map);
            if (ret == ESP_ERR_INVALID_STATE) delay(1);
        } while (ret == ESP_ERR_INVALID_STATE);
    }
    lv_display_flush_ready(disp);
}

static void lvgl_touch_cb(lv_indev_t *indev,
                          lv_indev_data_t *data) {
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

    g_lcd = new LCD_EK79007(bus, LCD_WIDTH, LCD_HEIGHT,
                            LCD_COLOR_BITS, LCD_RST_IO);
    assert(g_lcd->begin());

    BacklightPWM_LEDC *bl =
        new BacklightPWM_LEDC(LCD_BL_IO, LCD_BL_ON_LEVEL);
    assert(bl->begin());
    assert(bl->on());

    BusI2C *touch_bus = new BusI2C(
        TOUCH_I2C_SCL, TOUCH_I2C_SDA,
        (BusI2C::ControlPanelFullConfig)
            ESP_PANEL_TOUCH_I2C_CONTROL_PANEL_CONFIG(GT911));
    touch_bus->configI2C_FreqHz(TOUCH_I2C_FREQ);
    touch_bus->configI2C_PullupEnable(true, true);
    g_touch = new TouchGT911(touch_bus, LCD_WIDTH, LCD_HEIGHT,
                             TOUCH_RST_IO, TOUCH_INT_IO);
    g_touch->begin();

    lv_init();
    lv_tick_set_cb((lv_tick_get_cb_t)millis);

    size_t buf_size = LCD_WIDTH * LVGL_BUF_LINES * sizeof(lv_color_t);
    uint8_t *buf1 = (uint8_t *)heap_caps_malloc(buf_size,
                                                 MALLOC_CAP_SPIRAM);
    assert(buf1);

    lvgl_disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
    lv_display_set_flush_cb(lvgl_disp, lvgl_flush_cb);
    lv_display_set_buffers(lvgl_disp, buf1, NULL, buf_size,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    lvgl_touch_dev = lv_indev_create();
    lv_indev_set_type(lvgl_touch_dev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lvgl_touch_dev, lvgl_touch_cb);
    lv_indev_set_display(lvgl_touch_dev, lvgl_disp);
}

// ─── Wi-Fi helpers ──────────────────────────────────────────────────
static void update_status(const char *msg) {
    if (status_bar) lv_label_set_text(status_bar, msg);
    Serial.println(msg);
}

static lv_color_t rssi_color(int rssi) {
    if (rssi > -60) return lv_color_hex(0x00E676);  // strong
    if (rssi > -80) return lv_color_hex(0xFFEB3B);  // medium
    return lv_color_hex(0xFF5252);                    // weak
}

static void do_wifi_scan() {
    update_status("Scanning (SDIO -> C6)...");
    lv_obj_clean(net_list);
    lv_timer_handler();

    int n = WiFi.scanNetworks();

    // Retry once - SDIO link may need time
    if (n < 0) {
        update_status("SDIO retry...");
        lv_timer_handler();
        delay(2000);
        n = WiFi.scanNetworks();
    }

    if (n <= 0) {
        char err[80];
        snprintf(err, sizeof(err),
            "Scan failed (code %d) - SDIO link may be unstable", n);
        update_status(err);
        return;
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "Found %d networks", n);
    update_status(buf);

    for (int i = 0; i < n; i++) {
        snprintf(buf, sizeof(buf), "%s  %s  (%d dBm)%s",
            LV_SYMBOL_WIFI,
            WiFi.SSID(i).c_str(),
            WiFi.RSSI(i),
            WiFi.encryptionType(i) == WIFI_AUTH_OPEN
                ? "" : "  " LV_SYMBOL_EYE_CLOSE);

        lv_obj_t *btn = lv_button_create(net_list);
        lv_obj_set_width(btn, lv_pct(100));
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x16213E), 0);
        lv_obj_set_style_bg_color(btn,
            lv_color_hex(0x2A3A5E), LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_pad_all(btn, 8, 0);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, rssi_color(WiFi.RSSI(i)), 0);

        lv_obj_add_event_cb(btn, [](lv_event_t *e) {
            lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
            lv_obj_t *label = lv_obj_get_child(btn, 0);
            const char *txt = lv_label_get_text(label);

            String full = String(txt);
            int start = full.indexOf("  ") + 2;
            int end = full.indexOf("  ", start);
            if (end < 0) end = full.length();
            selected_ssid = full.substring(start, end);
            selected_ssid.trim();

            if (pwd_panel)
                lv_obj_remove_flag(pwd_panel, LV_OBJ_FLAG_HIDDEN);
            if (pwd_ta) {
                lv_textarea_set_text(pwd_ta, "");
                lv_textarea_set_placeholder_text(pwd_ta,
                    selected_ssid.c_str());
            }
        }, LV_EVENT_CLICKED, NULL);
    }

    WiFi.scanDelete();
}

// ─── Password keyboard callbacks ──────────────────────────────────
static void kb_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        const char *pwd = lv_textarea_get_text(pwd_ta);
        char buf[128];
        snprintf(buf, sizeof(buf), "Connecting to %s...",
                 selected_ssid.c_str());
        update_status(buf);
        lv_obj_add_flag(pwd_panel, LV_OBJ_FLAG_HIDDEN);
        WiFi.begin(selected_ssid.c_str(), pwd);
    } else if (code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(pwd_panel, LV_OBJ_FLAG_HIDDEN);
        update_status("Connection cancelled");
    }
}

// ─── Build UI ─────────────────────────────────────────────────────
static void build_ui() {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1A1A2E), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, LV_SYMBOL_WIFI "  CrowPanel Wi-Fi");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 20, 12);

    scan_btn = lv_button_create(scr);
    lv_obj_set_size(scan_btn, 140, 44);
    lv_obj_align(scan_btn, LV_ALIGN_TOP_RIGHT, -20, 8);
    lv_obj_set_style_bg_color(scan_btn, lv_color_hex(0x2196F3), 0);
    lv_obj_set_style_radius(scan_btn, 10, 0);
    lv_obj_add_event_cb(scan_btn, [](lv_event_t *e) {
        do_wifi_scan();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *scan_lbl = lv_label_create(scan_btn);
    lv_label_set_text(scan_lbl, LV_SYMBOL_REFRESH " Scan");
    lv_obj_set_style_text_font(scan_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(scan_lbl);

    status_bar = lv_label_create(scr);
    lv_label_set_text(status_bar, "Tap Scan to find networks");
    lv_obj_set_style_text_font(status_bar, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status_bar,
        lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(status_bar, LV_ALIGN_TOP_LEFT, 20, 56);

    // Scrollable network list
    net_list = lv_obj_create(scr);
    lv_obj_set_size(net_list, LCD_WIDTH - 40, 400);
    lv_obj_align(net_list, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_set_style_bg_color(net_list, lv_color_hex(0x0F0F23), 0);
    lv_obj_set_style_border_color(net_list,
        lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(net_list, 1, 0);
    lv_obj_set_style_radius(net_list, 10, 0);
    lv_obj_set_style_pad_all(net_list, 6, 0);
    lv_obj_set_style_pad_gap(net_list, 4, 0);
    lv_obj_set_flex_flow(net_list, LV_FLEX_FLOW_COLUMN);

    // IP display at bottom
    lv_obj_t *ip_lbl = lv_label_create(scr);
    lv_label_set_text(ip_lbl, "");
    lv_obj_set_style_text_font(ip_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ip_lbl, lv_color_hex(0x00E676), 0);
    lv_obj_align(ip_lbl, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_set_user_data(scr, ip_lbl);

    // Password entry panel (hidden initially)
    pwd_panel = lv_obj_create(scr);
    lv_obj_set_size(pwd_panel, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_style_bg_color(pwd_panel, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_bg_opa(pwd_panel, LV_OPA_90, 0);
    lv_obj_align(pwd_panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(pwd_panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *pwd_title = lv_label_create(pwd_panel);
    lv_label_set_text(pwd_title, "Enter Wi-Fi Password");
    lv_obj_set_style_text_font(pwd_title,
        &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(pwd_title,
        lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(pwd_title, LV_ALIGN_TOP_MID, 0, 10);

    pwd_ta = lv_textarea_create(pwd_panel);
    lv_obj_set_size(pwd_ta, 500, 50);
    lv_obj_align(pwd_ta, LV_ALIGN_TOP_MID, 0, 45);
    lv_textarea_set_password_mode(pwd_ta, true);
    lv_textarea_set_one_line(pwd_ta, true);
    lv_textarea_set_placeholder_text(pwd_ta, "Password");
    lv_obj_set_style_text_font(pwd_ta, &lv_font_montserrat_16, 0);

    kb = lv_keyboard_create(pwd_panel);
    lv_obj_set_size(kb, LCD_WIDTH - 40, 340);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_keyboard_set_textarea(kb, pwd_ta);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_CANCEL, NULL);
}

// ─── Setup ────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("CrowPanel ESP32-P4 Wi-Fi Scanner");

    init_hardware();
    build_ui();

    // SDIO link needs time to stabilise
    update_status("Initialising Wi-Fi (SDIO link)...");
    lv_timer_handler();

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(3000);  // critical - C6 needs time to boot

    update_status("Wi-Fi ready - tap Scan");
}

// ─── Loop ─────────────────────────────────────────────────────────
static bool was_connected = false;
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
            if (esp_lcd_touch_get_coordinates(tp, x, y, strength,
                    &cnt, 1) && cnt > 0) {
                if (touch_pressed ||
                    (millis() - touch_release_time) > TOUCH_DEBOUNCE_MS)
                {
                    touch_pressed = true;
                    touch_x = x[0];
                    touch_y = y[0];
                }
            } else {
                if (touch_pressed)
                    touch_release_time = millis();
                touch_pressed = false;
            }
        }
    }

    // Monitor connection status
    if (WiFi.status() == WL_CONNECTED && !was_connected) {
        was_connected = true;
        char buf[128];
        snprintf(buf, sizeof(buf),
            "Connected! IP: %s  RSSI: %d dBm",
            WiFi.localIP().toString().c_str(), WiFi.RSSI());
        update_status(buf);

        lv_obj_t *scr = lv_screen_active();
        lv_obj_t *ip_lbl = (lv_obj_t *)lv_obj_get_user_data(scr);
        if (ip_lbl) {
            lv_label_set_text_fmt(ip_lbl,
                LV_SYMBOL_OK "  %s  |  %d dBm",
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
        }
    } else if (WiFi.status() != WL_CONNECTED && was_connected) {
        was_connected = false;
        update_status("Disconnected");
    }

    lv_timer_handler();
    delay(10);
}
