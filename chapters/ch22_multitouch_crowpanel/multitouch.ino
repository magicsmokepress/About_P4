/******************************************************************************
 * CrowPanel Advanced 7" ESP32-P4 — Chapter 22: Multitouch Gestures
 *
 * Demonstrates GT911 multitouch (up to 5 simultaneous touch points):
 *   Tab 1: Raw point visualizer — colored dot per active finger, coords shown
 *   Tab 2: Pinch-to-zoom       — two-finger spread/squeeze resizes a target box
 *   Tab 3: Two-finger pan      — two-finger drag pans a grid panel
 *   Swipe left/right with one finger to navigate between tabs.
 *
 * Key concepts:
 *   - gt911->readPoints(tp, 5) returns up to 5 simultaneous touch coordinates
 *   - LVGL indev only receives point[0]; custom gesture math uses all points
 *   - Pinch distance delta → scale factor → object resize
 *   - Two-finger midpoint delta → pan offset → object translate
 *   - Single-finger X delta > 80px threshold → tab switch
 *
 * BOARD SETTINGS:
 *   Board:      "ESP32P4 Dev Module"
 *   USB Mode:   "Hardware CDC and JTAG"
 *   PSRAM:      "OPI PSRAM"
 *   Flash Mode: "QIO 80MHz"
 *   Partition:  "Huge APP (3MB No OTA/1MB SPIFFS)"
 *
 * LIBRARIES:
 *   esp_display_panel (ESP_Panel_Library)
 *   LVGL 9.x
 ******************************************************************************/

#include <esp_display_panel.hpp>
#include <esp_lcd_panel_ops.h>
#include <lvgl.h>

using namespace esp_panel::drivers;

// ═══════════════════════════════════════════════════════════════════════════
//  Display + Touch config (proven boilerplate — same as all CrowPanel chapters)
// ═══════════════════════════════════════════════════════════════════════════
#define LCD_WIDTH           1024
#define LCD_HEIGHT          600
#define LCD_DSI_LANE_NUM    2
#define LCD_DSI_LANE_RATE   1000
#define LCD_DPI_CLK_MHZ     52
#define LCD_COLOR_BITS      ESP_PANEL_LCD_COLOR_BITS_RGB565
#define LCD_DPI_HPW         10
#define LCD_DPI_HBP         160
#define LCD_DPI_HFP         160
#define LCD_DPI_VPW         1
#define LCD_DPI_VBP         23
#define LCD_DPI_VFP         12
#define LCD_DSI_PHY_LDO_ID  3
#define LCD_RST_IO          -1
#define LCD_BL_IO           31
#define LCD_BL_ON_LEVEL     1
#define TOUCH_I2C_SDA       45
#define TOUCH_I2C_SCL       46
#define TOUCH_I2C_FREQ      (400 * 1000)
#define TOUCH_RST_IO        40
#define TOUCH_INT_IO        42
#define LVGL_BUF_LINES      60

#define MAX_TOUCH_POINTS    5

// ═══════════════════════════════════════════════════════════════════════════
//  Globals
// ═══════════════════════════════════════════════════════════════════════════
static LCD_EK79007   *g_lcd          = NULL;
static TouchGT911    *g_touch        = NULL;
static lv_display_t  *lvgl_disp      = NULL;
static lv_indev_t    *lvgl_touch_dev = NULL;

// Single-point shadow for LVGL indev (point[0] only)
static volatile bool    touch_pressed = false;
static volatile int16_t touch_x = 0, touch_y = 0;

// UI elements
static lv_obj_t *tabview      = NULL;
static lv_obj_t *tab_points   = NULL;
static lv_obj_t *tab_pinch    = NULL;
static lv_obj_t *tab_scroll   = NULL;

// Tab 1: point visualizer
static lv_obj_t *pt_dot[MAX_TOUCH_POINTS];
static lv_obj_t *pt_lbl[MAX_TOUCH_POINTS];
static lv_obj_t *pt_count_lbl = NULL;

