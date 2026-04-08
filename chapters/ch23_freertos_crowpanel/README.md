# Chapter 23: FreeRTOS Dual-Core

## Overview
Runs sensor reading and UI update on separate FreeRTOS tasks pinned to different cores (Core 0 and Core 1). Uses a queue and mutex to pass data safely between tasks, eliminating the `in_blocking_io` freeze pattern.

## Hardware Required
- CrowPanel Advanced 7" (Elecrow ESP32-2432S028R)
- USB-C power cable (2A minimum)
- Arduino IDE 2.x
- BME280 sensor (I2C1: GPIO 45/46) — optional, sketch uses simulated data if absent

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
1. Open `freertos.ino`, upload
2. Serial Monitor shows task IDs and core assignments
3. Display updates at ~60 fps regardless of sensor poll timing
4. Optionally wire BME280 for live sensor data

## Key Concepts
- `xTaskCreatePinnedToCore()` for Core 0 / Core 1 assignment
- `xQueueSend` / `xQueueReceive` for inter-task data passing
- `xSemaphoreTake` / `xSemaphoreGive` for LVGL mutex
- Why lv_timer_handler() must run on the UI task only

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
