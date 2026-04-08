# Chapter 14: BME280/BMP280 Environment Sensor

## Overview
Auto-detects BME280 (temperature + humidity + pressure) or BMP280 (temperature + pressure only) on the I2C1 header and adapts the UI accordingly. Uses the legacy `driver/i2c.h` API to share the bus safely with the GT911 touch controller.

## Hardware Required
- CrowPanel Advanced 7" (Elecrow ESP32-2432S028R)
- USB-C power cable (2A minimum)
- Arduino IDE 2.x
- BME280 or BMP280 module
  - SDA → GPIO 45 (I2C1 header)
  - SCL → GPIO 46 (I2C1 header)
  - VCC → 3V3, GND → GND

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
1. Wire BME/BMP280 to I2C1 header (GPIO 45/46)
2. Open `bme280.ino`, upload
3. Display shows temperature, humidity (BME280 only), pressure, and altitude

## Key Concepts
- BME280 vs BMP280 auto-detection via chip ID register 0xD0
- Legacy `driver/i2c.h` API — prevents `driver_ng CONFLICT` with BusI2C
- BME280 compensation formulas (Bosch datasheet Appendix)
- Derived altitude from pressure using barometric formula

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
