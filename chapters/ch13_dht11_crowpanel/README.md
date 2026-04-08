# Chapter 13: DHT11 Temperature & Humidity

## Overview
Reads temperature and humidity from a DHT11 sensor on a single GPIO line and displays live readings on an LVGL gauge and label. A gentle introduction to 1-Wire protocol sensors before moving to I2C sensors in later chapters.

## Hardware Required
- CrowPanel Advanced 7" (Elecrow ESP32-2432S028R)
- USB-C power cable (2A minimum)
- Arduino IDE 2.x
- DHT11 sensor module
- 10kΩ pull-up resistor (if bare sensor, not module)
- Jumper wire to GPIO header

## Libraries
- [esp_display_panel](https://github.com/esp-arduino-libs/ESP32_Display_Panel)
- [LVGL 9.x](https://lvgl.io)
- [DHT sensor library](https://github.com/adafruit/DHT-sensor-library) by Adafruit

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
1. Install `DHT sensor library` and `Adafruit Unified Sensor`
2. Wire DHT11 DATA pin to GPIO (see `#define DHT_PIN` in sketch)
3. Open `dht11.ino`, upload
4. Display shows temperature and humidity, updating every 2s

## Key Concepts
- DHT single-wire protocol timing
- Adafruit DHT library read() return values
- LVGL arc gauge update from sensor callback
- Handling checksum errors gracefully

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
