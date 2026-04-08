/*******************************************************************************
 * P4ETH_Touch_Input.ino
 *
 * Demonstrates XPT2046 resistive touch input with a drawing canvas on the
 * Waveshare ESP32-P4-ETH board. Features a color picker bar, coordinate
 * display, and long-press-to-clear functionality.
 *
 * Hardware: Waveshare ESP32-P4-ETH
 *   - Display: ILI9488 320x480 SPI
 *     - SPI Host: SPI2_HOST
 *     - SCLK: GPIO 26, MOSI: GPIO 23, MISO: GPIO 27
 *     - DC: GPIO 22, CS: GPIO 20, RST: GPIO 21
 *   - Touch: XPT2046
 *     - CS: GPIO 33 (shared SPI bus)
 *     - freq: 1 MHz, offset_rotation: 6
 *
 * Required libraries:
 *   - LovyanGFX (install via Library Manager)
 *   - Arduino ESP32 core (3.x)
 *
 * Usage:
 *   - Tap a color in the top bar to select it
 *   - Touch the canvas area to draw colored dots
 *   - Long press (1.5 s) anywhere on the canvas to clear
 *   - Current touch coordinates shown in the status bar
 *
 * Author: Educational example for ESP32-P4
 ******************************************************************************/

#include <LovyanGFX.hpp>

// ---------------------------------------------------------------------------
// LovyanGFX driver with ILI9488 + XPT2046 touch
// ---------------------------------------------------------------------------
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9488  _panel;
    lgfx::Bus_SPI        _bus;
    lgfx::Touch_XPT2046  _touch;

public:
    LGFX() {
        // --- SPI bus ---
        auto busCfg        = _bus.config();
        busCfg.spi_host    = SPI2_HOST;
        busCfg.spi_mode    = 0;
        busCfg.freq_write  = 40000000;
        busCfg.freq_read   = 16000000;
        busCfg.pin_sclk    = 26;
        busCfg.pin_mosi    = 23;
        busCfg.pin_miso    = 27;
        busCfg.pin_dc      = 22;
        _bus.config(busCfg);
        _panel.setBus(&_bus);

        // --- Panel ---
        auto panelCfg           = _panel.config();
        panelCfg.pin_cs         = 20;
        panelCfg.pin_rst        = 21;
        panelCfg.pin_busy       = -1;
        panelCfg.panel_width    = 320;
        panelCfg.panel_height   = 480;
        panelCfg.offset_x       = 0;
        panelCfg.offset_y       = 0;
        panelCfg.readable       = true;
        panelCfg.invert         = false;
        panelCfg.rgb_order      = false;
        panelCfg.dlen_16bit     = false;
        panelCfg.bus_shared     = true;   // Shared with touch
        _panel.config(panelCfg);

        // --- Touch (XPT2046) ---
        auto touchCfg           = _touch.config();
        touchCfg.x_min          = 0;
        touchCfg.x_max          = 319;
        touchCfg.y_min          = 0;
        touchCfg.y_max          = 479;
        touchCfg.pin_int        = -1;     // No interrupt pin used
        touchCfg.pin_cs         = 33;
        touchCfg.bus_shared     = true;
        touchCfg.freq           = 1000000; // 1 MHz
        touchCfg.spi_host       = SPI2_HOST;
        touchCfg.offset_rotation = 6;
        _touch.config(touchCfg);

        _panel.setTouch(&_touch);
        setPanel(&_panel);
    }
};

static LGFX display;

// Screen dimensions (landscape after rotation 1)
static const int SCREEN_W = 480;
static const int SCREEN_H = 320;

// ---------------------------------------------------------------------------
// Color picker configuration
// ---------------------------------------------------------------------------
static const int COLOR_BAR_H   = 36;      // Height of the color bar at top
static const int STATUS_BAR_H  = 24;      // Status bar at the bottom
static const int CANVAS_TOP    = COLOR_BAR_H;
static const int CANVAS_BOTTOM = SCREEN_H - STATUS_BAR_H;

struct ColorEntry {
    uint16_t color;
    const char *name;
};

static const ColorEntry palette[] = {
    { TFT_RED,       "Red"     },
    { TFT_ORANGE,    "Orange"  },
    { TFT_YELLOW,    "Yellow"  },
    { TFT_GREEN,     "Green"   },
    { TFT_CYAN,      "Cyan"    },
    { TFT_BLUE,      "Blue"    },
    { TFT_MAGENTA,   "Magenta" },
    { TFT_WHITE,     "White"   },
    { TFT_BLACK,     "Black"   },
};
static const int NUM_COLORS = sizeof(palette) / sizeof(palette[0]);

static int      selectedColor = 0;        // Index into palette
static int      brushSize     = 4;        // Dot radius

