/**
 * Chapter 24 - Deep Sleep with Timer Wake-Up
 * CrowPanel Advanced 7" ESP32-P4
 *
 * Demonstrates deep sleep with automatic timer wake-up.
 * On each wake cycle the display shows:
 *   - Boot count (persisted in RTC memory)
 *   - Wake-up reason
 *   - Countdown to next sleep
 *   - Heap / PSRAM statistics
 *
 * The board sleeps for 10 seconds, wakes, initializes the display,
 * shows the dashboard for a few seconds, then goes back to sleep.
 *
 * REQUIRED LIBRARIES:
 *   1. ESP32_Display_Panel  by esp-arduino-libs (v1.0.4+)
 *   2. lvgl                 (v9.x)
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
#include "esp_sleep.h"

using namespace esp_panel::drivers;

// ─── Deep-Sleep Configuration ─────────────────────────────────────
#define SLEEP_DURATION_SEC   10          // seconds of deep sleep
#define AWAKE_DISPLAY_SEC     8          // seconds to stay awake showing info
#define COUNTDOWN_TICK_MS  1000          // update countdown every second

// ─── RTC-Persistent Data ──────────────────────────────────────────
RTC_DATA_ATTR int      boot_count = 0;
RTC_DATA_ATTR uint64_t total_awake_ms = 0;  // cumulative time awake

// ─── Display Timings (CrowPanel 7" / EK79007 / MIPI-DSI) ─────────
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

static LCD_EK79007 *g_lcd = NULL;
static TouchGT911 *g_touch = NULL;
static lv_display_t *lvgl_disp = NULL;
static lv_indev_t *lvgl_touch_dev = NULL;
static volatile bool touch_pressed = false;
static volatile int16_t touch_x = 0, touch_y = 0;

// ─── UI Label Handles ─────────────────────────────────────────────
static lv_obj_t *lbl_boot      = NULL;
static lv_obj_t *lbl_wake      = NULL;
static lv_obj_t *lbl_countdown = NULL;
static lv_obj_t *lbl_heap      = NULL;
static lv_obj_t *lbl_psram     = NULL;
static lv_obj_t *lbl_total     = NULL;

static int       countdown_sec = AWAKE_DISPLAY_SEC;
static uint32_t  awake_start   = 0;

// ─── LVGL Flush Callback (from confirmed working baseline) ───────
static void lvgl_flush_cb(lv_display_t *disp,
    const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_handle_t panel = g_lcd->getHandle();
    if (panel) {
        esp_err_t ret;
        do {
            ret = esp_lcd_panel_draw_bitmap(panel,
                area->x1, area->y1,
                area->x2 + 1, area->y2 + 1, px_map);
            if (ret == ESP_ERR_INVALID_STATE) delay(1);
        } while (ret == ESP_ERR_INVALID_STATE);
    }
    lv_display_flush_ready(disp);
}

// ─── LVGL Touch Callback (from confirmed working baseline) ───────
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

// ─── Wake-Reason String ──────────────────────────────────────────
static const char *get_wake_reason()
{
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    switch (cause) {
        case ESP_SLEEP_WAKEUP_TIMER:     return "Timer";
        case ESP_SLEEP_WAKEUP_EXT0:      return "External (EXT0)";
        case ESP_SLEEP_WAKEUP_EXT1:      return "External (EXT1)";
        case ESP_SLEEP_WAKEUP_GPIO:      return "GPIO";
        case ESP_SLEEP_WAKEUP_TOUCHPAD:  return "Touch pad";
        case ESP_SLEEP_WAKEUP_ULP:       return "ULP / LP core";
        default:                         return "Power-on / Reset";
    }
}

// ─── Enter Deep Sleep ─────────────────────────────────────────────
static void enter_deep_sleep()
{
    // Accumulate awake time before sleeping
    total_awake_ms += (millis() - awake_start);

    Serial.printf("Entering deep sleep for %d seconds...\n", SLEEP_DURATION_SEC);
    Serial.flush();

    esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_DURATION_SEC * 1000000ULL);
    esp_deep_sleep_start();
    // Execution never reaches here - board resets on wake
}

// ─── Build the Dashboard UI ──────────────────────────────────────
static void build_ui()
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1A1A2E), 0);

    // ── Title bar ────────────────────────────────────────────────
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Ch24 \xE2\x80\x94 Deep Sleep Demo");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xE94560), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // ── Info panel (rounded rectangle) ───────────────────────────
    lv_obj_t *panel = lv_obj_create(scr);
    lv_obj_set_size(panel, 700, 420);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x16213E), 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x0F3460), 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_pad_all(panel, 30, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(panel, 14, 0);

    // Helper: creates a label inside the panel
    #define MAKE_LABEL(handle)                                           \
        (handle) = lv_label_create(panel);                               \
        lv_obj_set_style_text_font((handle), &lv_font_montserrat_24, 0);\
        lv_obj_set_style_text_color((handle), lv_color_hex(0xEEEEEE), 0);

    MAKE_LABEL(lbl_boot);
    MAKE_LABEL(lbl_wake);
    MAKE_LABEL(lbl_heap);
    MAKE_LABEL(lbl_psram);
    MAKE_LABEL(lbl_total);
    MAKE_LABEL(lbl_countdown);

    #undef MAKE_LABEL

    // ── Populate static values ───────────────────────────────────
    char buf[128];

    snprintf(buf, sizeof(buf), "Boot count:   %d", boot_count);
    lv_label_set_text(lbl_boot, buf);

    snprintf(buf, sizeof(buf), "Wake reason:  %s", get_wake_reason());
    lv_label_set_text(lbl_wake, buf);

    snprintf(buf, sizeof(buf), "Heap free:    %u KB  (min %u KB)",
             (unsigned)(esp_get_free_heap_size() / 1024),
             (unsigned)(esp_get_minimum_free_heap_size() / 1024));
    lv_label_set_text(lbl_heap, buf);

    size_t psram_free  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    snprintf(buf, sizeof(buf), "PSRAM free:   %u / %u KB",
             (unsigned)(psram_free / 1024),
             (unsigned)(psram_total / 1024));
    lv_label_set_text(lbl_psram, buf);

    snprintf(buf, sizeof(buf), "Total awake:  %llu ms across %d boots",
             (unsigned long long)total_awake_ms, boot_count);
    lv_label_set_text(lbl_total, buf);

    snprintf(buf, sizeof(buf), "Sleeping in:  %d seconds...", countdown_sec);
    lv_label_set_text(lbl_countdown, buf);
    lv_obj_set_style_text_color(lbl_countdown, lv_color_hex(0x53DFB2), 0);
}

// ═════════════════════════════════════════════════════════════════
void setup()
{
    awake_start = millis();

    Serial.begin(115200);
    delay(500);

    boot_count++;
    Serial.printf("\n=== Boot #%d | Wake reason: %s ===\n",
                  boot_count, get_wake_reason());

    // DSI bus
    BusDSI *bus = new BusDSI(
        LCD_DSI_LANE_NUM, LCD_DSI_LANE_RATE,
        LCD_DPI_CLK_MHZ, LCD_COLOR_BITS,
        LCD_WIDTH, LCD_HEIGHT,
        LCD_DPI_HPW, LCD_DPI_HBP, LCD_DPI_HFP,
        LCD_DPI_VPW, LCD_DPI_VBP, LCD_DPI_VFP,
        LCD_DSI_PHY_LDO_ID);
    bus->configDpiFrameBufferNumber(1);
    assert(bus->begin());

    // LCD
    g_lcd = new LCD_EK79007(bus, LCD_WIDTH, LCD_HEIGHT,
                            LCD_COLOR_BITS, LCD_RST_IO);
    assert(g_lcd->begin());

    // Backlight
    BacklightPWM_LEDC *bl =
        new BacklightPWM_LEDC(LCD_BL_IO, LCD_BL_ON_LEVEL);
    assert(bl->begin());
    assert(bl->on());

    // Touch
    BusI2C *touch_bus = new BusI2C(
        TOUCH_I2C_SCL, TOUCH_I2C_SDA,
        (BusI2C::ControlPanelFullConfig)
            ESP_PANEL_TOUCH_I2C_CONTROL_PANEL_CONFIG(GT911));
    touch_bus->configI2C_FreqHz(TOUCH_I2C_FREQ);
    touch_bus->configI2C_PullupEnable(true, true);
    g_touch = new TouchGT911(touch_bus, LCD_WIDTH, LCD_HEIGHT,
                             TOUCH_RST_IO, TOUCH_INT_IO);
    g_touch->begin();

    // LVGL
    lv_init();
    lv_tick_set_cb((lv_tick_get_cb_t)millis);
    size_t buf_size =
        LCD_WIDTH * LVGL_BUF_LINES * sizeof(lv_color_t);
    uint8_t *buf1 = (uint8_t *)heap_caps_malloc(
        buf_size, MALLOC_CAP_SPIRAM);
    assert(buf1);

    lvgl_disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
    lv_display_set_flush_cb(lvgl_disp, lvgl_flush_cb);
    lv_display_set_buffers(lvgl_disp, buf1, NULL, buf_size,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    lvgl_touch_dev = lv_indev_create();
    lv_indev_set_type(lvgl_touch_dev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lvgl_touch_dev, lvgl_touch_cb);
    lv_indev_set_display(lvgl_touch_dev, lvgl_disp);

    // ── Build UI ────────────────────────────────────────────────
    build_ui();

    Serial.println("Display ready - countdown to sleep started");
}

// ═════════════════════════════════════════════════════════════════
void loop()
{
    // Touch polling (exact pattern from confirmed working baseline)
    if (g_touch) {
        esp_lcd_touch_handle_t tp = g_touch->getHandle();
        if (tp) {
            uint16_t x[1], y[1], strength[1];
            uint8_t cnt = 0;
            esp_lcd_touch_read_data(tp);
            if (esp_lcd_touch_get_coordinates(tp,
                    x, y, strength, &cnt, 1) && cnt > 0) {
                touch_pressed = true;
                touch_x = x[0];
                touch_y = y[0];
            } else {
                touch_pressed = false;
            }
        }
    }

    lv_timer_handler();

    // Update countdown once per second
    static uint32_t last_tick = 0;
    uint32_t now = millis();
    if (now - last_tick >= COUNTDOWN_TICK_MS) {
        last_tick = now;
        countdown_sec--;

        if (countdown_sec <= 0) {
            // Time's up - enter deep sleep
            enter_deep_sleep();
        }

        // Update the countdown label
        char buf[64];
        snprintf(buf, sizeof(buf), "Sleeping in:  %d seconds...", countdown_sec);
        lv_label_set_text(lbl_countdown, buf);

        Serial.printf("  Countdown: %d\n", countdown_sec);
    }

    delay(10);
}
