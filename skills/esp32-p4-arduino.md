---
name: esp32-p4-arduino
description: Board-aware development assistant for ESP32-P4 Arduino projects. Triggers on CrowPanel, Tab5, P4-ETH code. Prevents 50+ known pitfalls, provides pin mappings, enforces library versions.
---

# ESP32-P4 Arduino Development Skill

Complete reference for ESP32-P4 Arduino development across three boards: Elecrow CrowPanel Advanced 7", Waveshare ESP32-P4-ETH, and M5Stack Tab5. Extracted from "Programming the ESP32-P4" by Marko Vasilj.

---

## 1. Trigger Conditions

Activate this skill when ANY of the following appear in user code or conversation:

- `#include <ESP_Panel_Library.h>` or `ESP32_Display_Panel` references
- `WiFi.setPins(` calls
- LovyanGFX P4 config (`LGFX_Device`, `Panel_ILI9488`, `Bus_SPI` with P4 GPIOs)
- `#include <M5Unified.h>` or `M5.begin()` in P4/Tab5 context
- Mentions of **ESP32-P4**, **CrowPanel**, **Tab5**, **P4-ETH**, **EK79007**, **ILI9488**, **ST7123**, **ILI9881C** in Arduino context
- `ESP_PANEL_BOARD_*` defines
- GPIO 54 as output (C6_EN)
- `SD_MMC` usage with MIPI-DSI display
- MIPI-DSI or MIPI-CSI configuration code
- `ETH.h` with IP101 PHY defines
- LVGL v9 code targeting 1024x600 or 480x320 on P4

---

## 2. Board Detection Rules

### CrowPanel Advanced 7"
Identify by ANY of:
- `ESP32_Display_Panel` or `ESP_Panel_Library.h` includes
- `ESP_PANEL_BOARD_*` defines
- GPIO 54 used as OUTPUT (C6_EN for WiFi)
- `SD_MMC` usage alongside MIPI-DSI display
- EK79007 LCD controller references
- 1024x600 resolution with DSI
- `WiFi.setPins(18, 19, 14, ...)`
- NS4168 amplifier or GPIO 30 audio enable

### M5Stack Tab5
Identify by ANY of:
- `#include <M5Unified.h>` or `M5.begin()`
- M5Stack board package references
- M5GFX library usage
- SC202CS / SC2356 camera references
- FT5x06 touch controller
- BMI270 IMU references

### Waveshare P4-ETH
Identify by ANY of:
- LovyanGFX class with ILI9488 panel
- `LGFX_Device` subclass with SPI bus on P4 GPIOs
- `#include <ETH.h>` with `ETH_PHY_IP101`
- Waveshare references in comments
- 480x320 SPI display on P4
- XPT2046 touch controller
- RMII Ethernet configuration

---

## 3. Proactive Code Review Rules

**CHECK EVERY ONE OF THESE on any ESP32-P4 code review. Each represents hours of debugging if missed.**

| # | Check | Why | How to Fix |
|---|-------|-----|------------|
| 1 | **PSRAM set to OPI?** | Most common crash cause. 1024x600x16bit framebuffer = 1.2MB, exceeds 768KB SRAM | Tools > PSRAM > **OPI PSRAM** |
| 2 | **WiFi.setPins() before WiFi.mode()?** | Driver initializes on wrong GPIOs if order is reversed | Move setPins() call above mode() |
| 3 | **3-second delay after WiFi.mode()?** | C6 SDIO bridge needs stabilization time | Add `delay(3000)` after `WiFi.mode()` |
| 4 | **SD card init after display init?** | LDO4 dependency on CrowPanel -- SD GPIOs need display init first | Call display init before SD_MMC.begin() |
| 5 | **lv_conf.h exists with LV_COLOR_DEPTH=16?** | LVGL won't compile without it; wrong depth = garbled colors | Copy template to libraries/ root, set depth to 16 |
| 6 | **Partition scheme = Huge APP?** | Default 1.3MB too small; LVGL projects need 1.5-2.5MB | Tools > Partition > Huge APP (3MB No OTA/1MB SPIFFS) |
| 7 | **delay(2000) at start of setup()?** | USB CDC enumeration delay -- early prints lost | First line of setup() |
| 8 | **No blocking I/O on LVGL core?** | UI freezes during HTTP/MQTT/WiFi operations | Dual-core FreeRTOS: Core 0 = network, Core 1 = LVGL |
| 9 | **No mixing old/new I2C drivers?** | Crash before setup() runs -- `i2c: CONFLICT!` | Use new i2c_master.h API exclusively |
| 10 | **Tab5 on board pkg 3.2.6?** | 3.3.x has MIPI-DSI flicker bug | Pin M5Stack board package to 3.2.6 |

---

## 4. Golden Config -- Known-Good Versions

### Core Libraries

| Component | Version | Notes |
|---|---|---|
| Arduino IDE | 2.3.x | |
| arduino-esp32 (Espressif boards) | 3.3.3+ (tested 3.3.7) | For CrowPanel WiFi projects |
| arduino-esp32 (Espressif boards) | 3.x latest | For non-WiFi CrowPanel projects |
| M5Stack board package | **3.2.6 exactly** | Tab5 only. NOT 3.3.x (flicker bug) |
| ESP-IDF (bundled) | 5.3.x / 5.4.x | Inside arduino-esp32 |
| LVGL | **9.2.x** | **NOT v8** (incompatible API), **NOT v10** (future breaking changes) |
| ESP32_Display_Panel | **1.0.4+** | NOT v0.x (different namespace/API) |
| LovyanGFX | **1.2.19** | P4-ETH only |
| M5Unified | 0.2.13+ | Tab5 only |
| M5GFX | 0.2.19+ | Tab5 only |
| ESP-DSP | **1.7.0** | FFT/signal processing |
| PubSubClient | 2.8+ | MQTT |
| ArduinoJson | 6.21+ | JSON parsing |
| ESP32Ping | latest | RMII Ethernet projects |

### Arduino IDE Board Settings

| Setting | Value |
|---|---|
| Board | ESP32P4 Dev Module |
| PSRAM | **OPI PSRAM** (MANDATORY) |
| Flash Mode | QIO 80MHz |
| Flash Size | 16MB |
| Partition Scheme | **Huge APP (3MB No OTA/1MB SPIFFS)** |
| USB Mode | **Hardware CDC and JTAG** |
| Upload Speed | 921600 |

### LVGL lv_conf.h Critical Settings

