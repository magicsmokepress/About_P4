# Chapter 17: NTP Clock

## Overview
Connects to WiFi, synchronizes time with an NTP server using `configTime()`, and displays a live digital and analog clock on the LVGL screen. Handles time zone offsets and DST.

## Hardware Required
- CrowPanel Advanced 7" (Elecrow ESP32-2432S028R)
- USB-C power cable (2A minimum)
- Arduino IDE 2.x
- Two USB-C cables (recommended for stable WiFi)

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
1. Edit `ntp_clock.ino`: set `WIFI_SSID` and `WIFI_PASS`
2. Set `UTC_OFFSET_SEC` for your time zone (e.g. `3600` for UTC+1)
3. Upload; clock syncs within ~5s of WiFi connection

## Key Concepts
- `configTime(gmtOffset, dstOffset, ntpServer)`
- `getLocalTime()` vs `time()` + `localtime_r()`
- LVGL canvas drawing for analog clock hands
- NTP sync retry logic

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
