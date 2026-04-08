# Chapter 7: WiFi Scanner

## Overview
Scans for nearby 2.4 GHz networks and displays them in an LVGL list sorted by signal strength (RSSI). Demonstrates WiFi.h STA mode scan without associating to any network.

## Hardware Required
- CrowPanel Advanced 7" (Elecrow ESP32-2432S028R)
- USB-C power cable (2A minimum)
- Arduino IDE 2.x

## Libraries
- [esp_display_panel](https://github.com/esp-arduino-libs/ESP32_Display_Panel)
- [LVGL 9.x](https://lvgl.io)
- WiFi (built-in to ESP32 Arduino core)

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
1. Open `wifi_scanner.ino`
2. Upload
3. Tap **Scan** button on screen; results populate the list
4. Serial Monitor shows scan log

## Key Concepts
- `WiFi.scanNetworks()` in async vs blocking mode
- RSSI-based list sorting
- LVGL list widget + dynamic item creation
- CrowPanel WiFi note: STA scan works; AP mode is unstable (see Ch10)

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
