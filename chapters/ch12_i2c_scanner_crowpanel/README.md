# Chapter 12: I2C Scanner

## Overview
Scans all I2C addresses (0x00–0x7F) on both I2C buses and displays found devices on the LVGL display with their hex addresses. Useful as a hardware diagnostic before connecting any I2C sensor.

## Hardware Required
- CrowPanel Advanced 7" (Elecrow ESP32-2432S028R)
- USB-C power cable (2A minimum)
- Arduino IDE 2.x
- Any I2C sensor connected to the I2C1 header (GPIO 45/46)

## Libraries
- [esp_display_panel](https://github.com/esp-arduino-libs/ESP32_Display_Panel)
- [LVGL 9.x](https://lvgl.io)

## Board Settings
```
Board:      ESP32P4 Dev Module
USB Mode:   Hardware CDC and JTAG
PSRAM:      OPI PSRAM
Flash Mode: QIO 80MHz
Partition:  Huge APP (3MB No OTA/1MB SPIFFS)
```

## Boards Tested
- ✅ CrowPanel Advanced 7" (Elecrow ESP32-2432S028R)

## How to Use
1. Open `i2c_scanner.ino`
2. Upload
3. Tap **Scan** — detected addresses appear on screen and Serial Monitor

> **Note:** The GT911 touch controller shares I2C0. Use the legacy `driver/i2c.h` API to avoid `driver_ng CONFLICT` crashes.

## Key Concepts
- Wire.begin() with custom SDA/SCL pins
- I2C address scanning loop with ACK detection
- Legacy vs new-gen I2C driver conflict on ESP32-P4
- GT911 at 0x14 or 0x5D (shared bus awareness)

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
