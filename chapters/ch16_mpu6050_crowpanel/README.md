# Chapter 16: MPU-6050 Accelerometer & Gyroscope

## Overview
Reads 6-axis IMU data (3-axis accelerometer + 3-axis gyroscope) from an MPU-6050 over I2C and visualizes pitch/roll on an LVGL graphical horizon indicator. Also logs raw values to Serial for calibration.

## Hardware Required
- CrowPanel Advanced 7" (Elecrow ESP32-2432S028R)
- USB-C power cable (2A minimum)
- Arduino IDE 2.x
- MPU-6050 module
  - SDA → GPIO 45, SCL → GPIO 46
  - VCC → 3V3, GND → GND, AD0 → GND (address 0x68)

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
1. Wire MPU-6050 to I2C1 header
2. Open `mpu6050.ino`, upload
3. Tilt the board — pitch and roll update on the display horizon

## Key Concepts
- MPU-6050 register map (ACCEL_XOUT, GYRO_XOUT)
- Wake-up sequence (clear sleep bit in PWR_MGMT_1)
- Accelerometer → pitch/roll via atan2
- Complementary filter to blend gyro + accel data

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