| Setting | Value | Default | Why |
|---|---|---|---|
| `#if` guard (line 15) | **1** | 0 | Enable the config file |
| `LV_COLOR_DEPTH` | **16** | 32 | All displays are RGB565 |
| `LV_MEM_CUSTOM` | **1** | 0 | Use malloc/free for PSRAM support |
| `LV_TICK_CUSTOM` | **1** | 0 | Use millis() callback |
| `LV_DEF_REFR_PERIOD` | **16** | 33 | Target 60 FPS |
| `LV_USE_LOG` | **1** | 0 | Enable debug logging |
| `LV_LOG_LEVEL` | `LV_LOG_LEVEL_WARN` | WARN | |
| `LV_FONT_MONTSERRAT_14` | 1 | 1 | Base UI font |
| `LV_FONT_MONTSERRAT_16` | 1 | 0 | Buttons, labels |
| `LV_FONT_MONTSERRAT_20` | 1 | 0 | Section headings |
| `LV_FONT_MONTSERRAT_24` | 1 | 0 | Dashboard values |
| `LV_FONT_MONTSERRAT_28` | 1 | 0 | Clock digits |
| `LV_FONT_MONTSERRAT_36` | 1 | 0 | Large dashboard text |
| `LV_FONT_DEFAULT` | `&lv_font_montserrat_14` | | |
| `LV_DRAW_THREAD_COUNT` | 1 | | Single-threaded best |
| `LV_USE_ANIMATION` | 1 | | |
| `LV_USE_FLEX` | 1 | | |
| `LV_USE_GRID` | 1 | | |

**lv_conf.h location:**
```
Arduino/libraries/lv_conf.h    <-- HERE (sibling to lvgl/)
Arduino/libraries/lvgl/src/    <-- NOT inside here
```

**LVGL tick setup (in code):**
```cpp
lv_tick_set_cb((lv_tick_get_cb_t)millis);
```

---

## 5. Pin Mapping Tables

### 5.1 CrowPanel Advanced 7"

#### Display: EK79007 MIPI-DSI

| Parameter | Value | Notes |
|---|---|---|
| Width | 1024 | |
| Height | 600 | |
| Color Depth | 16 (RGB565) | |
| DPI Clock | 52 MHz | Vendor says 51.2, 52 is stable |
| DSI Lanes | 2 | |
| DSI Lane Rate | 1000 Mbps | |
| DSI PHY LDO | 3 | |
| HPW | 10 | Horizontal Pulse Width |
| HBP | 160 | Horizontal Back Porch |
| HFP | 160 | Horizontal Front Porch |
| VPW | 1 | Vertical Pulse Width |
| VBP | 23 | Vertical Back Porch |
| VFP | 12 | Vertical Front Porch |
| RST Pin | -1 | No hardware reset |
| Backlight Pin | GPIO 31 | PWM, active HIGH |
| Display Library | ESP_Display_Panel 1.0.4+ | NOT LovyanGFX |

#### Touch: GT911 Capacitive

| Parameter | Value |
|---|---|
| SDA | GPIO 45 |
| SCL | GPIO 46 |
| I2C Freq | 400 kHz |
| RST | GPIO 40 |
| INT | GPIO 42 |
| I2C Address | **0x5D** |
| Max Points | 5 |

#### Audio Output: I2S + NS4168

| Parameter | Value | Notes |
|---|---|---|
| BCLK | GPIO 22 | |
| LRCLK | GPIO 21 | |
| DOUT | GPIO 23 | |
| AMP Enable | GPIO 30 | **Active LOW** (unusual -- LOW=on, HIGH=mute) |

#### Audio Input: PDM Microphone

| Parameter | Value |
|---|---|
| PDM CLK | GPIO 24 |
| PDM DIN | GPIO 26 |
| Sample Rate | 16000 Hz |
| Channels | 1 (Mono) |

#### SD Card: SD_MMC 1-bit

| Parameter | Value | Notes |
|---|---|---|
| CLK | GPIO 43 | |
| CMD | GPIO 44 | |
| D0 | GPIO 39 | |
| Mode | 1-bit | |
| **CRITICAL** | Init AFTER display | LDO4 dependency -- SD GPIOs need display init first |

#### WiFi (via ESP32-C6 over SDIO)

| Parameter | Value | Notes |
|---|---|---|
| C6 Enable | GPIO 54 | OUTPUT HIGH, must enable before WiFi.mode() |
| SDIO CLK | GPIO 18 | |
| SDIO CMD | GPIO 19 | |
| SDIO D0 | GPIO 14 | |
| setPins call | `WiFi.setPins(18, 19, 14, -1, -1, -1, -1)` | Before WiFi.mode() |
| Startup Delay | 3000 ms after WiFi.mode() | Mandatory -- SDIO stabilization |
| Stable Modes | STA only | AP mode unstable on V1.0 |
| Power | Two USB cables required | 8-10W with display + WiFi |
| Board Package | Espressif 3.3.3+ (tested 3.3.7) | For WiFi projects |

#### Ethernet: W5500 External (Bit-Bang SPI)

| Parameter | Value | Notes |
|---|---|---|
| SCK | GPIO 4 | Expansion header |
| MOSI | GPIO 3 | Expansion header |
| MISO | GPIO 2 | Expansion header |
| CS | GPIO 5 | Expansion header |
| RST | GPIO 25 | Expansion header |
| Protocol | Bit-bang SPI | Hardware SPI conflicts with display |

#### CrowPanel GPIO Conflict Map

| GPIO | Function | Conflict Risk |
|---|---|---|
| 2, 3, 4, 5 | W5500 Ethernet SPI | Free if no Ethernet module |
| 14, 18, 19 | WiFi SDIO | Dedicated when WiFi active |
| 21, 22, 23 | Audio I2S (LRCLK, BCLK, DOUT) | Cannot share with SPI display |
| 24, 26 | PDM Mic | GPIO 26 conflicts with P4-ETH SCLK |
| 25 | W5500 RST | Free if no Ethernet |
| 30 | Audio AMP enable | Active LOW |
| 31 | Backlight PWM | |
| 39, 43, 44 | SD Card | Init after display (LDO4) |
| 40, 42 | Touch RST/INT | |
| 45, 46 | Touch I2C | |
| 54 | C6 WiFi enable | OUTPUT HIGH before WiFi.mode() |

---

### 5.2 Waveshare ESP32-P4-ETH

#### Display: ILI9488 SPI (External)

