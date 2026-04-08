# Chapter 6: LED Control

## Overview
Controls the CrowPanel's onboard LED via GPIO and PWM. Adds an LVGL slider to adjust brightness in real time, demonstrating the connection between LVGL input events and hardware output.

## Hardware Required
- CrowPanel Advanced 7" (Elecrow ESP32-2432S028R)
- USB-C power cable (2A minimum)
- Arduino IDE 2.x
- (Optional) External LED + 220Ω resistor on GPIO header

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
1. Open `led_control.ino`
2. Upload
3. Use the LVGL slider to dim/brighten the LED

## Key Concepts
- `ledcAttach` / `ledcWrite` for PWM on ESP32-P4
- LVGL slider event → hardware PWM value
- GPIO output vs PWM channel assignment

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
