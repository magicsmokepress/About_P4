# Chapter 25: USB Host

## Overview
Uses the ESP32-P4 USB OTG peripheral in host mode to enumerate a connected USB HID keyboard or mouse. Displays keystrokes/button events on the LVGL screen in real time.

## Hardware Required
- CrowPanel Advanced 7" (Elecrow ESP32-2432S028R)
- USB-C power cable (2A minimum)
- Arduino IDE 2.x
- USB-A OTG adapter (USB-C OTG to USB-A)
- USB HID keyboard or mouse

## Libraries
- [esp_display_panel](https://github.com/esp-arduino-libs/ESP32_Display_Panel)
- [LVGL 9.x](https://lvgl.io)
- USB (built-in to ESP32 Arduino core — host mode)

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
1. Connect OTG adapter + USB keyboard to CrowPanel USB-C port
2. Open `usb_host.ino`, upload via **separate** USB-C cable
3. Type on keyboard; keystrokes appear on display

> **Scope note:** USB Host works for HID class. No TT hub, Ethernet, or WiFi USB adapter support — see book Ch25 for details.

## Key Concepts
- USB OTG host mode on ESP32-P4
- HID keyboard report parsing (boot protocol)
- USB device enumeration flow
- Limitations: no USB-Ethernet, no USB-WiFi driver support

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