| Parameter | Value | Notes |
|---|---|---|
| Width | 480 (landscape) | Physical: 320x480 |
| Height | 320 (landscape) | |
| Color Depth | 16 (RGB565) | |
| SPI Host | SPI2_HOST | SPI0/SPI1 reserved for flash/PSRAM |
| Write Freq | 40 MHz | |
| Read Freq | 16 MHz | |
| SCLK | GPIO 26 | J2-7 |
| MOSI | GPIO 23 | J2-4 |
| MISO | GPIO 27 | J2-8 |
| DC | GPIO 22 | J2-3 |
| CS | GPIO 20 | J2-1 |
| RST | GPIO 21 | J2-2 |
| Backlight | None | Always on via hardware pull-up |
| Display Library | **LovyanGFX 1.2.19** | NOT ESP_Display_Panel |

#### Touch: XPT2046 Resistive SPI (shared bus)

| Parameter | Value | Notes |
|---|---|---|
| CS | GPIO 33 | |
| SPI Freq | 1 MHz | |
| Offset Rotation | 6 | For landscape setRotation(1) |
| SPI Bus | Shared with display | LovyanGFX handles arbitration |
| INT | -1 | No interrupt pin wired |

#### Ethernet: IP101 RMII (On-Board)

| Parameter | Value | Notes |
|---|---|---|
| PHY Type | ETH_PHY_IP101 | |
| PHY Addr | 1 | |
| MDC | GPIO 31 | |
| MDIO | GPIO 52 | |
| Power | GPIO 51 | |
| Clock Mode | EMAC_CLK_EXT_IN | NOT ETH_CLOCK_GPIO0_IN |
| **CRITICAL** | `#define`s BEFORE `#include <ETH.h>` | Preprocessor reads at compile time |

#### P4-ETH GPIO Conflict Map

| GPIO | Function | Conflict Risk |
|---|---|---|
| 20, 21, 22, 23, 26, 27 | SPI Display | Dedicated |
| 31, 52 | Ethernet RMII MDC/MDIO | Cannot reuse |
| 33 | Touch CS | |
| 51 | Ethernet PHY power | |

#### No Audio, SD Card, Camera, or WiFi on this board.

---

### 5.3 M5Stack Tab5

#### Display: ST7123 (V2) / ILI9881C (V1) MIPI-DSI

| Parameter | Value | Notes |
|---|---|---|
| Width | 1024 | |
| Height | 600 | |
| Color Depth | 16 (RGB565) | |
| Init | `M5.begin()` handles everything | No manual pin config needed |
| Display Library | **M5GFX 0.2.19+** | NOT ESP_Display_Panel, NOT LovyanGFX |
| **CRITICAL** | M5Stack board pkg **3.2.6** | 3.3.x has MIPI-DSI flicker bug |

#### Touch: Built-In Capacitive (FT5x06)

| Parameter | Value | Notes |
|---|---|---|
| I2C Addr | 0x55 | On internal bus |
| Management | M5Unified | No direct pin config needed |

#### Camera: SC202CS via MIPI-CSI

| Parameter | Value | Notes |
|---|---|---|
| SCCB SDA | GPIO 31 (actual) | Docs say GPIO 7 -- **docs are wrong** |
| SCCB SCL | GPIO 32 (actual) | Docs say GPIO 8 -- **docs are wrong** |
| I2C Addr | 0x36 | |
| XCLK Pin | GPIO 36 | 24 MHz via LEDC |
| XCLK Freq | 24 MHz | |
| CSI Lanes | **1 only** | 2-lane produces zero frames |
| CSI Lane Rate | 400 Mbps | |
| Chip ID | 0xEB52 | Marketed as "SC2356" -- different name, same chip |
| Max Resolution | 1280x720 | |
| Bayer Pattern | BGGR | |
| LDO for CSI PHY | LDO3, 2500 mV | |
| ISP Mode | RAW8 passthrough | HW demosaic non-functional in Arduino |
| Display Output | 640x360 (half res) | ~16 FPS; full res = 1.3 FPS |

**Camera Init Gotchas:**
- `Wire.end()` kills the camera (IO expanders lose state, CAM_RST drops)
- Exposure regs (0x3e01, 0x3e09) must be written BEFORE stream-on (0x0100=0x01)
- Analog gain above 0x10 whites out the image
- Auto-expose: run every ~60 frames, not every frame (I2C contention)
- ISP hardware demosaic: `esp_isp_demosaic_enable()` returns ESP_OK but does nothing

#### Internal I2C Bus (GPIO 31/32)

| I2C Address | Device |
|---|---|
| 0x28 | Power Monitor |
| 0x32 | RTC |
| 0x36 | Camera (SC202CS SCCB) |
| 0x40 | Audio Codec |
| 0x43 | IO Expander 1 (controls CAM_RST pin 6) |
| 0x44 | IO Expander 2 |
| 0x55 | Touch Controller (FT5x06) |
| 0x68 | IMU (BMI270) |

**WARNING:** Do NOT call `Wire.end()` -- it will kill all 8 devices on this bus, including the camera.

#### Tab5 GPIO Conflict Map

| GPIO | Function | Conflict Risk |
|---|---|---|
| 7, 8 | Documented as camera I2C | Actually unused per bus scan |
| 31, 32 | Internal I2C bus | Shared by 8 devices -- do not call Wire.end() |
| 36 | Camera XCLK | LEDC output |

---

### 5.4 ESP32-P4 Power Rails (All Boards)

| LDO | Voltage | Powers | Notes |
|---|---|---|---|
| LDO3 | 2.5V | Camera/MIPI-CSI PHY, some I/O | Must init in software |
| LDO4 | 3.3V | Display, SD card, external sensors | Must init in software; init order matters |

---

## 6. All Gotchas

### 6.1 WiFi / Networking (9 issues)

#### G-W1: SDIO C6 Enable Sequence (CrowPanel)
- **Symptom:** `H_SDIO_DRV: sdio_get_tx_buffer_num: err: 265` in a loop
- **Cause:** GPIO54 (C6_EN) not driven HIGH before SDIO communication; and/or `WiFi.setPins()` missing or called after `WiFi.mode()`
- **Fix:** Exact sequence required before any WiFi call:
  ```cpp
  pinMode(54, OUTPUT);
  digitalWrite(54, HIGH);
  delay(500);
  WiFi.setPins(18, 19, 14, -1, -1, -1, -1);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(3000);
  ```
- **Boards:** CrowPanel (all WiFi projects)

#### G-W2: Mandatory 3-Second Delay After WiFi.mode()
- **Symptom:** WiFi.begin() returns WL_DISCONNECTED immediately or drops after 1 second
- **Cause:** C6 co-processor SDIO bridge needs time to initialize
- **Fix:** `delay(3000)` after `WiFi.mode(WIFI_STA)` -- non-negotiable
- **Boards:** All P4 boards with C6 co-processor

