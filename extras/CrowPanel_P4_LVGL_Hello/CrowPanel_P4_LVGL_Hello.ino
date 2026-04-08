/**
 * CrowPanel Advanced 7" ESP32-P4 — LVGL "Hello World" with Touch
 *
 * Uses ESP32_Display_Panel library (v1.0.4+) driver-level API.
 * Displays "Hello World!" label centred on screen with touch feedback.
 *
 * REQUIRED LIBRARIES (install via Arduino Library Manager):
 *   1. ESP32_Display_Panel   by esp-arduino-libs
 *   2. lvgl                  (v9.x — matching the SquareLine examples)
 *
 * BOARD SETTINGS in Arduino IDE (all critical!):
 *   Board:      "ESP32P4 Dev Module"  (requires ESP32 Arduino Core 3.x)
 *   PSRAM:      "OPI PSRAM"          ← MUST be enabled or framebuffer alloc fails
 *   Flash Mode: "QIO 80MHz"
 *   Partition:  "Huge APP (3MB No OTA/1MB SPIFFS)"
 *
 * NOTE: You must copy lv_conf.h into your Arduino/libraries/ folder.
 *       A minimal lv_conf.h is provided as a companion file.
 */

#include <esp_display_panel.hpp>
#include <lvgl.h>

using namespace esp_panel::drivers;

// ─── Display Timings ───────────────────────────────────────────────
#define LCD_WIDTH            1024
#define LCD_HEIGHT           600
#define LCD_DSI_LANE_NUM     2
#define LCD_DSI_LANE_RATE    1000
#define LCD_DPI_CLK_MHZ      52
// RGB565 = 1.2 MB framebuffer;  RGB888 = 1.8 MB (fails without enough PSRAM)
// The factory BSP also uses 16-bit colour (BITS_PER_PIXEL = 16)
#define LCD_COLOR_BITS       ESP_PANEL_LCD_COLOR_BITS_RGB565
#define LCD_DPI_HPW          10
#define LCD_DPI_HBP          160
#define LCD_DPI_HFP          160
#define LCD_DPI_VPW          1
#define LCD_DPI_VBP          23
#define LCD_DPI_VFP          12
#define LCD_DSI_PHY_LDO_ID   3
#define LCD_RST_IO           -1

// ─── Backlight ─────────────────────────────────────────────────────
#define LCD_BL_IO            31
#define LCD_BL_ON_LEVEL      1

// ─── Touch (GT911) ─────────────────────────────────────────────────
#define TOUCH_I2C_SDA        45
#define TOUCH_I2C_SCL        46
#define TOUCH_I2C_FREQ       (400 * 1000)
#define TOUCH_RST_IO         40
#define TOUCH_INT_IO         42

// ─── LVGL draw buffer ──────────────────────────────────────────────
#define LVGL_BUF_LINES       60        // Number of lines per flush
static lv_display_t  *lvgl_disp = NULL;
static lv_indev_t    *lvgl_touch = NULL;

// Pointers kept globally so LVGL callbacks can reach them
static LCD_EK79007   *g_lcd = NULL;
static TouchGT911    *g_touch = NULL;

// ─── LVGL flush callback ──────────────────────────────────────────
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    int w = lv_area_get_width(area);
    int h = lv_area_get_height(area);
    g_lcd->drawBitmap(area->x1, area->y1, w, h, (const uint8_t *)px_map);
    lv_display_flush_ready(disp);
}

// ─── LVGL touch read callback ─────────────────────────────────────
static void lvgl_touch_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)lv_indev_get_user_data(indev);
    uint16_t x[1], y[1], strength[1];
    uint8_t cnt = 0;

    esp_lcd_touch_read_data(tp);
    if (esp_lcd_touch_get_coordinates(tp, x, y, strength, &cnt, 1) && cnt > 0) {
        data->point.x = x[0];
        data->point.y = y[0];
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state   = LV_INDEV_STATE_RELEASED;
    }
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("CrowPanel ESP32-P4 LVGL Hello World");

    // ── 1. MIPI-DSI bus ─────────────────────────────────────────────
    BusDSI *bus = new BusDSI(
        LCD_DSI_LANE_NUM, LCD_DSI_LANE_RATE,
        LCD_DPI_CLK_MHZ, LCD_COLOR_BITS,
        LCD_WIDTH, LCD_HEIGHT,
        LCD_DPI_HPW, LCD_DPI_HBP, LCD_DPI_HFP,
        LCD_DPI_VPW, LCD_DPI_VBP, LCD_DPI_VFP,
        LCD_DSI_PHY_LDO_ID
    );
    bus->configDpiFrameBufferNumber(1);
    assert(bus->begin());

    // ── 2. LCD (EK79007) ────────────────────────────────────────────
    //  Signature: LCD_EK79007(Bus *bus, int width, int height, int color_bits, int rst_io)
    g_lcd = new LCD_EK79007(bus, LCD_WIDTH, LCD_HEIGHT,
                            LCD_COLOR_BITS, LCD_RST_IO);
    assert(g_lcd->begin());
    // displayOn() not supported by EK79007 driver — panel is on after begin()

    // ── 3. Backlight ────────────────────────────────────────────────
    BacklightPWM_LEDC *backlight = new BacklightPWM_LEDC(LCD_BL_IO, LCD_BL_ON_LEVEL);
    assert(backlight->begin());
    assert(backlight->on());

    // ── 4. Touch (GT911 over I2C) ───────────────────────────────────
    //  Use the library's macro for the I2C control panel config, then set
    //  frequency and pullups via helper methods BEFORE begin().
    Serial.println("Initializing touch...");
    BusI2C *touch_bus = new BusI2C(
        TOUCH_I2C_SCL, TOUCH_I2C_SDA,
        (BusI2C::ControlPanelFullConfig) ESP_PANEL_TOUCH_I2C_CONTROL_PANEL_CONFIG(GT911)
    );
    touch_bus->configI2C_FreqHz(TOUCH_I2C_FREQ);
    touch_bus->configI2C_PullupEnable(true, true);

    g_touch = new TouchGT911(
        touch_bus,
        LCD_WIDTH, LCD_HEIGHT,
        TOUCH_RST_IO, TOUCH_INT_IO
    );
    bool touch_ok = g_touch->begin();
    Serial.println(touch_ok ? "  Touch OK" : "  Touch FAILED — display will work without touch");

    // ── 5. Initialize LVGL ──────────────────────────────────────────
    lv_init();

    // Create display driver
    size_t buf_size = LCD_WIDTH * LVGL_BUF_LINES * sizeof(lv_color_t);
    uint8_t *buf1 = (uint8_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    assert(buf1);

    lvgl_disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
    lv_display_set_flush_cb(lvgl_disp, lvgl_flush_cb);
    lv_display_set_buffers(lvgl_disp, buf1, NULL, buf_size,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    // Create touch input device (only if touch initialized)
    if (touch_ok) {
        lvgl_touch = lv_indev_create();
        lv_indev_set_type(lvgl_touch, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(lvgl_touch, lvgl_touch_cb);
        lv_indev_set_user_data(lvgl_touch, (void *)g_touch->getHandle());
    }

    // ── 6. Build the UI ─────────────────────────────────────────────
    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "Hello World!");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFF5529), 0);
    lv_obj_center(label);

    Serial.println("Setup complete — Hello World displayed!");
}

void loop()
{
    lv_timer_handler();    // Let LVGL do its work
    delay(5);              // ~200 fps max
}
