# Chapter 30: WiFi Camera Stream (Tab5)

## Overview
Streams live MJPEG video from the Tab5 camera over WiFi. A built-in HTTP server serves the MJPEG stream at `/stream`, viewable in any browser on the same network.

## Hardware Required
- M5Stack Tab5 (ESP32-P4 + SC202CS camera)
- USB-C power cable
- Arduino IDE 2.x
- WiFi network (2.4 GHz)

## Libraries
- [esp_display_panel](https://github.com/esp-arduino-libs/ESP32_Display_Panel)
- [LVGL 9.x](https://lvgl.io)
- WiFi (built-in)
- esp_camera (built-in)

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
1. Edit `wifi_camera_stream.ino`: set `WIFI_SSID` and `WIFI_PASS`
2. Upload
3. Serial Monitor prints the stream URL (e.g. `http://192.168.1.45/stream`)
4. Open URL in browser — live video streams at ~10 fps

## Key Concepts
- MJPEG multipart HTTP response format
- WiFi.setPins() required on Tab5 (boards 3.2.6+)
- Balancing JPEG quality vs network bandwidth
- Camera frame buffer management with multi-frame queue

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
