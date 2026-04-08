#include <esp_display_panel.hpp>
using namespace esp_panel::drivers;

// 1. Create the MIPI-DSI bus
BusDSI *bus = new BusDSI(/* lane rate, lanes, DPI clock, timing params */);

// 2. Create the LCD driver
LCD_EK79007 *lcd = new LCD_EK79007("EK79007", bus, /* resolution, color mode */);

// 3. Create the backlight
BacklightPWM_LEDC *bl = new BacklightPWM_LEDC(/* GPIO pin, default brightness */);