#### G-W3: WiFi.setPins() MUST Come Before WiFi.mode()
- **Symptom:** SDIO errors, WiFi never connects
- **Cause:** Driver initializes on wrong GPIO pins if setPins is called after mode
- **Fix:** Always call `WiFi.setPins()` before `WiFi.mode()`
- **Boards:** CrowPanel, any board needing setPins

#### G-W4: SoftAP Mode Unstable
- **Symptom:** AP mode drops connections, unreliable
- **Cause:** SDIO-proxied radio stack instability in AP mode
- **Fix:** Use STA mode. For captive portals, consider external ESP8266 on UART1
- **Boards:** CrowPanel

#### G-W5: Avoid WiFi.RSSI() Polling
- **Symptom:** SDIO communication issues, eventual WiFi failure
- **Cause:** Each WiFi query generates an RPC over SDIO to the C6; frequent polling destabilizes the link
- **Fix:** Use `WiFi.status()` (cached locally, no C6 contact). Read RSSI/IP once after connection, not on a timer
- **Boards:** CrowPanel

#### G-W6: Reset Button Only Resets P4, Not C6
- **Symptom:** WiFi stops responding, reset button doesn't fix it
- **Cause:** Boot/reset button only resets the P4 main CPU, not the C6 co-processor
- **Fix:** Full power cycle: unplug USB, wait 5 seconds, reconnect
- **Boards:** CrowPanel

#### G-W7: Long-Running WiFi Connections May Fail
- **Symptom:** WiFi drops after extended operation
- **Cause:** SDIO bridge reliability over long sessions
- **Fix:** Periodic power cycle, or use external ESP8266 on UART1 for always-on WiFi
- **Boards:** CrowPanel

#### G-W8: sdmmc_init_ocr Error
- **Symptom:** WiFi init fails with sdmmc_init_ocr error
- **Cause:** Insufficient power -- display + WiFi draws significant current
- **Fix:** Use quality USB cable rated for data + power, at least 500mA port, avoid USB hubs. Two USB cables recommended for CrowPanel WiFi.
- **Boards:** CrowPanel

#### G-W9: ESP-NOW Permanently Broken
- **Symptom:** Link-time error with missing symbols like `esp_now_init`
- **Cause:** ESP-NOW requires low-level MAC-layer access that the SDIO-proxied radio stack doesn't expose. This is a permanent, fundamental limitation.
- **Fix:** Use MQTT over WiFi, raw TCP/UDP sockets, or BLE instead. No workaround exists.
- **Boards:** All P4 boards

---

### 6.2 Display / MIPI-DSI (8 issues)

#### G-D1: PSRAM Must Be OPI PSRAM
- **Symptom:** `LoadProhibited` or `StoreProhibited` crash at runtime; Guru Meditation Error during display init
- **Cause:** Framebuffer for 1024x600 16-bit = 1.2MB, exceeds 768KB internal SRAM. Without PSRAM, allocation fails silently, null pointer write crashes.
- **Fix:** Tools > PSRAM > **"OPI PSRAM"** -- not Disabled, not QSPI. **Single most common crash cause.**
- **Boards:** All P4 boards

#### G-D2: Screen Stays Black -- LDO3 Not Initialized
- **Symptom:** Display init reports success but screen stays dark
- **Cause:** LDO3 (2.5V rail for MIPI-DSI PHY) was never initialized. Silent failure -- no error message.
- **Fix:** Ensure LDO3 is explicitly initialized before MIPI-DSI setup
- **Boards:** CrowPanel, boards with internal PMIC

#### G-D3: Garbled Colors / Shifted Image
- **Symptom:** Vertical bars, shifted image, wrong colors on screen
- **Cause:** MIPI-DSI timing parameters don't match panel datasheet
- **Fix:** Verify every timing parameter: HPW, HBP, HFP, VPW, VBP, VFP. Even one wrong value produces artifacts. Reduce LCD_DSI_LANE_RATE to 800 if screen stays black.
- **Boards:** All MIPI-DSI boards

#### G-D4: Tab5 Display Flicker on M5Stack Boards 3.3.x
- **Symptom:** Display strobes/flickers visibly
- **Cause:** M5Stack board package 3.3.0-3.3.2+ has DSI initialization regression
- **Fix:** Pin M5Stack board package to **3.2.6**. It supports WiFi.setPins(), camera, display, all features except BLE. No reason to use 3.3.x unless you need BLE.
- **Boards:** M5Stack Tab5 only

#### G-D5: LV_COLOR_DEPTH Mismatch
- **Symptom:** Garbled or inverted colors
- **Cause:** LV_COLOR_DEPTH set to 32 but display expects RGB565 (16-bit)
- **Fix:** Set `LV_COLOR_DEPTH` to **16** for all displays
- **Boards:** All

#### G-D6: Backlight Won't Turn On
- **Symptom:** Display initialized but no visible output
- **Cause:** Backlight enable pin not set in code
- **Fix:** Check board_config.h for backlight GPIO and enable it. CrowPanel = GPIO 31, active HIGH.
- **Boards:** All

#### G-D7: CrowPanel vs. Waveshare 7B Code NOT Compatible
- **Symptom:** CrowPanel code compiles for Waveshare 7B but produces blank screen or hang
- **Cause:** Different LCD controllers (EK79007 vs JD9365), different timing, Waveshare 7B needs PMIC enabled via I2C before panel responds to DSI commands
- **Fix:** Use board-specific code. As of March 2026, ESP32_Display_Panel Arduino library does not support Waveshare 7B. Use ESP-IDF examples.
- **Boards:** Waveshare ESP32-P4-WiFi6-Touch-LCD-7B / KLAYERS

#### G-D8: Hardware SPI Conflicts with MIPI-DSI
- **Symptom:** Crash when calling SPI.begin() with display active
- **Cause:** SPI.begin() conflicts with MIPI-DSI display driver's DMA channel usage on some boards
- **Fix:** Use bit-bang SPI or I2C for external peripherals instead
- **Boards:** Board-dependent (CrowPanel affected)

---

### 6.3 LVGL (5 issues)

#### G-L1: lv_conf.h Not Found
- **Symptom:** LVGL won't compile
- **Cause:** LVGL requires manually created config header
- **Fix:**
  1. Copy `lv_conf_template.h` from lvgl/ folder to **parent** libraries/ folder (sibling to lvgl/, NOT inside it)
  2. Rename to `lv_conf.h`
  3. Change first `#if 0` to `#if 1`
  4. Set `LV_COLOR_DEPTH` to 16
- **Boards:** All

