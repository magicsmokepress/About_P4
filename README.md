# Programming the ESP32-P4: Companion Code v.1.0
Companion Arduino sketches for *Programming the ESP32-P4: Practical Projects with Arduino IDE*.

## About the Book

The ESP32-P4 is Espressif's most powerful chip - and its most misunderstood. A dual-core 400 MHz RISC-V processor with 16 or 32 MB of PSRAM (depending on board variant), MIPI-DSI display output, USB 2.0 OTG, and hardware AI acceleration. It has no WiFi. No Bluetooth. No wireless at all. Instead, it delegates all radio work to a companion ESP32-C6 over SDIO - a fundamentally different architecture that breaks every assumption you've built from years of ESP32 development.

This book fills that gap. **36 chapters and 5 appendices** take you from first blink to TensorFlow Lite person detection, covering:

- **Display & UI** - MIPI-DSI initialization, SPI displays (LovyanGFX + ILI9488), LVGL fundamentals, multi-screen dashboards, multitouch
- **Connectivity** - WiFi (STA mode with config portal), BLE scanning, Ethernet (W5500 SPI + native RMII), MQTT
- **Sensors** - I2C scanning, DHT11, BME280, BH1750, MPU6050
- **Peripherals** - SD card, audio output/input, USB host, NTP clock, deep sleep, NVS persistent storage with CRC32
- **Advanced** - FreeRTOS dual-core architecture, camera, WiFi streaming, TFLite inference, FFT signal analysis with ESP-DSP, marble maze game

Every sketch was compiled, uploaded, and tested on real hardware. The code works. When it doesn't (because the P4 ecosystem is young and moving fast), it's documented honestly - including what doesn't work and why.

*Build things. Break things. Learn from both.*

## Repository Structure

### `chapters/`
Book companion sketches organized by chapter. Each folder includes the target board in its name (`_crowpanel` or `_tab5`).

| Chapter | Topic | Board |
|---------|-------|-------|
| ch02 | Development Environment Setup | CrowPanel |
| ch03 | P4 Architecture | CrowPanel |
| ch04 | Hello World Display | CrowPanel |
| ch05 | LVGL Fundamentals | CrowPanel |
| ch06 | LED Control | CrowPanel |
| ch07 | WiFi Scanner | CrowPanel |
| ch08 | BLE Scanner | CrowPanel |
| ch09 | Ethernet (W5500 + RMII) | CrowPanel |
| ch10 | WiFi AP & Web Server | CrowPanel |
| ch11 | MQTT | CrowPanel |
| ch12 | I2C Scanner | CrowPanel |
| ch13 | DHT11 Temperature/Humidity | CrowPanel |
| ch14 | BME280 Environmental Sensor | CrowPanel |
| ch15 | BH1750 Light Sensor | CrowPanel |
| ch16 | MPU6050 Accelerometer/Gyro | CrowPanel |
| ch17 | NTP Clock | CrowPanel |
| ch18 | SD Card | CrowPanel |
| ch19 | Audio Output | CrowPanel |
| ch20 | Audio Input | CrowPanel |
| ch21 | Dashboard | CrowPanel |
| ch22 | Multitouch | CrowPanel |
| ch23 | FreeRTOS Dual-Core | CrowPanel |
| ch24 | Deep Sleep | CrowPanel |
| ch25 | USB Host | CrowPanel |
| ch26 | Camera | Tab5 |
| ch29 | TFLite Person Detection | Tab5 |
| ch30 | WiFi Camera Stream | Tab5 |
| ch31 | Marble Maze | Tab5 |
| ch32 | SPI Displays (LovyanGFX + ILI9488) | P4-ETH |
| ch33 | Native RMII Ethernet (IP101 PHY) | P4-ETH |
| ch34 | NVS Persistent Storage with CRC32 | Any P4 |
| ch35 | FFT Signal Analysis with ESP-DSP | P4-ETH |
| ch36 | AI-Assisted ESP32-P4 Development | Any P4 |

> **Note:** Chapters 1 (Meet the P4), 27 (What Doesn't Work), and 28 (Prototype to Product) are theory and reference chapters with no companion code.

### `extras/`
Standalone reference sketches for the CrowPanel ESP32-P4, Waveshare P4-ETH, and M5Stack Tab5. These are independent projects that complement the book material.

### `skills/`
Claude Code skill for AI-assisted ESP32-P4 development. Install Claude Code, clone this repo, and the skill activates automatically. See Chapter 36.

### `docs/`
Additional reference documentation.

## Hardware

Sketches are tested on:
- **Elecrow CrowPanel Advanced 7" ESP32-P4** (primary, chapters 2-25)
- **M5Stack Tab5** (chapters 26, 29, 30, 31)
- **Waveshare ESP32-P4-ETH** (chapters 32-33, 35)

## Requirements

- Arduino IDE 2.x
- ESP32 Arduino Core 3.2.6+
- ESP32_Display_Panel v1.0.4+ (CrowPanel/Tab5 chapters)
- LovyanGFX (P4-ETH display chapters)
- LVGL v9.2.x (with included `lv_conf.h` - see `docs/lv_conf.h`)
- ESP-DSP library (chapter 35)

## Attribution

This book was born from not wanting to learn a new programming language and needing to build a better mousetrap. Along the way, I consulted the internet many times - forum posts, GitHub issues, library source code, and the occasional blog post written by someone who had already fought the same battle. I have done my best to attribute all third-party code and ideas to their rightful authors. If, despite my efforts, I have copied, adapted, or built upon your work without providing proper attribution, please contact feedback@magicsmokepress.com. I will gladly add the appropriate credit or remove the material - no takedown notice needed, just a friendly email.

## Support

If this code helped you build something, learn something, or saved you from staring at a blank screen for another hour - you can say thanks by grabbing the book or buying me a beer:

- **Buy the book:** [magicsmokepress.gumroad.com/l/yfcnzu](https://magicsmokepress.gumroad.com/l/yfcnzu)
- **Buy me a beer:** [paypal.me/magicsmokepress](https://paypal.me/magicsmokepress)
- **Buy me a coffee:** [buymeacoffee.com/magicsmokepress](https://buymeacoffee.com/magicsmokepress)

Either way, go build something. That's the best thank you.

## 🇭🇷

Za vas par koji ste se našli tu, slučajno ili ne - pretpostavljam da znate engleski da ste pročitali sve gore navedeno. Ako ne, javite mi se pa ću to napisati i na "naški." Ali ozbiljno, ako ste iz Hrvatske, Bosne & Hercegovine, Srbije ili Crne Gore i želite izdanje na naškom možda bude. Razumijemo se - bez obzira dal je ova knjiga lijepa, ljepa, lipa ili lepa. Javite se.

## License

Copyright (c) 2026 Marko Vasilj. The example code in this repository is released under the [MIT License](LICENSE) - you are free to use, modify, and distribute it. The only requirement is that you comply with the licenses of the third-party libraries the sketches depend on (LVGL, LovyanGFX, ESP-DSP, PubSubClient, etc.). See the LICENSE file for details.

Written with AI assistance. All code tested on real hardware by a human.
