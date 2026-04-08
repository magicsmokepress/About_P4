# Chapter 9: Ethernet (W5500 SPI + RMII/IP101)

## Overview
Full Ethernet chapter covering both the W5500 SPI module (reliable, external) and the onboard RMII/IP101 PHY on boards that include it. Establishes a TCP connection and fetches a page to verify link-layer connectivity.

## Hardware Required
- CrowPanel Advanced 7" (Elecrow ESP32-2432S028R)
- USB-C power cable (2A minimum)
- Arduino IDE 2.x
- W5500 SPI Ethernet module (connected to VSPI header)
  OR board with RMII/IP101 PHY

## Libraries
- [esp_display_panel](https://github.com/esp-arduino-libs/ESP32_Display_Panel)
- [LVGL 9.x](https://lvgl.io)
- Ethernet (built-in) or ETH.h

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
1. Wire W5500 to SPI header (MOSI/MISO/SCK/CS + INT)
2. Open `ethernet.ino`
3. Upload; Serial Monitor shows IP address on DHCP success
4. Display shows connection status

## Key Concepts
- W5500 SPI vs RMII hardware differences
- ETH.begin() with custom PHY configuration
- Fallback to DHCP with static IP option
- Distinguishing network errors from PHY init failures

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
