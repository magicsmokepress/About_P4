# Chapter 18: SD Card

## Overview
Mounts a FAT32 SD card over SPI, reads a text file, writes a log entry, and lists the root directory. All operations are shown on the LVGL display with a file browser widget.

## Hardware Required
- CrowPanel Advanced 7" (Elecrow ESP32-2432S028R)
- USB-C power cable (2A minimum)
- Arduino IDE 2.x
- MicroSD card module (SPI)
  - Connect to SPI header or GPIO header
- FAT32 formatted MicroSD card

## Libraries
- [esp_display_panel](https://github.com/esp-arduino-libs/ESP32_Display_Panel)
- [LVGL 9.x](https://lvgl.io)
- SD (built-in to ESP32 Arduino core)

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
1. Format SD card as FAT32
2. Wire SD module (SPI) — see `#define SD_CS`, `SD_MOSI`, `SD_MISO`, `SD_SCK` in sketch
3. Open `sdcard.ino`, upload
4. Display shows directory listing; tap a file to read first 256 bytes

## Key Concepts
- SD.begin(CS_PIN) with custom SPI pins
- File.read() vs File.readString()
- LVGL list populated from SD directory scan
- SPI bus sharing with other peripherals

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