#### G-L2: Missing Montserrat Font
- **Symptom:** `undefined reference to lv_font_montserrat_XX`
- **Cause:** Font not enabled in lv_conf.h
- **Fix:** Set `LV_FONT_MONTSERRAT_XX` to 1 for each size you use. **Clean build required** (Arduino caches library objects).
- **Boards:** All

#### G-L3: LVGL v8 Code Doesn't Work on v9
- **Symptom:** Missing functions: `lv_disp_draw_buf_init()`, `lv_disp_drv_init()`, `lv_indev_drv_register()` etc.
- **Cause:** LVGL v9 was a complete API rewrite with zero backward compatibility
- **Fix:** Use LVGL v9 API exclusively. Do not copy v8 examples from the internet. Functions like `lv_disp_draw_buf_init` do not exist in v9.
- **Boards:** All

#### G-L4: UI Freezes During Network Operations
- **Symptom:** Display freezes for seconds during HTTP requests, MQTT, etc.
- **Cause:** Blocking I/O halts the CPU; LVGL can't refresh on the same core
- **Fix:** Dual-core architecture: Core 0 = network I/O, Core 1 = LVGL rendering, FreeRTOS queues between them. Never do blocking I/O on the LVGL core.
- **Boards:** All

#### G-L5: LVGL Crashes in Multi-Task Code
- **Symptom:** Random Guru Meditation errors, display corruption in FreeRTOS projects
- **Cause:** LVGL is NOT thread-safe. Calling LVGL functions from ISR or different core causes race conditions.
- **Fix:** Only Core 1 touches LVGL. Core 0 sends data via FreeRTOS queue; Core 1 reads queue and updates UI.
- **Boards:** All

---

### 6.4 Build / Flash / IDE (7 issues)

#### G-B1: Sketch Too Large
- **Symptom:** `Sketch too large, maximum is 1310720 bytes`
- **Cause:** Default partition scheme = 1.3MB; LVGL projects need 1.5-2.5MB
- **Fix:** Tools > Partition Scheme > **"Huge APP (3MB No OTA/1MB SPIFFS)"**
- **Boards:** All

#### G-B2: Failed to Connect During Upload
- **Symptom:** "Failed to connect to ESP32-P4" during upload
- **Cause:** Charge-only USB cable (no data), bootloader not in download mode, or upload baud too high
- **Fix:**
  1. Replace cable with known data-capable USB cable
  2. Hold BOOT button while clicking Upload
  3. Reduce upload speed to 460800 or 230400
- **Boards:** All

#### G-B3: No Serial Output After Upload
- **Symptom:** Serial Monitor blank after upload
- **Cause:** USB CDC enumeration takes time; early Serial.println() fires before host recognizes port
- **Fix:** Add `delay(2000)` as very first line in setup(). Verify USB Mode = "Hardware CDC and JTAG"
- **Boards:** All

#### G-B4: Garbled Serial Output
- **Symptom:** Random characters in Serial Monitor
- **Cause:** Baud rate mismatch or wrong USB Mode. P4 uses USB CDC, not UART-to-USB bridge.
- **Fix:** Serial Monitor = **115200 baud**, USB Mode = **"Hardware CDC and JTAG"**
- **Boards:** All

#### G-B5: First Compile Takes 3-5 Minutes
- **Symptom:** Very slow first build
- **Cause:** Normal -- ESP-IDF compiles from source on first build
- **Fix:** Wait. Subsequent builds use cache (10-30 seconds for incremental changes).
- **Boards:** All

#### G-B6: ESP32_Display_Panel Namespace Error
- **Symptom:** `expected type-specifier before 'BacklightPWM_LEDC'`
- **Cause:** Outdated ESP32_Display_Panel version; namespace changed in v1.0.4
- **Fix:** Update to **v1.0.4+** via Library Manager. Add `using namespace esp_panel::drivers;` after includes.
- **Boards:** All

#### G-B7: Arduino Clean Build Required After lv_conf.h Changes
- **Symptom:** Changes to lv_conf.h have no effect
- **Cause:** Arduino IDE caches library objects
- **Fix:** Sketch > Clean Build Folder, or delete the build directory manually
- **Boards:** All

---

### 6.5 I2C / Sensors (4 issues)

#### G-I1: I2C Driver Conflict (Legacy vs. New)
- **Symptom:** Crash: `i2c: CONFLICT! driver_ng is not allowed to be used with this old driver`
- **Cause:** ESP-IDF v5.x has two mutually exclusive I2C drivers. Arduino Wire uses the new driver (i2c_master.h). Third-party libraries calling `i2c_driver_install` (legacy) on same port crash. Can crash before setup() runs -- during C++ global constructors.
- **Fix:** Use `ets_printf("[DIAG] setup() entry\n")` to confirm if you reach setup(). Identify library calling legacy API. Replace with new I2C master API. Do not mix old and new drivers on same port.
- **Boards:** All P4 boards

#### G-I2: I2C Scanner Finds Nothing When Display Is Initialized
- **Symptom:** I2C scanner sketch returns no devices after display init
- **Cause:** Display initialization claims the I2C bus and changes its configuration
- **Fix:** Run I2C scanner WITHOUT display initialization to verify wiring. Then integrate sensor code into full project.
- **Boards:** All

#### G-I3: GT911 Touch Address Confusion
- **Symptom:** Touch not responding; wrong I2C address
- **Cause:** GT911 selects address based on INT pin state at boot: LOW = 0x5D, HIGH = 0x14
- **Fix:** CrowPanel uses **0x5D**. Check your board's hardware design for INT pin pull.
- **Boards:** CrowPanel (0x5D), others may vary

#### G-I4: XPT2046 Touch Axes Mirrored/Rotated
- **Symptom:** Touch coordinates don't match display position
- **Cause:** Touch controller coordinate system doesn't match display orientation
- **Fix:** Set `offset_rotation = 6` in LovyanGFX touch config for landscape. Try values 0-7 for other boards.
- **Boards:** Waveshare P4-ETH with XPT2046

---

### 6.6 SD Card (1 issue)

#### G-S1: SD Card Must Initialize AFTER Display
- **Symptom:** SD card mount fails silently, or initializing SD corrupts display
- **Cause:** Display and SD card share LDO4 on CrowPanel. MIPI-DSI driver configures LDO4 during display init. SD_MMC.begin() before display init = wrong voltage levels.
- **Fix:** Always: `init_display()` FIRST, then `init_sd_card()`. Never reverse. Single most common CrowPanel forum issue.
- **Boards:** CrowPanel

---

### 6.7 Audio (1 issue)

