# Chapter 15: BH1750 Ambient Light Sensor

## Overview
Reads lux values from a BH1750 light sensor over I2C and displays them on an LVGL bar graph that updates in real time. Demonstrates single-byte I2C command protocol without a dedicated library.

## Hardware Required
- CrowPanel Advanced 7" (Elecrow ESP32-2432S028R)
- USB-C power cable (2A minimum)
- Arduino IDE 2.x
- BH1750 (GY-30) module
  - SDA → GPIO 45, SCL → GPIO 46
  - VCC → 3V3, GND → GND, ADDR → GND (address 0x23)

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
1. Wire GY-30 to I2C1 header
2. Open `bh1750.ino`, upload
3. Display shows lux reading with color-coded bar (dim/normal/bright)

## Key Concepts
- BH1750 single-byte command register (0x10 continuous high-res)
- Raw count → lux conversion (raw / 1.2)
- I2C read-without-register-address protocol
- LVGL bar with dynamic color zones

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
