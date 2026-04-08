# Chapter 10: WiFi STA Config Portal

## Overview
A WiFi configuration portal: the ESP32-P4 starts in AP mode briefly to accept SSID/password via a web form, saves credentials to NVS, then reconnects as STA. Includes a fallback timeout so it reverts to STA if no config is received.

## Hardware Required
- CrowPanel Advanced 7" (Elecrow ESP32-2432S028R)
- USB-C power cable (2A minimum)
- Arduino IDE 2.x

## Libraries
- [esp_display_panel](https://github.com/esp-arduino-libs/ESP32_Display_Panel)
- [LVGL 9.x](https://lvgl.io)
- WiFi (built-in)
- WebServer (built-in)
- Preferences (built-in)

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
1. Open `wifi_config_portal.ino`
2. Upload
3. Connect your phone to the `ESP32-P4-Setup` AP
4. Browse to `192.168.4.1`, enter your WiFi credentials
5. Device saves to NVS and connects as STA

> **Note:** AP mode on CrowPanel is unstable for long-running use. For 24/7 WiFi, see the External ESP8266 (UART1) approach in the book.

## Key Concepts
- WiFi AP + STA mode switching
- WebServer form handling
- NVS (Preferences) for credential persistence
- AP mode instability on CrowPanel SDIO WiFi — documented workaround

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
