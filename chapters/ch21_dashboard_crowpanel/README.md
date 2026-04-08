# Chapter 21: Sensor Dashboard

## Overview
A three-tab LVGL dashboard combining BME280 environment data, live charts, and WiFi network status. Demonstrates polling multiple sensors, updating charts, and building a production-style tabbed UI.

## Hardware Required
- CrowPanel Advanced 7" (Elecrow ESP32-2432S028R)
- USB-C power cable (2A minimum)
- Arduino IDE 2.x
- BME280 sensor (I2C1: GPIO 45/46)
- Two USB-C cables for WiFi stability

## Libraries
- [esp_display_panel](https://github.com/esp-arduino-libs/ESP32_Display_Panel)
- [LVGL 9.x](https://lvgl.io)
- WiFi (built-in)

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
1. Wire BME280 to I2C1 header
2. Edit `dashboard.ino`: set `WIFI_SSID` and `WIFI_PASS`
3. Upload; three tabs show Environment, Charts, and Network

## Key Concepts
- LVGL tabview with multiple content tabs
- lv_chart with scrolling time-series data
- Polling multiple peripherals at different intervals
- WiFi status display (RSSI, IP, channel)

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
