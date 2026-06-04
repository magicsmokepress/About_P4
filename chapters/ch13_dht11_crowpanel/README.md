# Chapter 13: DHT11 Temperature & Humidity

## Overview
Reads temperature and humidity from a DHT11 sensor on a single GPIO line and displays live readings on an LVGL dashboard with comfort-zone color coding. The twist: the ESP32-P4's 400 MHz clock is too fast for the usual loop-counting DHT code, so this sketch bit-bangs the protocol with the esp_timer hardware timer instead of a sensor library.

## Hardware Required
- CrowPanel Advanced 7" (Elecrow, ESP32-P4)
- USB-C power cable (2A minimum)
- Arduino IDE 2.x
- DHT11 sensor module
- 10kΩ pull-up resistor (if bare sensor, not module)
- Jumper wire to GPIO header

## Libraries
- [esp_display_panel](https://github.com/esp-arduino-libs/ESP32_Display_Panel)
- [LVGL 9.x](https://lvgl.io)

No DHT library required — the single-wire protocol is implemented in the sketch itself (~45 lines), timed with `esp_timer_get_time()` so it works at any CPU clock.

## Board Settings
```
Board:      ESP32P4 Dev Module
USB Mode:   Hardware CDC and JTAG
PSRAM:      OPI PSRAM
Flash Mode: QIO 80MHz
Partition:  Huge APP (3MB No OTA/1MB SPIFFS)
```

## Boards Tested
- ✅ CrowPanel Advanced 7" (Elecrow, ESP32-P4)

## How to Use
1. Wire DHT11 DATA to IO2 (see `#define DHT11_PIN 2` in the sketch), VCC to 3.3V, GND to GND
2. Open `dht11.ino`, upload
3. Display shows temperature and humidity, updating every 2 s; the status line counts reads vs. checksum errors
4. Serial monitor prints the pull-up sanity check at boot (idle line must read HIGH)

## Key Concepts
- DHT single-wire protocol timing (~26 µs HIGH = 0, ~70 µs HIGH = 1)
- Why 400 MHz breaks loop-counted timing, and hardware-timer measurement with `esp_timer_get_time()`
- Checksum verification and graceful error counting
- Pull-up verification at startup

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
