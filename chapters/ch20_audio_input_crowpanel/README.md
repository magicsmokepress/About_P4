# Chapter 20: Audio Input (I2S Microphone)

## Overview
Captures audio from an INMP441 MEMS microphone over I2S and displays a live VU meter and waveform on the LVGL display. Also logs peak amplitude to Serial for threshold calibration.

## Hardware Required
- CrowPanel Advanced 7" (Elecrow ESP32-2432S028R)
- USB-C power cable (2A minimum)
- Arduino IDE 2.x
- INMP441 I2S microphone module
  - SCK, WS, SD → GPIO header
  - L/R → GND (left channel select)

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
1. Wire INMP441 to I2S GPIO pins (see `#define MIC_SCK`, etc.)
2. Open `audio_input.ino`, upload
3. Speak near mic; VU meter and waveform respond on display

## Key Concepts
- I2S RX mode for PDM microphone
- 32-bit samples → 24-bit left-justified conversion
- DMA buffer drain in loop()
- Peak detection and dB approximation

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
