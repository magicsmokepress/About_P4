# Chapter 8: BLE Scanner

## Overview
Scans for BLE advertisements and displays discovered devices with RSSI and advertised name in an LVGL list. Uses NimBLE-Arduino for the Bluetooth stack on ESP32-P4.

## Hardware Required
- CrowPanel Advanced 7" (Elecrow ESP32-2432S028R)
- USB-C power cable (2A minimum)
- Arduino IDE 2.x

## Libraries
- [esp_display_panel](https://github.com/esp-arduino-libs/ESP32_Display_Panel)
- [LVGL 9.x](https://lvgl.io)
- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino)

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
1. Install `NimBLE-Arduino` via Library Manager
2. Open `ble_scanner.ino`
3. Upload and tap **Scan**; nearby BLE devices appear in list

## Key Concepts
- NimBLEDevice + NimBLEScan + NimBLEAdvertisedDevice callbacks
- BLE requires ESP32 boards 3.3.x (display flickers on 3.3.x — see book note)
- Classic Bluetooth is NOT supported on ESP32-P4 (documented in Ch27)

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