// Long-press detection
static unsigned long touchStartTime = 0;
static bool          touchActive    = false;
static const unsigned long LONG_PRESS_MS = 1500;

// ---------------------------------------------------------------------------
// Draw the color picker bar
// ---------------------------------------------------------------------------
void drawColorBar() {
    int cellW = SCREEN_W / NUM_COLORS;

    for (int i = 0; i < NUM_COLORS; i++) {
        int x = i * cellW;
        display.fillRect(x, 0, cellW, COLOR_BAR_H, palette[i].color);

        // Draw selection indicator
        if (i == selectedColor) {
            display.drawRect(x + 1, 1, cellW - 2, COLOR_BAR_H - 2, TFT_WHITE);
            display.drawRect(x + 2, 2, cellW - 4, COLOR_BAR_H - 4, TFT_BLACK);
        }
    }
}

// ---------------------------------------------------------------------------
// Draw the status bar at the bottom
// ---------------------------------------------------------------------------
void drawStatusBar(int tx, int ty, bool touching) {
    display.fillRect(0, CANVAS_BOTTOM, SCREEN_W, STATUS_BAR_H, TFT_DARKGREY);
    display.setTextColor(TFT_WHITE, TFT_DARKGREY);
    display.setTextSize(1);

    display.setCursor(8, CANVAS_BOTTOM + 7);
    if (touching) {
        display.printf("Touch: (%3d, %3d)  |  Color: %-8s  |  Brush: %d  |  Long press to clear",
                       tx, ty, palette[selectedColor].name, brushSize);
    } else {
        display.printf("Ready  |  Color: %-8s  |  Brush: %d  |  Long press to clear",
                       palette[selectedColor].name, brushSize);
    }
}

// ---------------------------------------------------------------------------
// Clear the canvas area
// ---------------------------------------------------------------------------
void clearCanvas() {
    display.fillRect(0, CANVAS_TOP, SCREEN_W, CANVAS_BOTTOM - CANVAS_TOP, TFT_BLACK);

    // Show a brief "Cleared!" message
    display.setTextColor(TFT_GREEN, TFT_BLACK);
    display.setTextSize(3);
    int msgX = (SCREEN_W - 8 * 3 * 8) / 2;  // Approximate centering
    int msgY = (CANVAS_TOP + CANVAS_BOTTOM) / 2 - 12;
    display.setCursor(msgX, msgY);
    display.print("Cleared!");
    delay(400);
    display.fillRect(0, CANVAS_TOP, SCREEN_W, CANVAS_BOTTOM - CANVAS_TOP, TFT_BLACK);
}

// ---------------------------------------------------------------------------
// setup()
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("P4ETH Touch Input Demo — starting...");

    display.init();
    display.setRotation(1);  // Landscape 480x320
    display.fillScreen(TFT_BLACK);
    display.setTextWrap(false);

    // Calibrate touch (optional — adjust if coordinates are off)
    // display.setTouchCalibrate(...);

    drawColorBar();
    drawStatusBar(0, 0, false);

    Serial.println("Touch the screen to draw. Tap a color to select it.");
    Serial.println("Long press (1.5s) on canvas to clear.");
}

// ---------------------------------------------------------------------------
// loop()
// ---------------------------------------------------------------------------
void loop() {
    lgfx::touch_point_t tp;
    int touchCount = display.getTouchRaw(&tp);

    if (touchCount > 0) {
        // Convert raw touch to display coordinates
        display.convertRawXY(&tp, 1);
        int tx = tp.x;
        int ty = tp.y;

        // --- Long press detection ---
        if (!touchActive) {
            touchActive    = true;
            touchStartTime = millis();
        } else if (millis() - touchStartTime >= LONG_PRESS_MS) {
            // Long press detected — clear canvas
            clearCanvas();
            drawStatusBar(tx, ty, true);
            touchActive    = false;
            touchStartTime = 0;
            delay(300);  // Debounce after clear
            return;
        }

        // --- Color bar tap ---
        if (ty < COLOR_BAR_H) {
            int cellW = SCREEN_W / NUM_COLORS;
            int newSel = tx / cellW;
            if (newSel >= 0 && newSel < NUM_COLORS && newSel != selectedColor) {
                selectedColor = newSel;
                drawColorBar();
                Serial.printf("Selected color: %s\n", palette[selectedColor].name);
            }
        }
        // --- Canvas drawing ---
        else if (ty >= CANVAS_TOP && ty < CANVAS_BOTTOM) {
            display.fillCircle(tx, ty, brushSize, palette[selectedColor].color);
        }

        drawStatusBar(tx, ty, true);

    } else {
        // Touch released
        if (touchActive) {
            touchActive = false;
            drawStatusBar(0, 0, false);
        }
    }

    delay(10);
}
