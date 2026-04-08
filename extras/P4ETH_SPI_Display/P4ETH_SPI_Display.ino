/*******************************************************************************
 * P4ETH_SPI_Display.ino
 *
 * Demonstrates ILI9488 display initialization and basic drawing using
 * LovyanGFX on the Waveshare ESP32-P4-ETH board. Shows filled shapes,
 * text rendering, a color palette, and a live FPS counter.
 *
 * Hardware: Waveshare ESP32-P4-ETH
 *   - Display: ILI9488 320x480 SPI
 *   - SPI Host: SPI2_HOST
 *   - SCLK: GPIO 26
 *   - MOSI: GPIO 23
 *   - MISO: GPIO 27
 *   - DC:   GPIO 22
 *   - CS:   GPIO 20
 *   - RST:  GPIO 21
 *
 * Required libraries:
 *   - LovyanGFX (install via Library Manager)
 *   - Arduino ESP32 core (3.x)
 *
 * No touch or network — display drawing only.
 *
 * Author: Educational example for ESP32-P4
 ******************************************************************************/

#include <LovyanGFX.hpp>

// ---------------------------------------------------------------------------
// LovyanGFX display driver class for Waveshare ESP32-P4-ETH ILI9488
// ---------------------------------------------------------------------------
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9488 _panel;
    lgfx::Bus_SPI       _bus;

public:
    LGFX() {
        // --- SPI bus configuration ---
        auto busCfg       = _bus.config();
        busCfg.spi_host   = SPI2_HOST;
        busCfg.spi_mode   = 0;
        busCfg.freq_write  = 40000000;   // 40 MHz write
        busCfg.freq_read   = 16000000;   // 16 MHz read
        busCfg.pin_sclk   = 26;
        busCfg.pin_mosi   = 23;
        busCfg.pin_miso   = 27;
        busCfg.pin_dc     = 22;
        _bus.config(busCfg);
        _panel.setBus(&_bus);

        // --- Panel configuration ---
        auto panelCfg          = _panel.config();
        panelCfg.pin_cs        = 20;
        panelCfg.pin_rst       = 21;
        panelCfg.pin_busy      = -1;
        panelCfg.panel_width   = 320;
        panelCfg.panel_height  = 480;
        panelCfg.offset_x      = 0;
        panelCfg.offset_y      = 0;
        panelCfg.readable      = true;
        panelCfg.invert        = false;
        panelCfg.rgb_order     = false;
        panelCfg.dlen_16bit    = false;
        panelCfg.bus_shared    = false;
        _panel.config(panelCfg);

        setPanel(&_panel);
    }
};

static LGFX display;

// Screen dimensions after rotation(1) — landscape
static const int SCREEN_W = 480;
static const int SCREEN_H = 320;

// FPS tracking
static unsigned long frameCount    = 0;
static unsigned long lastFpsTime   = 0;
static float         currentFps    = 0.0f;

// ---------------------------------------------------------------------------
// Draw a color palette grid
// ---------------------------------------------------------------------------
void drawColorPalette(int x, int y, int cellW, int cellH, int cols) {
    // 16 colors from the classic VGA palette
    const uint32_t colors[] = {
        0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
        0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
        0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
        0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
    };
    const int numColors = sizeof(colors) / sizeof(colors[0]);

    for (int i = 0; i < numColors; i++) {
        int col = i % cols;
        int row = i / cols;
        int cx  = x + col * cellW;
        int cy  = y + row * cellH;

        display.fillRect(cx, cy, cellW - 2, cellH - 2, display.color888(
            (colors[i] >> 16) & 0xFF,
            (colors[i] >> 8)  & 0xFF,
             colors[i]        & 0xFF
        ));
    }
}

