/**
 * CrowPanel Advanced 7" ESP32-P4 — Arduino "Hello World" Display Demo
 *
 * Uses ESP32_Display_Panel library (v1.0.4+) with the driver-level API
 * since there is no pre-built board profile for the ESP32-P4 CrowPanel yet.
 *
 * Hardware: ESP32-P4, EK79007 (MIPI-DSI), GT911 (I2C touch), 1024×600
 *
 * REQUIRED LIBRARY:
 *   ESP32_Display_Panel by esp-arduino-libs (install via Arduino Library Manager)
 *
 * BOARD SETTINGS in Arduino IDE (all critical!):
 *   Board:      "ESP32P4 Dev Module"  (requires ESP32 Arduino Core 3.x)
 *   PSRAM:      "OPI PSRAM"          ← MUST be enabled or framebuffer alloc fails
 *   Flash Mode: "QIO 80MHz"
 *   Partition:  "Huge APP (3MB No OTA/1MB SPIFFS)"
 */

#include <esp_display_panel.hpp>

using namespace esp_panel::drivers;

// ─── Display Timings (from factory BSP) ────────────────────────────
#define LCD_WIDTH            1024
#define LCD_HEIGHT           600
#define LCD_DSI_LANE_NUM     2
#define LCD_DSI_LANE_RATE    1000      // Mbps per lane
#define LCD_DPI_CLK_MHZ      52
// RGB565 = 1.2 MB framebuffer;  RGB888 = 1.8 MB (may fail without enough PSRAM)
// The factory BSP uses 16-bit colour (BITS_PER_PIXEL = 16)
#define LCD_COLOR_BITS       ESP_PANEL_LCD_COLOR_BITS_RGB565
#define LCD_DPI_HPW          10
#define LCD_DPI_HBP          160
#define LCD_DPI_HFP          160
#define LCD_DPI_VPW          1
#define LCD_DPI_VBP          23
#define LCD_DPI_VFP          12
#define LCD_DSI_PHY_LDO_ID   3        // LDO channel for MIPI PHY
#define LCD_RST_IO           -1       // No dedicated reset pin

// ─── Backlight ─────────────────────────────────────────────────────
#define LCD_BL_IO            31
#define LCD_BL_ON_LEVEL      1

// ─── Touch (GT911 via I2C) ─────────────────────────────────────────
#define TOUCH_I2C_SDA        45
#define TOUCH_I2C_SCL        46
#define TOUCH_I2C_FREQ       (400 * 1000)
#define TOUCH_RST_IO         40
#define TOUCH_INT_IO         42

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("CrowPanel ESP32-P4 Hello World");

    // ── 0. PSRAM diagnostic ─────────────────────────────────────────
    Serial.printf("Total PSRAM: %u bytes\n", ESP.getPsramSize());
    Serial.printf("Free  PSRAM: %u bytes\n", ESP.getFreePsram());
    Serial.printf("Total heap:  %u bytes\n", ESP.getHeapSize());
    Serial.printf("Free  heap:  %u bytes\n", ESP.getFreeHeap());

    // Quick PSRAM allocation test
    void *test = heap_caps_malloc(1024 * 1024, MALLOC_CAP_SPIRAM);
    if (test) {
        Serial.println("PSRAM test: 1 MB alloc OK");
        free(test);
    } else {
        Serial.println("PSRAM test: FAILED — PSRAM not available!");
    }

    // ── 1. Create MIPI-DSI bus ──────────────────────────────────────
    Serial.println("Creating MIPI-DSI bus...");
    BusDSI *bus = new BusDSI(
        LCD_DSI_LANE_NUM,
        LCD_DSI_LANE_RATE,
        LCD_DPI_CLK_MHZ,
        LCD_COLOR_BITS,
        LCD_WIDTH,
        LCD_HEIGHT,
        LCD_DPI_HPW,
        LCD_DPI_HBP,
        LCD_DPI_HFP,
        LCD_DPI_VPW,
        LCD_DPI_VBP,
        LCD_DPI_VFP,
        LCD_DSI_PHY_LDO_ID
    );
    assert(bus);
    bus->configDpiFrameBufferNumber(1);
    assert(bus->begin());
    Serial.println("  DSI bus OK");

    // ── 2. Create LCD (EK79007) ─────────────────────────────────────
    //  Signature: LCD_EK79007(Bus *bus, int width, int height, int color_bits, int rst_io)
    Serial.println("Initializing EK79007 LCD...");
    LCD_EK79007 *lcd = new LCD_EK79007(
        bus,                 // bus handle
        LCD_WIDTH,
        LCD_HEIGHT,
        LCD_COLOR_BITS,
        LCD_RST_IO           // reset GPIO (-1 = none)
    );
    assert(lcd);
    if (!lcd->begin()) {
        Serial.println("  LCD FAILED — check PSRAM numbers above");
        Serial.printf("  Framebuffer needs: %u bytes\n", LCD_WIDTH * LCD_HEIGHT * 2);
        Serial.printf("  Free PSRAM now:    %u bytes\n", ESP.getFreePsram());
        while (1) delay(1000);  // halt so you can read the serial output
    }
    Serial.println("  LCD OK");

    // ── 3. Backlight on ─────────────────────────────────────────────
    Serial.println("Turning on backlight...");
    BacklightPWM_LEDC *backlight = new BacklightPWM_LEDC(
        LCD_BL_IO,
        LCD_BL_ON_LEVEL
    );
    assert(backlight);
    assert(backlight->begin());
    assert(backlight->on());
    Serial.println("  Backlight ON");

    // ── 4. Draw colour bars to prove the display works ──────────────
    Serial.println("Drawing colour bars...");
    lcd->colorBarTest(LCD_WIDTH, LCD_HEIGHT);
    Serial.println("  Done!  You should see colour bars on screen.");
}

void loop()
{
    delay(1000);
}
