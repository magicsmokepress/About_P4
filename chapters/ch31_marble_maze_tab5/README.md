# Chapter 31: Marble Maze (Tab5)

## Overview
A physics-based marble maze game using the Tab5's built-in IMU as tilt control. The marble rolls in response to board tilt using a simple 2D physics engine, bouncing off LVGL-drawn walls.

## Hardware Required
- M5Stack Tab5 (ESP32-P4 + SC202CS camera)
- USB-C power cable
- Arduino IDE 2.x

## Libraries
- [esp_display_panel](https://github.com/esp-arduino-libs/ESP32_Display_Panel)
- [LVGL 9.x](https://lvgl.io)
- IMU library (built-in M5Stack Tab5 support)

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
1. Open `marble_maze.ino`, upload
2. Tilt the Tab5 to guide the marble through the maze
3. Reach the goal zone to trigger the win animation

## Key Concepts
- IMU accelerometer → tilt angle
- 2D physics: velocity integration, friction, wall collision
- LVGL canvas drawing for dynamic game objects
- Frame-rate-independent physics via delta-time

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