// ---------------------------------------------------------------------------
// Draw decorative shapes
// ---------------------------------------------------------------------------
void drawShapesDemo() {
    // Filled rectangles
    display.fillRect(10, 10, 120, 60, TFT_BLUE);
    display.fillRect(20, 20, 100, 40, TFT_CYAN);
    display.drawRect(10, 10, 120, 60, TFT_WHITE);

    // Circles
    display.fillCircle(200, 40, 30, TFT_RED);
    display.drawCircle(200, 40, 30, TFT_WHITE);
    display.fillCircle(260, 40, 20, TFT_GREEN);
    display.drawCircle(260, 40, 20, TFT_WHITE);

    // Lines — radiating from a point
    for (int angle = 0; angle < 360; angle += 15) {
        float rad = angle * DEG_TO_RAD;
        int ex = 400 + (int)(35 * cos(rad));
        int ey = 40  + (int)(35 * sin(rad));
        display.drawLine(400, 40, ex, ey, display.color565(
            128 + (int)(127 * cos(rad)),
            128 + (int)(127 * sin(rad)),
            200
        ));
    }

    // Triangles
    display.fillTriangle(50, 140, 10, 200, 90, 200, TFT_MAGENTA);
    display.drawTriangle(50, 140, 10, 200, 90, 200, TFT_WHITE);

    // Rounded rectangle
    display.fillRoundRect(110, 140, 140, 60, 12, TFT_DARKGREEN);
    display.drawRoundRect(110, 140, 140, 60, 12, TFT_WHITE);
}

// ---------------------------------------------------------------------------
// Draw text samples at various sizes
// ---------------------------------------------------------------------------
void drawTextDemo() {
    display.setTextColor(TFT_YELLOW, TFT_BLACK);

    display.setTextSize(1);
    display.setCursor(280, 100);
    display.print("Size 1: Hello P4!");

    display.setTextSize(2);
    display.setCursor(280, 120);
    display.print("Size 2: ESP32");

    display.setTextSize(3);
    display.setCursor(280, 150);
    display.print("Size 3");

    // Styled text
    display.setTextColor(TFT_WHITE, TFT_NAVY);
    display.setTextSize(1);
    display.setCursor(10, 215);
    display.print("Waveshare ESP32-P4-ETH  |  ILI9488 320x480  |  LovyanGFX");
}

// ---------------------------------------------------------------------------
// Update FPS counter (bottom-right corner)
// ---------------------------------------------------------------------------
void updateFps() {
    frameCount++;
    unsigned long now = millis();

    if (now - lastFpsTime >= 1000) {
        currentFps   = (float)frameCount * 1000.0f / (float)(now - lastFpsTime);
        frameCount   = 0;
        lastFpsTime  = now;
    }

    // Draw FPS in bottom-right
    display.setTextColor(TFT_GREEN, TFT_BLACK);
    display.setTextSize(2);
    display.setCursor(SCREEN_W - 130, SCREEN_H - 25);
    display.printf("FPS: %.1f  ", currentFps);
}

// ---------------------------------------------------------------------------
// setup()
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("P4ETH SPI Display Demo — starting...");

    display.init();
    display.setRotation(1);        // Landscape: 480 x 320
    display.fillScreen(TFT_BLACK);
    display.setTextWrap(false);

    Serial.printf("Display: %d x %d\n", display.width(), display.height());

    // Draw the static elements once
    drawShapesDemo();
    drawTextDemo();

    // Color palette in the lower-left area
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextSize(1);
    display.setCursor(10, 235);
    display.print("VGA Color Palette:");
    drawColorPalette(10, 248, 28, 28, 8);

    lastFpsTime = millis();
    Serial.println("Display initialized — running FPS counter.");
}

// ---------------------------------------------------------------------------
// loop()
// ---------------------------------------------------------------------------
void loop() {
    updateFps();

    // Small animation: bouncing dot to show the loop is alive
    static int   dotX   = SCREEN_W / 2;
    static int   dotY   = SCREEN_H / 2;
    static int   dx     = 2;
    static int   dy     = 1;
    static const int DOT_R = 5;

    // Erase old dot
    display.fillCircle(dotX, dotY, DOT_R + 1, TFT_BLACK);

    dotX += dx;
    dotY += dy;

    // Bounce within a region that avoids the static drawings
    if (dotX <= 270 || dotX >= SCREEN_W - 10)  dx = -dx;
    if (dotY <= 180 || dotY >= SCREEN_H - 35)  dy = -dy;

    // Draw new dot
    display.fillCircle(dotX, dotY, DOT_R, TFT_ORANGE);

    delay(16);  // ~60 FPS target
}
