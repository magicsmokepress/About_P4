# Chapter 2: Development Environment Setup

## Overview
This chapter walks through setting up Arduino IDE 2.x for ESP32-P4 development. It covers installing the ESP32 boards package, configuring board settings for the CrowPanel, and verifying your toolchain with a minimal sketch.

## Hardware Required
- CrowPanel Advanced 7" (Elecrow ESP32-2432S028R)
- USB-C power cable (2A minimum)
- Arduino IDE 2.x

## Libraries
- Arduino IDE 2.x
- ESP32 Arduino core 3.x
- esp_display_panel

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
1. Install ESP32 boards package in Arduino IDE
2. Set board to `ESP32P4 Dev Module`
3. Configure PSRAM and partition settings (see Board Settings below)
4. Compile and upload the hello sketch

## Key Concepts
- Arduino IDE board manager setup for ESP32-P4
- Critical board settings (OPI PSRAM, CDC/JTAG, partition scheme)
- First upload verification

---

_Reference chapter — see manuscript for setup walkthrough. No runnable sketch required._

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
