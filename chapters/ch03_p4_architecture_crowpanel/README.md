# Chapter 3: ESP32-P4 Architecture

## Overview
A deep dive into the ESP32-P4 chip: dual RISC-V cores, 32 MB OPI PSRAM, MIPI-DSI, USB OTG, and the differences from ESP32-S3. This chapter explains why the P4 behaves differently from other ESP32 variants and what that means for your code.

## Hardware Required
- CrowPanel Advanced 7" (Elecrow ESP32-2432S028R)
- USB-C power cable (2A minimum)
- Arduino IDE 2.x

## Libraries
_(Architecture overview — no sketch required)_

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
1. Read this chapter in the book for architecture context
2. Refer back here when debugging peripheral conflicts

## Key Concepts
- Dual RISC-V HP + LP cores
- OPI PSRAM vs QSPI PSRAM
- MIPI-DSI vs parallel RGB displays
- USB OTG and CDC/JTAG modes
- Why RISC-V has no ROM CRC32 helper

---

_Reference chapter — architectural overview only, no runnable sketch._

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