#### G-A1: NS4168 Amplifier Enable Pin is Active LOW
- **Symptom:** No audio output despite correct I2S setup
- **Cause:** CrowPanel's NS4168 amp enable (GPIO 30) is **active LOW** -- pull LOW to enable, HIGH to mute. Most amps are active HIGH.
- **Fix:** `digitalWrite(30, LOW)` to enable audio
- **Boards:** CrowPanel

---

### 6.8 Bluetooth (2 issues)

#### G-BT1: Classic Bluetooth (BR/EDR) Will NEVER Work
- **Symptom:** A2DP, SPP, HFP, AVRCP all fail
- **Cause:** Hardware impossibility -- ESP32-C6 has no Classic BT radio. Only the original ESP32 (2016) supports BR/EDR. This is permanent.
- **Fix:** No fix possible. Workarounds:
  1. ESP32 module as UART bridge (~$4) for A2DP sink
  2. Standalone BT audio module (KCX_BT, MH-M18, ~$3-5)
  3. WiFi audio streaming (no extra hardware)
  4. BLE audio (LC3 codec, experimental)
- **Boards:** All P4 boards

#### G-BT2: BLE on Tab5 Requires 3.3.x (But 3.3.x Has Display Flicker)
- **Symptom:** BLEDevice.h won't compile on M5Stack boards 3.2.6
- **Cause:** Boards 3.2.6 doesn't enable `CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE`
- **Fix:** Cannot have both BLE and flicker-free display on Tab5 currently. Choose:
  1. Use 3.3.x for BLE, accept display flicker
  2. Stay on 3.2.6 for display + WiFi, skip BLE
  3. Use WiFi instead of BLE
- **Boards:** M5Stack Tab5 only

---

### 6.9 Camera (3 issues)

#### G-C1: Camera Not Available in Arduino (Generic)
- **Symptom:** No MIPI-CSI camera support in arduino-esp32
- **Cause:** Camera abstraction not ported from DVP-based ESP32/S3 drivers
- **Fix:** Use M5Unified BSP on Tab5 or ESP-IDF for CrowPanel
- **Boards:** CrowPanel (no Arduino camera), Tab5 (works via M5Unified)

#### G-C2: Tab5 Camera Must Use 1-Lane MIPI-CSI
- **Symptom:** Zero frames captured in 2-lane mode
- **Cause:** Hardware routing limitation on Tab5 PCB, not a driver bug
- **Fix:** Configure 1-lane MIPI-CSI only
- **Boards:** M5Stack Tab5

#### G-C3: SC202CS Sensor Identity Confusion
- **Symptom:** Sensor marketed as "SC2356" but chip ID is 0xEB52
- **Cause:** M5Stack marketing name differs from actual silicon
- **Fix:** Use chip ID 0xEB52 for detection; esp_cam_sensor recognizes it in ESP-IDF
- **Boards:** M5Stack Tab5

---

### 6.10 Ethernet (2 issues)

#### G-E1: ETH_PHY Defines Must Appear Before #include <ETH.h>
- **Symptom:** Ethernet fails to initialize or uses wrong PHY
- **Cause:** ETH.h reads #defines at include time
- **Fix:** Place all `#define ETH_PHY_*` lines BEFORE `#include <ETH.h>`
- **Boards:** Waveshare P4-ETH

#### G-E2: CrowPanel Has No Onboard Ethernet
- **Symptom:** No Ethernet hardware on CrowPanel
- **Cause:** CrowPanel doesn't include Ethernet PHY
- **Fix:** Use external W5500 module via bit-bang SPI on expansion header GPIOs (2, 3, 4, 5, 25)
- **Boards:** CrowPanel

---

### 6.11 Library Versions / Board Packages (4 issues)

#### G-LV1: Golden Config Must Be Followed
- arduino-esp32 board package: Use the version specified per board (3.3.3+ for CrowPanel WiFi, 3.2.6 for Tab5 via M5Stack)
- M5Stack board package: Pin to **3.2.6** (NOT 3.3.x)
- ESP32_Display_Panel: v1.0.4+
- LVGL: v9.2.x exclusively (NOT v8, NOT v10)
- LV_COLOR_DEPTH: 16

#### G-LV2: M5Stack 3.2.6 vs 3.3.x Tradeoff
- 3.2.6: Flicker-free display, WiFi, camera, all peripherals, NO BLE
- 3.3.x: BLE works, but display flicker bug present
- **Recommendation:** Use 3.2.6 unless BLE is absolutely required

#### G-LV3: PlatformIO P4 Support
- **Symptom:** No P4 board definitions in official PlatformIO
- **Fix:** Use pioarduino community fork: `platform = https://github.com/pioarduino/platform-espressif32.git`
- **Boards:** All (PlatformIO users)

#### G-LV4: LVGL Major Version Breaks Everything
- **Cause:** v9-to-v10 will change API extensively (same as v8-to-v9 did)
- **Fix:** Pin LVGL version. Do not upgrade major versions without full code review.
- **Boards:** All

---

### 6.12 Hardware / Silicon (2 issues)

#### G-H1: P4 Silicon Revisions: v1.x vs v3.0 NOT Compatible
- **Symptom:** Code compiled for v1.x won't run on v3.0 and vice versa
- **Cause:** Pin 54 changed function (NC -> VDD_HP_1), DCDC feedback circuit now required, USB PHY changes
- **Fix:** Check silicon revision. Current boards (CrowPanel, Waveshare, Tab5) ship with v1.x. Boards purchased late 2026+ may have v3.0. Firmware must target correct revision.
- **Boards:** All P4 boards (future concern)

#### G-H2: USB PHY Leaks Current in Sleep (v1.x)
- **Symptom:** Higher than expected current draw during deep sleep
- **Cause:** USB PHY leakage on v1.0/v1.3 silicon
- **Fix:** MOSFET to cut power to USB PHY during sleep (v3.0 fixes this in silicon)
- **Boards:** All v1.x silicon

---

### 6.13 Deep Sleep (1 issue)

#### G-DS1: Deep Sleep Power Draw
- **Symptom:** Battery drains faster than expected even in deep sleep
- **Cause:** P4 is power-hungry: 400MHz dual-core, 32MB PSRAM, MIPI-DSI display
- **Fix:** Deep sleep is essential for battery-powered applications. Timer wake-up is reliable. Data must be stored to survive power cycles (NVS or RTC memory).
- **Boards:** All

---

### 6.14 NVS / Flash Storage (2 issues)

#### G-N1: Flash Wear from Frequent Writes
- **Symptom:** Flash wears out, data corruption over time
- **Cause:** Writing NVS on every data change (e.g., every sensor reading)
- **Fix:** Checkpoint pattern: write at most once every 5-30 minutes. At 288 writes/day, flash lasts 347+ days. Keep frequently-changing data in RAM, save to NVS on graceful shutdown.
- **Boards:** All

