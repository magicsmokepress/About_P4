#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
    lgfx::Bus_SPI       _bus_instance;
    lgfx::Panel_ILI9488 _panel_instance;
    lgfx::Touch_XPT2046 _touch_instance;
public:
    LGFX(void) {
        // SPI bus - shared between LCD and touch
        { auto cfg = _bus_instance.config();
          cfg.spi_host = SPI2_HOST;
          cfg.spi_mode = 0;
          cfg.freq_write = 40000000;    // 40 MHz write clock
          cfg.freq_read  = 16000000;    // 16 MHz read clock
          cfg.pin_sclk = 26;
          cfg.pin_mosi = 23;
          cfg.pin_miso = 27;
          cfg.pin_dc   = 22;
          _bus_instance.config(cfg);
          _panel_instance.setBus(&_bus_instance);
        }
        // LCD panel - ILI9488 in 480×320 landscape
        { auto cfg = _panel_instance.config();
          cfg.pin_cs  = 20;
          cfg.pin_rst = 21;
          cfg.panel_width   = 320;
          cfg.panel_height  = 480;
          cfg.memory_width  = 320;
          cfg.memory_height = 480;
          cfg.offset_x = 0;
          cfg.offset_y = 0;
          _panel_instance.config(cfg);
        }
        // Touch - XPT2046 resistive, shared SPI bus
        { auto cfg = _touch_instance.config();
          cfg.pin_cs   = 33;
          cfg.pin_int  = -1;            // No interrupt pin
          cfg.bus_shared = true;        // Shares SPI with LCD
          cfg.spi_host = SPI2_HOST;
          cfg.freq     = 1000000;       // 1 MHz (touch is slow)
          cfg.pin_sclk = 26;
          cfg.pin_mosi = 23;
          cfg.pin_miso = 27;
          cfg.offset_rotation = 6;      // Axis correction for setRotation(1)
          cfg.x_min = 0; cfg.x_max = 4095;
          cfg.y_min = 0; cfg.y_max = 4095;
          _touch_instance.config(cfg);
          _panel_instance.setTouch(&_touch_instance);
        }
        setPanel(&_panel_instance);
    }
};

LGFX tft;
