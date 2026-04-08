# Chapter 24: Deep Sleep & Wake

## Overview
Puts the ESP32-P4 into deep sleep with configurable wake sources (timer, GPIO touch interrupt). Measures and displays current draw before and after sleep, and persists data across sleep cycles using RTC memory.

## Hardware Required
- CrowPanel Advanced 7" (Elecrow ESP32-2432S028R)
- USB-C power cable (2A minimum)
- Arduino IDE 2.x

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
1. Open `deep_sleep.ino`, upload
2. Display shows countdown timer before sleep
3. After `SLEEP_DURATION_SEC`, device wakes and increments boot counter
4. Tap screen to wake early via touch interrupt

## Key Concepts
- `esp_deep_sleep_start()` and wake stub
- `RTC_DATA_ATTR` for RTC-preserved variables
- Timer wake: `esp_sleep_enable_timer_wakeup()`
- GPIO wake: `esp_sleep_enable_ext0_wakeup()`
- Power draw: active vs light-sleep vs deep-sleep comparison

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