// Tab 2: pinch / zoom
static lv_obj_t *zoom_target  = NULL;
static lv_obj_t *zoom_lbl     = NULL;
static float     zoom_level   = 1.0f;

// Tab 3: two-finger pan
static lv_obj_t *scroll_target = NULL;
static lv_obj_t *scroll_lbl    = NULL;
static int       pan_x = 0, pan_y = 0;

// Gesture tracker
struct GestureState {
    float   prev_dist;      // previous inter-finger distance (pinch)
    int16_t prev_mid_x;     // previous two-finger midpoint (pan)
    int16_t prev_mid_y;
    int16_t swipe_start_x;  // x position when finger first touched (swipe)
    bool    swipe_active;
};
static GestureState gesture = {0, 0, 0, 0, false};

// One distinct color per finger slot
static const uint32_t DOT_COLORS[MAX_TOUCH_POINTS] = {
    0xFF4444, 0x44FF88, 0x4488FF, 0xFFDD44, 0xFF44FF
};

// ═══════════════════════════════════════════════════════════════════════════
//  Display + Touch hardware init
// ═══════════════════════════════════════════════════════════════════════════
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
        data->point.x = touch_x;
        data->point.y = touch_y;
        data->state   = LV_INDEV_STATE_PRESSED;
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

// ═══════════════════════════════════════════════════════════════════════════
//  Gesture: pinch-to-zoom
//  Two fingers → compute inter-finger distance → compare to previous distance
//  → scale factor → resize zoom_target proportionally
// ═══════════════════════════════════════════════════════════════════════════
static void on_zoom(float scale) {
    zoom_level *= scale;
    zoom_level = constrain(zoom_level, 0.3f, 5.0f);
    int new_size = (int)(200 * zoom_level);
    lv_obj_set_size(zoom_target, new_size, new_size);
    lv_obj_align(zoom_target, LV_ALIGN_CENTER, 0, 0);
    char buf[32];
    snprintf(buf, sizeof(buf), "Zoom: %.2fx", zoom_level);
    lv_label_set_text(zoom_lbl, buf);
    Serial.printf("Zoom: %.2fx  size=%dpx\n", zoom_level, new_size);
}

