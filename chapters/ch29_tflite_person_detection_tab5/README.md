# Chapter 29: TFLite Person Detection (Tab5)

## Overview
Runs a TensorFlow Lite MobileNet person-detection model on the ESP32-P4 using the ESP-NN accelerated inference engine. Live camera feed is passed to the model; confidence score and bounding box are overlaid on the LVGL display.

## Hardware Required
- M5Stack Tab5 (ESP32-P4 + SC202CS camera)
- USB-C power cable
- Arduino IDE 2.x

## Libraries
- [esp_display_panel](https://github.com/esp-arduino-libs/ESP32_Display_Panel)
- [LVGL 9.x](https://lvgl.io)
- TensorFlowLite_ESP32
- esp_camera (built-in)

## Board Settings
```
Board:         M5Stack Tab5 (or ESP32P4 Dev Module)
Arduino Core:  esp32 boards 3.2.6
USB Mode:      Hardware CDC and JTAG
PSRAM:         OPI PSRAM
```

## Boards Tested
- ✅ M5Stack Tab5

## How to Use
1. Install `TensorFlowLite_ESP32` via Library Manager
2. Open `tflite_person_detection.ino`, upload
3. Point camera at a person; confidence score appears on screen
4. Serial Monitor logs inference time (typically 150–300 ms)

## Key Concepts
- TFLite Micro interpreter on ESP32-P4
- ESP-NN SIMD acceleration vs pure C fallback
- Model quantization (INT8 vs FLOAT32)
- Camera frame → 96×96 grayscale preprocessing
- Balancing inference frequency with UI refresh

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
