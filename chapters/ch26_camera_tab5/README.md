# Chapter 26: Camera (Tab5 SC202CS)

## Overview
Staged bring-up of the Tab5's onboard SC202CS camera over MIPI-CSI, exactly as the book chapter walks it:

1. **`camera.ino`** - step 0: boot sanity check. Proves the board, core version, and display come up before any camera code is involved ("Tab5 ready!" on screen).
2. **`ch26_camera_sketch2.ino`** - the full pipeline: SCCB sensor init over Wire, 1-lane CSI capture at 1280x720 RAW8, ISP in RAW8 passthrough, software Bayer demosaic to RGB565, live view via M5GFX.
3. **`ch26_camera_sketch3.ino`** - adds the auto-exposure loop (center-weighted metering every 60 frames).

The complete, fully annotated viewfinder lives in [`extras/Tab5_Camera_Viewfinder/`](../../extras/Tab5_Camera_Viewfinder/) together with the diagnostic sketches used to find the camera in the first place (`Tab5_I2C_BruteForce.ino`, `Tab5_Minimal_Test.ino`).

## Hardware Required
- M5Stack Tab5 (ESP32-P4 + SC202CS camera)
- USB-C power cable
- Arduino IDE 2.x

## Libraries
- M5Unified + M5GFX (M5Stack)
- Camera path uses ESP-IDF drivers built into the core: `esp_cam_ctlr` (CSI), `driver/isp`, `esp_ldo_regulator`
- **Not used:** esp_camera (no P4 support), LVGL (display goes through M5GFX here)

## Board Settings
```
Board:         M5Stack Tab5 (M5Stack board manager)
Boards:        M5Stack 3.2.6
PSRAM:         OPI PSRAM
```

## Boards Tested
- ✅ M5Stack Tab5

## Key Concepts
- SC202CS (marketed SC2356): SCCB address 0x36 on SDA=7 / SCL=8 - **not** the internal bus
- 1-lane MIPI CSI at 576 Mbps (2-lane produces zero frames)
- IO expanders on internal bus (31/32) control camera reset - **never call `Wire.end()`**, it de-powers them and the frame buffer fills with constant 0x10
- LDO3 at 2.5V powers the CSI PHY
- ISP RAW8 passthrough + software Bayer demosaic (hardware demosaic is ESP-IDF-only)
- Exposure registers must be written before stream-on


## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