#### G-N2: Struct Versioning for NVS
- **Symptom:** Silent data corruption after firmware update changes struct layout
- **Cause:** Old stored data doesn't match new struct format
- **Fix:** Include version field and CRC in config structs. Validate on load. Add new fields before CRC field, bump CONFIG_VERSION.
- **Boards:** All

---

### 6.15 USB Host (1 issue)

#### G-U1: Limited USB Host Driver Support
- **Symptom:** Mass Storage, CDC-ACM, USB-Ethernet don't work in Arduino
- **Cause:** Arduino-ESP32 USB Host library for P4 only supports HID (keyboard) as of early 2026
- **Fix:** Use ESP-IDF for Mass Storage, CDC-ACM, USB-Ethernet device classes
- **Boards:** All

---

### 6.16 FreeRTOS / Dual-Core (1 issue)

#### G-F1: Blocking I/O Freezes LVGL (The Core Architectural Flaw)
- **Symptom:** UI freezes during network operations
- **Cause:** Single-core Arduino sketch runs blocking WiFi/HTTP/MQTT on same core as LVGL
- **Fix:** Dual-core pattern: Core 0 = network, Core 1 = LVGL, FreeRTOS queues for inter-core data. LVGL is NOT thread-safe -- only one core may call LVGL functions.
- **Boards:** All

---

### 6.17 FFT / Signal Processing (2 issues)

#### G-FF1: Not Windowing FFT Data
- **Symptom:** Broad humps instead of sharp frequency peaks
- **Cause:** Not applying Hann window before FFT
- **Fix:** Multiply input samples by Hann window function before FFT
- **Boards:** All

#### G-FF2: 16-bit FFT Overflow
- **Symptom:** Incorrect FFT results with `dsps_fft2r_sc16()`
- **Cause:** 16-bit fixed-point requires careful scaling to avoid overflow
- **Fix:** Use 32-bit float (`dsps_fft2r_fc32()`) for most sensor applications -- easier and sufficient
- **Boards:** All

---

### 6.18 Matter / Thread / Zigbee (1 issue)

#### G-M1: Not Yet Available in Arduino
- **Symptom:** Cannot use Matter, Thread, or Zigbee protocols
- **Cause:** C6 co-processor supports Thread/Zigbee at hardware level but Arduino core doesn't expose them yet
- **Fix:** Use MQTT + Home Assistant or similar bridges. ESP-IDF has partial support.
- **Boards:** All

---

### 6.19 Board Cross-Compatibility (2 issues)

#### G-X1: Board Configs Are NOT Interchangeable
- **Symptom:** Code from one board doesn't work on another even with same resolution
- **Cause:** Different LCD controllers (EK79007 vs JD9365 vs ILI9881C vs ILI9488), different bus interfaces (MIPI-DSI vs SPI), different graphics libraries (ESP_Display_Panel vs LovyanGFX vs M5GFX)
- **Fix:** Use the correct `board_config.h` for your specific board. Application code uses symbolic names; only the config file changes between boards.
- **Boards:** All -- CrowPanel, Waveshare P4-ETH, Tab5, Waveshare 7B all need different configs

#### G-X2: Waveshare P4-ETH Uses LovyanGFX, Not ESP_Display_Panel
- **Symptom:** CrowPanel display code doesn't work on Waveshare P4-ETH
- **Cause:** SPI display (ILI9488) vs MIPI-DSI requires different graphics library
- **Fix:** Use LovyanGFX class definition for Waveshare P4-ETH, ESP_Display_Panel for CrowPanel
- **Boards:** Waveshare P4-ETH

---

## 7. WiFi Init Template (CrowPanel -- Copy-Paste Ready)

This is the exact, correct, tested WiFi initialization sequence for CrowPanel. Every line matters. Do not reorder, remove delays, or skip steps.

```cpp
#include <WiFi.h>

const char* ssid     = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

void setup_wifi() {
    // Step 1: Enable the C6 co-processor (MUST be first)
    pinMode(54, OUTPUT);
    digitalWrite(54, HIGH);   // C6_EN = GPIO 54
    delay(500);               // Let C6 boot before touching SDIO

    // Step 2: Configure SDIO pins (MUST come before WiFi.mode)
    // CLK=18, CMD=19, D0=14, rest unused
    WiFi.setPins(18, 19, 14, -1, -1, -1, -1);

    // Step 3: Initialize STA mode
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();        // Clear any stale state
    delay(3000);              // MANDATORY: C6 SDIO bridge stabilization

    // Step 4: Connect
    Serial.printf("Connecting to %s", ssid);
    WiFi.begin(ssid, password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
        // Read RSSI once here -- do NOT poll it on a timer
        Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
    } else {
        Serial.println("\nWiFi FAILED. Check:");
        Serial.println("  - Two USB cables connected (power requirement)");
        Serial.println("  - SSID and password correct");
        Serial.println("  - Board package 3.3.3+");
        Serial.println("  - Full power cycle (unplug both USB, wait 5s)");
    }
}

void setup() {
    delay(2000);              // USB CDC enumeration
    Serial.begin(115200);

    // ... display init here (before SD card) ...

    setup_wifi();

    // ... SD card init here (after display) ...
}
```

**Common mistakes this template prevents:**
1. WiFi.setPins() after WiFi.mode() -- reversed order kills SDIO
2. Missing GPIO 54 HIGH -- C6 never boots
3. Missing 3-second delay -- SDIO bridge not ready
4. Polling WiFi.RSSI() in loop -- destabilizes SDIO link
5. Using AP mode -- unstable on CrowPanel V1.0

---

## 8. Ethernet Init Template (Waveshare P4-ETH -- Copy-Paste Ready)

```cpp
// CRITICAL: All #defines MUST appear BEFORE #include <ETH.h>
#define ETH_PHY_TYPE    ETH_PHY_IP101
#define ETH_PHY_ADDR    1
#define ETH_PHY_MDC     31
#define ETH_PHY_MDIO    52
#define ETH_PHY_POWER   51
#define ETH_CLK_MODE    EMAC_CLK_EXT_IN
#include <ETH.h>

static bool eth_connected = false;

void onEthEvent(arduino_event_id_t event) {
    switch (event) {
        case ARDUINO_EVENT_ETH_GOT_IP:
            Serial.print("ETH IP: ");
            Serial.println(ETH.localIP());
            eth_connected = true;
            break;
        case ARDUINO_EVENT_ETH_DISCONNECTED:
            Serial.println("ETH disconnected");
            eth_connected = false;
            break;
        default:
            break;
    }
}

void setup() {
    delay(2000);
    Serial.begin(115200);
    Network.onEvent(onEthEvent);
    ETH.begin();
}
```

