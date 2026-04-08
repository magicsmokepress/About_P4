# Chapter 4: Hello World Display

## Overview
The first runnable sketch for the CrowPanel: initializes the EK79007 DSI panel and GT911 touch controller, draws an LVGL label, and prints touch coordinates to Serial. Establishes the boilerplate that all subsequent chapters build on.

## Hardware Required
- CrowPanel Advanced 7" (Elecrow ESP32-2432S028R)
- USB-C power cable (2A minimum)
- Arduino IDE 2.x

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
1. Install `esp_display_panel` and `LVGL` via Arduino Library Manager
2. Open `hello_world.ino`
3. Select board and configure settings
4. Upload — display should show a centered "Hello, ESP32-P4!" label
5. Touch the screen; Serial Monitor shows coordinates

## Key Concepts
- BusDSI + LCD_EK79007 + TouchGT911 init pattern
- LVGL 9.x display and indev setup
- PSRAM framebuffer allocation with `heap_caps_malloc`
- lvgl_flush_cb and lvgl_touch_cb boilerplate

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
