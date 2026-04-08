# Chapter 5: LVGL Fundamentals

## Overview
Covers core LVGL 9.x concepts: widgets, styles, events, and layouts. Builds a multi-widget UI with buttons, labels, sliders, and a scroll view to demonstrate the LVGL object model and style system.

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
1. Install `esp_display_panel` and `LVGL`
2. Open `lvgl_fundamentals.ino`
3. Upload and explore the widget demo on-screen

## Key Concepts
- lv_obj_t hierarchy (screens, parents, children)
- lv_style_t and inline style setters
- lv_event_cb_t callbacks for button press
- lv_layout (flex, grid) for responsive positioning
- lv_timer_handler() in loop

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
