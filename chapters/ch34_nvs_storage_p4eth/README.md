# Chapter 34: NVS Persistent Storage (Waveshare ESP32-P4-ETH)

## Overview
Demonstrates NVS (Non-Volatile Storage) key-value persistence across reboots on the ESP32-P4. Includes a software CRC32 checksum to detect corrupted data — necessary because the RISC-V core has no ROM CRC32 helper unlike Xtensa.

## Hardware Required
- Waveshare ESP32-P4-ETH
- USB-C power cable
- Arduino IDE 2.x

## Libraries
- [LovyanGFX](https://github.com/lovyan03/LovyanGFX)
- Preferences (built-in)

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
1. Open `nvs_storage.ino`, upload
2. Serial Monitor shows interactive menu
3. Commands: `s` save, `l` load, `r` reset, `c` corrupt (test CRC), `m` modify
4. Reboot and load — data persists

## Key Concepts
- `Preferences.begin()` namespace management
- NVS wear leveling and partition limits
- CRC32 (IEEE 802.3 polynomial) in software — no ROM shortcut on RISC-V
- Detecting NVS corruption vs missing key

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
