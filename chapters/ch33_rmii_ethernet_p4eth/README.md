# Chapter 33: RMII Ethernet (Waveshare ESP32-P4-ETH + IP101)

## Overview
Brings up the onboard IP101 RMII PHY Ethernet on the Waveshare ESP32-P4-ETH board. Supports DHCP and static IP, with Serial commands to toggle modes and print link status.

## Hardware Required
- Waveshare ESP32-P4-ETH
- USB-C power cable
- Arduino IDE 2.x
- RJ45 Ethernet cable to router/switch

## Libraries
- [LovyanGFX](https://github.com/lovyan03/LovyanGFX)
- ETH (built-in to ESP32 Arduino core)

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
1. Open `rmii_ethernet.ino`
2. For static IP: edit `STATIC_IP`, `GATEWAY`, `SUBNET`
3. Upload and connect Ethernet cable
4. Serial Monitor shows link status and IP
5. Send `dhcp` or `static` via Serial Monitor to toggle modes

## Key Concepts
- `ETH.begin()` with IP101 PHY type and RMII pins
- DHCP vs static IP at runtime
- ETH event callbacks (GOT_IP, DISCONNECTED)
- Ethernet link vs WiFi coexistence (P4-ETH has no WiFi radio)

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
