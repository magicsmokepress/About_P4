# Chapter 18: SD Card File Browser

## Overview
A touch-driven file browser for the CrowPanel's **built-in microSD slot**. Tap a directory to open it, tap a file to preview its first bytes, navigate with on-screen BACK/UP/DOWN/OPEN buttons (or serial commands). Uses **SD_MMC in 1-bit mode** - no external module, no SPI wiring.

## Hardware Required
- CrowPanel Advanced 7" (Elecrow, ESP32-P4)
- USB-C power cable (2A minimum)
- MicroSD card, FAT32 formatted (inserted in the onboard slot)
- Arduino IDE 2.x

## Libraries
- [esp_display_panel](https://github.com/esp-arduino-libs/ESP32_Display_Panel)
- [LVGL 9.x](https://lvgl.io)
- SD_MMC (built-in to ESP32 Arduino core)

## Board Settings
```
Board:      ESP32P4 Dev Module
USB Mode:   Hardware CDC and JTAG
PSRAM:      OPI PSRAM
Flash:      16MB
Partition:  Default
```

## Boards Tested
- ✅ CrowPanel Advanced 7" (Elecrow, ESP32-P4)

## How to Use
1. Format the SD card as FAT32 and insert it in the onboard slot
2. Open `sdcard.ino`, upload
3. Browse by touch; serial commands `u`/`d`/`enter`/`b`/`r`/`p`/`q` also work

## Key Concepts
- SD_MMC 1-bit mode on CLK=43, CMD=44, D0=39 (`SD_MMC.setPins`)
- **Init order: SD card first, then display.** Both share LDO4; this sketch was tested with SD-first and the order is part of what was tested
- Directory traversal with `openNextFile()`, text preview, hex dump for binary files
- `ch18_sdcard_fragment1.ino` / `ch18_sdcard_fragment2.ino` are the excerpts printed in the book


## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
