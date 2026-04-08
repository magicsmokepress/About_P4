# Chapter 22: Multitouch Gestures

## Overview
Demonstrates the GT911's multitouch capability (up to 5 simultaneous touch points) with three gesture types: raw point visualization, pinch-to-zoom, and two-finger pan. Swipe left/right with one finger to switch between demo tabs.

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
1. Open `multitouch.ino`, upload
2. Tab 1 — place up to 5 fingers; colored dots track each point
3. Tab 2 — use two fingers to pinch/spread; box resizes
4. Tab 3 — use two fingers to drag the grid panel
5. Single-finger swipe left/right switches tabs

## Key Concepts
- `g_touch->readPoints(tp, 5)` vs single-point indev
- Pinch: inter-finger distance delta → scale factor
- Pan: two-finger midpoint delta → translate offset
- Swipe: single-finger X delta threshold → tab switch
- Routing all points to LVGL indev (point[0]) while custom math uses all

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
