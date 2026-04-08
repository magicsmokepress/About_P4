# Chapter 26: Camera (Tab5 SC202CS)

## Overview
Captures frames from the Tab5's onboard SC202CS camera via MIPI-CSI and displays a live preview on the LVGL screen. The camera and display share the MIPI-DSI PHY — this chapter documents the initialization order that makes them coexist.

## Hardware Required
- M5Stack Tab5 (ESP32-P4 + SC202CS camera)
- USB-C power cable
- Arduino IDE 2.x

## Libraries
- [esp_display_panel](https://github.com/esp-arduino-libs/ESP32_Display_Panel)
- [LVGL 9.x](https://lvgl.io)
- esp_camera (built-in to ESP32 Arduino core)

## Board Settings
```
Board:         M5Stack Tab5 (or ESP32P4 Dev Module)
Arduino Core:  esp32 boards 3.2.6
USB Mode:      Hardware CDC and JTAG
PSRAM:         OPI PSRAM
```

## Boards Tested
- ✅ M5Stack Tab5

## How to Use
1. Open `camera.ino`, upload to Tab5
2. Live camera preview appears on display
3. Tap the **Capture** button to freeze the frame

## Key Concepts
- SC202CS MIPI-CSI sensor initialization
- Shared MIPI PHY init order (display must be first)
- Frame buffer in PSRAM
- LVGL canvas → display pipeline for camera frames
- Arduino boards 3.2.6 has everything needed (camera + WiFi + display)

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
