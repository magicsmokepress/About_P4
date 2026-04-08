# Chapter 19: Audio Output (DAC / I2S)

## Overview
Plays a sine wave tone and a WAV file from PSRAM via I2S audio output. Demonstrates the ESP32-P4 I2S peripheral with an external DAC or MAX98357A amplifier module.

## Hardware Required
- CrowPanel Advanced 7" (Elecrow ESP32-2432S028R)
- USB-C power cable (2A minimum)
- Arduino IDE 2.x
- MAX98357A I2S amplifier module (or similar)
  - BCLK, LRCLK, DIN → GPIO header
- Small speaker (4Ω or 8Ω)

## Libraries
- [esp_display_panel](https://github.com/esp-arduino-libs/ESP32_Display_Panel)
- [LVGL 9.x](https://lvgl.io)
- ESP32-AudioI2S or driver/i2s_std.h (built-in)

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
1. Wire MAX98357A to I2S GPIO pins (see `#define I2S_BCLK`, etc.)
2. Open `audio_output.ino`, upload
3. Tap tone buttons on screen; speaker plays tones
4. Serial Monitor shows I2S write byte counts

## Key Concepts
- I2S standard mode (PHILIPS format)
- Sine wave generation in PSRAM buffer
- i2s_channel_write() with DMA
- Sample rate, bit depth, channel configuration

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
