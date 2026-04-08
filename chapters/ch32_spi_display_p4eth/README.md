# Chapter 32: SPI Display (Waveshare ESP32-P4-ETH + ILI9488)

## Overview
Initializes the ILI9488 3.5" SPI TFT display on the Waveshare ESP32-P4-ETH board using LovyanGFX. Demonstrates filled shapes, text rendering, color palette sweeps, and a bouncing dot with FPS counter.

## Hardware Required
- Waveshare ESP32-P4-ETH
- USB-C power cable
- Arduino IDE 2.x

## Libraries
- [LovyanGFX](https://github.com/lovyan03/LovyanGFX)

## Board Settings
```
Board:      ESP32P4 Dev Module
USB Mode:   Hardware CDC and JTAG
PSRAM:      OPI PSRAM
Flash Mode: QIO 80MHz
```

## Boards Tested
- ✅ Waveshare ESP32-P4-ETH

## How to Use
1. Install `LovyanGFX` via Library Manager
2. Open `spi_display.ino`, upload
3. Display cycles through shapes, palette, and bouncing dot demo
4. Serial Monitor prints FPS

## Key Concepts
- LovyanGFX LGFX_Device subclass with SPI bus config
- ILI9488 18-bit color (RGB666) vs 16-bit (RGB565)
- Hardware SPI DMA for fast pixel throughput
- Sprite double-buffering to eliminate flicker

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