---

## 9. LovyanGFX Class Template (Waveshare P4-ETH -- Copy-Paste Ready)

```cpp
class LGFX_WaveshareETH : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9488 _panel_instance;
    lgfx::Bus_SPI _bus_instance;
    lgfx::Touch_XPT2046 _touch_instance;
public:
    LGFX_WaveshareETH(void) {
        // SPI Bus
        auto cfg = _bus_instance.config();
        cfg.spi_host = SPI2_HOST;
        cfg.freq_write = 40000000;
        cfg.freq_read  = 16000000;
        cfg.pin_sclk = 26;
        cfg.pin_mosi = 23;
        cfg.pin_miso = 27;
        cfg.pin_dc   = 22;
        _bus_instance.config(cfg);
        _panel_instance.setBus(&_bus_instance);

        // Panel
        auto pcfg = _panel_instance.config();
        pcfg.pin_cs       = 20;
        pcfg.pin_rst      = 21;
        pcfg.panel_width  = 320;
        pcfg.panel_height = 480;
        pcfg.bus_shared   = true;
        _panel_instance.config(pcfg);

        // Touch
        auto tcfg = _touch_instance.config();
        tcfg.bus_shared      = true;
        tcfg.offset_rotation = 6;
        tcfg.spi_host        = SPI2_HOST;
        tcfg.freq            = 1000000;
        tcfg.pin_sclk = 26;
        tcfg.pin_mosi = 23;
        tcfg.pin_miso = 27;
        tcfg.pin_cs   = 33;
        _touch_instance.config(tcfg);
        _panel_instance.setTouch(&_touch_instance);
        setPanel(&_panel_instance);
    }
};
```

---

## 10. Tab5 Minimal Init Template (Copy-Paste Ready)

```cpp
#include <M5Unified.h>

void setup() {
    delay(2000);              // USB CDC enumeration
    Serial.begin(115200);

    auto cfg = M5.config();
    M5.begin(cfg);            // Inits display, touch, IMU, IO expanders, Wire on 31/32

    M5.Display.setRotation(1);
    M5.Display.setSwapBytes(true);

    Serial.println("Tab5 ready");

    // WARNING: Never call Wire.end() -- it kills camera and 8 I2C devices
    // WARNING: Must use M5Stack board package 3.2.6 (not 3.3.x)
}
```

---

## 11. Portability Rules

### The "First 100 Lines Change, Next 500 Don't" Philosophy

When writing portable ESP32-P4 code, separate your project into two layers:

**Board Config Layer (changes per board):**
- Display initialization (library, pins, timing)
- Touch controller setup (I2C address, SPI CS, rotation)
- WiFi pin configuration (setPins parameters)
- Audio pin assignments
- SD card pin mapping
- Ethernet PHY configuration
- GPIO assignments for LEDs, buttons, sensors

**Application Layer (stays the same across boards):**
- LVGL UI code (widgets, screens, event handlers)
- Business logic (sensor processing, data display)
- Network protocol code (HTTP, MQTT, TCP)
- FreeRTOS task structure
- NVS storage patterns
- State machines

### Implementation Pattern

```cpp
// board_config.h -- ONE file per board, swap to retarget
#if defined(BOARD_CROWPANEL_7)
    #define DISPLAY_W       1024
    #define DISPLAY_H       600
    #define TOUCH_SDA       45
    #define TOUCH_SCL       46
    #define TOUCH_ADDR      0x5D
    #define WIFI_C6_EN      54
    #define WIFI_SDIO_CLK   18
    #define WIFI_SDIO_CMD   19
    #define WIFI_SDIO_D0    14
    #define SD_CLK          43
    #define SD_CMD          44
    #define SD_D0           39
    #define AMP_EN          30
    #define AMP_EN_ACTIVE   LOW
    #define BL_PIN          31
    // ... etc
#elif defined(BOARD_WAVESHARE_P4_ETH)
    #define DISPLAY_W       480
    #define DISPLAY_H       320
    // ... etc
#elif defined(BOARD_M5STACK_TAB5)
    #define DISPLAY_W       1024
    #define DISPLAY_H       600
    // M5.begin() handles pin config
#endif
```

### What Changes Between Boards

| Component | CrowPanel | P4-ETH | Tab5 |
|---|---|---|---|
| Display library | ESP_Display_Panel | LovyanGFX | M5GFX |
| Display bus | MIPI-DSI | SPI | MIPI-DSI |
| LCD controller | EK79007 | ILI9488 | ST7123/ILI9881C |
| Touch type | GT911 (I2C) | XPT2046 (SPI) | FT5x06 (I2C) |
| WiFi | C6 SDIO | None | C6 SDIO |
| Ethernet | W5500 external | IP101 RMII | None |
| Audio | I2S + NS4168 | None | Codec (0x40) |
| Camera | None (Arduino) | None | SC202CS CSI |
| SD card | SD_MMC 1-bit | None | None |
| Board package | Espressif 3.3.3+ | Espressif 3.x | M5Stack 3.2.6 |

### What Stays the Same

- LVGL widget creation and event handling
- HTTP/MQTT client code (once WiFi/ETH is connected)
- FreeRTOS dual-core task pattern
- NVS read/write logic
- Data processing and business logic
- UI layout and screen management
- Sensor data interpretation (after I2C init)

---

## Quick Reference: The 10 Most Common Mistakes

| # | Mistake | Consequence | Fix |
|---|---------|-------------|-----|
| 1 | PSRAM not set to OPI | Crashes everything | Tools > PSRAM > OPI PSRAM |
| 2 | WiFi.setPins() after WiFi.mode() | WiFi never connects | setPins FIRST |
| 3 | Missing 3-second delay after WiFi.mode() | WiFi unreliable | `delay(3000)` mandatory |
| 4 | SD card initialized before display | SD fails or display corrupts | Display FIRST |
| 5 | Default partition scheme | Sketch too large | Use Huge APP |
| 6 | lv_conf.h not created | LVGL won't compile | Copy template, rename, enable |
| 7 | M5Stack boards 3.3.x on Tab5 | Display flickers | Pin to 3.2.6 |
| 8 | Blocking I/O on LVGL core | UI freezes | Dual-core FreeRTOS |
| 9 | No delay(2000) at start of setup() | No serial output | USB CDC needs time |
| 10 | Mixing I2C driver APIs | Crash before setup() | Use new API only |
