# Chapter 11: MQTT

## Overview
Connects the CrowPanel to an MQTT broker, publishes sensor readings on a schedule, and subscribes to a command topic. Demonstrates the publish/subscribe pattern with ArduinoJson payload encoding.

## Hardware Required
- CrowPanel Advanced 7" (Elecrow ESP32-2432S028R)
- USB-C power cable (2A minimum)
- Arduino IDE 2.x
- MQTT broker (Mosquitto on local network or HiveMQ cloud)
- Two USB-C cables (required for stable WiFi)

## Libraries
- [esp_display_panel](https://github.com/esp-arduino-libs/ESP32_Display_Panel)
- [LVGL 9.x](https://lvgl.io)
- [PubSubClient](https://github.com/knolleary/pubsubclient)
- [ArduinoJson](https://arduinojson.org)

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
1. Install `PubSubClient` and `ArduinoJson`
2. Edit `mqtt.ino`: set `WIFI_SSID`, `WIFI_PASS`, `MQTT_BROKER`
3. Upload; Serial Monitor shows connect + publish log
4. Subscribe to `esp32p4/status` with any MQTT client to verify

## Key Concepts
- PubSubClient connect/publish/subscribe loop
- ArduinoJson `JsonDocument` for structured payloads
- MQTT topic architecture (device/type/value)
- Reconnect logic for dropped broker connections

---

## Getting the Book

Full narrative, explanations, wiring diagrams, and troubleshooting are in the book:
- **Gumroad:** [smokemagic.gumroad.com/l/yfcnzu](https://smokemagic.gumroad.com/l/yfcnzu)
- **Amazon Kindle/Paperback:** search *"Programming the ESP32-P4"*

_Part of the [About_P4](https://github.com/magicsmokepress/About_P4) companion repo — Magic Smoke Press_