static void check_pinch(uint16_t *x, uint16_t *y) {
    float dx   = x[1] - x[0];
    float dy   = y[1] - y[0];
    float dist = sqrtf(dx * dx + dy * dy);

    if (gesture.prev_dist > 0) {
        float scale = dist / gesture.prev_dist;
        // Ignore < 5% change — filters sensor noise
        if (scale > 1.05f || scale < 0.95f) {
            on_zoom(scale);
        }
    }
    gesture.prev_dist = dist;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Gesture: two-finger pan
//  Two fingers → compute midpoint → delta from previous midpoint → translate
//  scroll_target by that delta, filtered to > 3px to ignore jitter
// ═══════════════════════════════════════════════════════════════════════════
static void on_scroll(int dx, int dy) {
    pan_x += dx;
    pan_y += dy;
    // Clamp so panel doesn't disappear off-screen
    pan_x = constrain(pan_x, -350, 350);
    pan_y = constrain(pan_y, -200, 200);
    lv_obj_set_pos(scroll_target, 362 + pan_x, 160 + pan_y);
    char buf[48];
    snprintf(buf, sizeof(buf), "Pan: (%d, %d)", pan_x, pan_y);
    lv_label_set_text(scroll_lbl, buf);
}

static void check_two_finger_scroll(uint16_t *x, uint16_t *y) {
    int16_t mid_x = (x[0] + x[1]) / 2;
    int16_t mid_y = (y[0] + y[1]) / 2;

    if (gesture.prev_mid_x != 0) {
        int dx = mid_x - gesture.prev_mid_x;
        int dy = mid_y - gesture.prev_mid_y;
        if (abs(dx) > 3 || abs(dy) > 3) {
            on_scroll(dx, dy);
        }
    }
    gesture.prev_mid_x = mid_x;
    gesture.prev_mid_y = mid_y;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Gesture: single-finger swipe → tab navigation
//  Track X on first touch; on release, if delta > 80px switch tab
// ═══════════════════════════════════════════════════════════════════════════
static void on_swipe_left() {
    uint32_t idx = lv_tabview_get_tab_active(tabview);
    if (idx < 2) {
        lv_tabview_set_active(tabview, idx + 1, LV_ANIM_ON);
        Serial.printf("Swipe left → tab %lu\n", idx + 1);
    }
}

static void on_swipe_right() {
    uint32_t idx = lv_tabview_get_tab_active(tabview);
    if (idx > 0) {
        lv_tabview_set_active(tabview, idx - 1, LV_ANIM_ON);
        Serial.printf("Swipe right → tab %lu\n", idx - 1);
    }
}

static void check_swipe(uint16_t x, bool released) {
    if (!gesture.swipe_active) {
        gesture.swipe_start_x = x;
        gesture.swipe_active  = true;
    }
    if (released) {
        int dx = (int)x - gesture.swipe_start_x;
        if (dx < -80)      on_swipe_left();
        else if (dx > 80)  on_swipe_right();
        gesture.swipe_active = false;
    }
}

static void reset_gesture_state() {
    gesture.prev_dist  = 0;
    gesture.prev_mid_x = 0;
    gesture.prev_mid_y = 0;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Tab 1 update — show or hide dots/labels per active finger
// ═══════════════════════════════════════════════════════════════════════════
static void update_points_tab(int count, TouchPoint *pts) {
    char cbuf[32];
    snprintf(cbuf, sizeof(cbuf), "Fingers: %d", count);
    lv_label_set_text(pt_count_lbl, cbuf);

    for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
        if (i < count) {
            lv_obj_set_pos(pt_dot[i], pts[i].x - 20, pts[i].y - 70); // -70: tab bar offset
            lv_obj_remove_flag(pt_dot[i], LV_OBJ_FLAG_HIDDEN);
            char lbuf[24];
            snprintf(lbuf, sizeof(lbuf), "P%d (%d,%d)", i + 1, pts[i].x, pts[i].y);
            lv_label_set_text(pt_lbl[i], lbuf);
            lv_obj_set_pos(pt_lbl[i], pts[i].x + 24, pts[i].y - 80);
            lv_obj_remove_flag(pt_lbl[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(pt_dot[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(pt_lbl[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Main touch poll — reads all points and dispatches to gesture handlers
// ═══════════════════════════════════════════════════════════════════════════
static void poll_touch() {
    if (!g_touch) return;

    TouchPoint tp[MAX_TOUCH_POINTS];
    int pts = g_touch->readPoints(tp, MAX_TOUCH_POINTS);

    // Feed first point to LVGL indev (single-point widgets still work normally)
    if (pts > 0) {
        touch_x       = tp[0].x;
        touch_y       = tp[0].y;
        touch_pressed = true;
    } else {
        touch_pressed = false;
    }

    uint32_t active_tab = lv_tabview_get_tab_active(tabview);

    // Tab 1 always updates its point visualizer
    if (active_tab == 0) {
        update_points_tab(pts, tp);
    }

    if (pts == 0) {
        reset_gesture_state();
        return;
    }

    uint16_t px[MAX_TOUCH_POINTS], py[MAX_TOUCH_POINTS];
    for (int i = 0; i < pts; i++) { px[i] = tp[i].x; py[i] = tp[i].y; }

    if (pts == 1) {
        check_swipe(px[0], false);
    } else {
        // Two or more fingers: reset swipe tracker, run two-finger gestures
        gesture.swipe_active = false;
        if (pts >= 2) {
            if (active_tab == 1) check_pinch(px, py);
            if (active_tab == 2) check_two_finger_scroll(px, py);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Build UI
// ═══════════════════════════════════════════════════════════════════════════
static void build_ui() {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A0A1A), 0);

    tabview = lv_tabview_create(scr);
    lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(tabview, 50);
    lv_obj_set_size(tabview, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_pos(tabview, 0, 0);
    lv_obj_set_style_bg_color(tabview, lv_color_hex(0x0A0A1A), 0);

    tab_points = lv_tabview_add_tab(tabview, "Touch Points");
    tab_pinch  = lv_tabview_add_tab(tabview, "Pinch Zoom");
    tab_scroll = lv_tabview_add_tab(tabview, "Two-Finger Pan");

    // ── Tab 1: raw touch visualizer ──────────────────────────────────────
    lv_obj_set_style_bg_color(tab_points, lv_color_hex(0x0A0A1A), 0);

    pt_count_lbl = lv_label_create(tab_points);
    lv_label_set_text(pt_count_lbl, "Fingers: 0");
    lv_obj_set_style_text_font(pt_count_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(pt_count_lbl, lv_color_hex(0xAABBCC), 0);
    lv_obj_align(pt_count_lbl, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *p_hint = lv_label_create(tab_points);
    lv_label_set_text(p_hint, "Place up to 5 fingers on the screen");
    lv_obj_set_style_text_font(p_hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(p_hint, lv_color_hex(0x445566), 0);
    lv_obj_align(p_hint, LV_ALIGN_BOTTOM_MID, 0, -10);

    for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
        pt_dot[i] = lv_obj_create(tab_points);
        lv_obj_set_size(pt_dot[i], 40, 40);
        lv_obj_set_style_bg_color(pt_dot[i], lv_color_hex(DOT_COLORS[i]), 0);
        lv_obj_set_style_radius(pt_dot[i], 20, 0);      // make it a circle
        lv_obj_set_style_border_width(pt_dot[i], 0, 0);
        lv_obj_set_style_shadow_width(pt_dot[i], 12, 0);
        lv_obj_set_style_shadow_color(pt_dot[i], lv_color_hex(DOT_COLORS[i]), 0);
        lv_obj_add_flag(pt_dot[i], LV_OBJ_FLAG_HIDDEN);

        pt_lbl[i] = lv_label_create(tab_points);
        lv_obj_set_style_text_font(pt_lbl[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(pt_lbl[i], lv_color_hex(DOT_COLORS[i]), 0);
        lv_obj_add_flag(pt_lbl[i], LV_OBJ_FLAG_HIDDEN);
    }

    // ── Tab 2: pinch zoom ─────────────────────────────────────────────────
    lv_obj_set_style_bg_color(tab_pinch, lv_color_hex(0x0A0A1A), 0);

    zoom_target = lv_obj_create(tab_pinch);
    lv_obj_set_size(zoom_target, 200, 200);
    lv_obj_set_style_bg_color(zoom_target, lv_color_hex(0x112244), 0);
    lv_obj_set_style_border_color(zoom_target, lv_color_hex(0x4488FF), 0);
    lv_obj_set_style_border_width(zoom_target, 3, 0);
    lv_obj_set_style_radius(zoom_target, 16, 0);
    lv_obj_set_style_shadow_width(zoom_target, 20, 0);
    lv_obj_set_style_shadow_color(zoom_target, lv_color_hex(0x2255AA), 0);
    lv_obj_align(zoom_target, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *zt_inner = lv_label_create(zoom_target);
    lv_label_set_text(zt_inner, "Pinch Me");
    lv_obj_set_style_text_font(zt_inner, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(zt_inner, lv_color_hex(0x88BBFF), 0);
    lv_obj_align(zt_inner, LV_ALIGN_CENTER, 0, 0);

    zoom_lbl = lv_label_create(tab_pinch);
    lv_label_set_text(zoom_lbl, "Zoom: 1.00x");
    lv_obj_set_style_text_font(zoom_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(zoom_lbl, lv_color_hex(0x4488FF), 0);
    lv_obj_align(zoom_lbl, LV_ALIGN_BOTTOM_MID, 0, -10);

    lv_obj_t *z_hint = lv_label_create(tab_pinch);
    lv_label_set_text(z_hint, "Two fingers: spread to zoom in, pinch to zoom out");
    lv_obj_set_style_text_font(z_hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(z_hint, lv_color_hex(0x334466), 0);
    lv_obj_align(z_hint, LV_ALIGN_TOP_MID, 0, 8);

    // ── Tab 3: two-finger pan ─────────────────────────────────────────────
    lv_obj_set_style_bg_color(tab_scroll, lv_color_hex(0x0A0A1A), 0);

    scroll_target = lv_obj_create(tab_scroll);
    lv_obj_set_size(scroll_target, 300, 230);
    lv_obj_set_style_bg_color(scroll_target, lv_color_hex(0x0D1F0D), 0);
    lv_obj_set_style_border_color(scroll_target, lv_color_hex(0x44BB44), 0);
    lv_obj_set_style_border_width(scroll_target, 3, 0);
    lv_obj_set_style_radius(scroll_target, 12, 0);
    lv_obj_set_style_shadow_width(scroll_target, 16, 0);
    lv_obj_set_style_shadow_color(scroll_target, lv_color_hex(0x225522), 0);
    lv_obj_set_pos(scroll_target, 362, 160);   // roughly centered

    // A 3×3 grid inside the panel so panning is visually obvious
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            lv_obj_t *cell = lv_obj_create(scroll_target);
            lv_obj_set_size(cell, 80, 58);
            lv_obj_set_pos(cell, 10 + col * 92, 10 + row * 68);
            lv_obj_set_style_bg_color(cell, lv_color_hex(0x162616), 0);
            lv_obj_set_style_border_color(cell, lv_color_hex(0x336633), 0);
            lv_obj_set_style_border_width(cell, 1, 0);
            lv_obj_set_style_radius(cell, 6, 0);
            char cbuf[8];
            snprintf(cbuf, sizeof(cbuf), "%d,%d", col + 1, row + 1);
            lv_obj_t *cl = lv_label_create(cell);
            lv_label_set_text(cl, cbuf);
            lv_obj_set_style_text_color(cl, lv_color_hex(0x55CC55), 0);
            lv_obj_set_style_text_font(cl, &lv_font_montserrat_14, 0);
            lv_obj_align(cl, LV_ALIGN_CENTER, 0, 0);
        }
    }

    scroll_lbl = lv_label_create(tab_scroll);
    lv_label_set_text(scroll_lbl, "Pan: (0, 0)");
    lv_obj_set_style_text_font(scroll_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(scroll_lbl, lv_color_hex(0x44CC44), 0);
    lv_obj_align(scroll_lbl, LV_ALIGN_BOTTOM_MID, 0, -10);

    lv_obj_t *s_hint = lv_label_create(tab_scroll);
    lv_label_set_text(s_hint, "Two fingers: drag to pan the grid");
    lv_obj_set_style_text_font(s_hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(0x336633), 0);
    lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, 8);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Setup & Loop
// ═══════════════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n╔═══════════════════════════════════════════════╗");
    Serial.println("║  CrowPanel P4 — Chapter 22: Multitouch Demo   ║");
    Serial.println("╚═══════════════════════════════════════════════╝\n");
    Serial.println("Gesture reference:");
    Serial.println("  1 finger  → swipe left/right to change tabs");
    Serial.println("  2 fingers → pinch/zoom on Tab 2");
    Serial.println("  2 fingers → drag/pan  on Tab 3");
    Serial.println("  up to 5   → visualized on Tab 1\n");

    init_hardware();
    build_ui();
    lv_timer_handler();

    Serial.println("Ready.\n");
}

void loop() {
    static uint32_t lt = 0;
    if (millis() - lt > 20) {
        lt = millis();
        poll_touch();
    }
    lv_timer_handler();
    delay(5);
}
