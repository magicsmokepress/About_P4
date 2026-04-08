# CrowPanel Advanced 7" ESP32-P4 HMI AI Display — Developer Cookbook

*A practical, recipe-based guide to programming the Elecrow CrowPanel Advanced 7-inch ESP32-P4 HMI AI Display (1024×600 IPS Touch Screen) using ESP-IDF and LVGL.*

---

## 1. Device Overview

The CrowPanel Advanced 7" is a feature-rich HMI development board built around the ESP32-P4 RISC-V SoC. It combines a high-resolution capacitive touchscreen with extensive connectivity and peripheral options.

**Core Specifications**

| Feature | Detail |
|---|---|
| **SoC** | ESP32-P4NRW32 — RISC-V dual-core HP (400 MHz) + single-core LP (40 MHz) |
| **Memory** | 768 KB L2MEM, 32 KB SRAM (LP), 8 KB TCM, 32 MB PSRAM |
| **Storage** | 128 KB ROM (HP), 16 KB ROM (LP), 16 MB Flash |
| **Display** | 7.0" IPS, 1024×600, 16.7M colors (8-bit), 178° viewing angle, 400 cd/m² |
| **Touch** | Capacitive, 1–5 point multi-touch (GT911 controller via I2C) |
| **WiFi** | 2.4 GHz Wi-Fi 6 (802.11a/b/g/n) |
| **Bluetooth** | Bluetooth 5.3 + BLE |
| **Interfaces** | USB 2.0, UART, I2C, GPIO headers, SD card slot, battery socket, speaker jack, camera header |
| **Audio** | Audio amplifier, dual microphones, dual speakers |
| **Power** | 5V/2A via USB or UART terminal; lithium battery with charging circuit |
| **Optional** | Zigbee, LoRa (SX1262), nRF2401, Matter, Thread modules |

**Pin Highlights**

The ESP32-P4 requires LDO power rail configuration before most peripherals work. LDO3 (2.5V) powers camera and some I/O; LDO4 (3.3V) powers general peripherals.

---

## 2. Development Environment Setup

### Prerequisites

- **VS Code** with the ESP-IDF extension
- **ESP-IDF v5.4.2 or higher** (v5.4.3 recommended)
- **LVGL 8.3.11** (included as a managed component via `lvgl/lvgl@8.3.11`)

### Quick Start (ESP-IDF)

1. Right-click in the project folder and select "Open with VS Code."
2. In the ESP-IDF plugin, select the correct COM port.
3. Click the Build icon (or run `idf.py build`).
4. Click Flash to upload to the device.
5. Open the Serial Monitor to see log output.

### Project Structure (Standard ESP-IDF Layout)

```
my_project/
├── CMakeLists.txt          # Top-level CMake (sets project name, min IDF version)
├── sdkconfig               # SDK configuration
├── main/
│   ├── CMakeLists.txt      # Registers source files and includes
│   ├── main.c              # Entry point: app_main()
│   └── main.h              # Shared macros and includes
└── components/             # Custom or third-party components
    ├── bsp_display/        # Display + LVGL initialization
    ├── bsp_i2c/            # I2C bus driver
    ├── bsp_extra/          # GPIO, LED, and misc peripherals
    └── ...
```

### Top-Level CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my_crowpanel_app)
```

### main/CMakeLists.txt

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES bsp_display bsp_i2c bsp_extra
)
```

---

## 3. Foundational Recipes

### Recipe 3.1 — Hello World (Serial Output)

The simplest possible program. Prints to the serial monitor in an infinite loop.

```c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    int i = 0;
    while (1) {
        printf("Hello world: %d\n", i++);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
```

**Key Takeaway:** Every ESP-IDF application starts with `app_main(void)`. FreeRTOS is always running; use `vTaskDelay()` to avoid watchdog timeouts.

---

### Recipe 3.2 — LDO Power Rail Initialization (Required for Display)

Almost every CrowPanel project needs LDO3 and LDO4 configured before initializing the display, camera, or other peripherals. This is a boilerplate pattern you'll see in every advanced example.

```c
#include "esp_ldo_regulator.h"
#include "esp_log.h"

static esp_ldo_channel_handle_t ldo3 = NULL;
static esp_ldo_channel_handle_t ldo4 = NULL;

static void init_fail_handler(const char *module_name, esp_err_t err) {
    while (1) {
        ESP_LOGE("INIT", "[%s] init failed: %s", module_name, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void ldo_init(void) {
    esp_err_t err;

    // LDO3: 2.5V — required for camera and some peripherals
    esp_ldo_channel_config_t ldo3_cfg = {
        .chan_id = 3,
        .voltage_mv = 2500,
    };
    err = esp_ldo_acquire_channel(&ldo3_cfg, &ldo3);
    if (err != ESP_OK) init_fail_handler("LDO3", err);

    // LDO4: 3.3V — required for general peripherals
    esp_ldo_channel_config_t ldo4_cfg = {
        .chan_id = 4,
        .voltage_mv = 3300,
    };
    err = esp_ldo_acquire_channel(&ldo4_cfg, &ldo4);
    if (err != ESP_OK) init_fail_handler("LDO4", err);
}
```

**Key Takeaway:** Always initialize LDO3 (2.5V) and LDO4 (3.3V) before calling `display_init()` or `camera_init()`. The `init_fail_handler` pattern (infinite error loop) is used throughout the official examples to catch and report init failures.

---

### Recipe 3.3 — I2C Bus Initialization

The I2C bus is required for the GT911 touch controller, DHT20 sensor, and other peripherals.

```c
#include "bsp_i2c.h"

void setup_i2c(void) {
    esp_err_t err = i2c_init();
    if (err != ESP_OK) {
        ESP_LOGE("I2C", "I2C initialization failed");
        return;
    }
    ESP_LOGI("I2C", "I2C bus initialized (400 kHz)");
}
```

Under the hood, the BSP configures I2C0 at 400 kHz in master mode, typically on SDA=GPIO3, SCL=GPIO2 with internal pull-ups enabled.

---

## 4. Display & Graphics Recipes

### Recipe 4.1 — Turning On the Screen with LVGL

This is the fundamental "display hello" recipe. It shows the complete initialization sequence: LDO → display → backlight → LVGL content.

```c
#include "bsp_illuminate.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"

#define TAG "MAIN"

static esp_ldo_channel_handle_t ldo3 = NULL;
static esp_ldo_channel_handle_t ldo4 = NULL;

// Display "Hello Elecrow" centered on screen
static void show_hello(void) {
    if (lvgl_port_lock(0) != true) return;

    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, LV_COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, "Hello Elecrow");

    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_text_font(&style, &lv_font_montserrat_42);
    lv_style_set_text_color(&style, LV_COLOR_BLACK);
    lv_style_set_bg_opa(&style, LV_OPA_TRANSP);
    lv_obj_add_style(label, &style, LV_PART_MAIN);
    lv_obj_center(label);

    lvgl_port_unlock();
}

void app_main(void) {
    // 1. Init LDOs (see Recipe 3.2)
    ldo_init();

    // 2. Init display hardware + LVGL
    esp_err_t err = display_init();
    if (err != ESP_OK) { /* handle error */ }

    // 3. Turn on backlight (0–100)
    set_lcd_blight(100);

    // 4. Draw content
    show_hello();
}
```

**Key Takeaway:** Always call `lvgl_port_lock(0)` before modifying LVGL objects, and `lvgl_port_unlock()` when done. This ensures thread safety since LVGL runs in its own FreeRTOS task.

---

### Recipe 4.2 — LVGL Thread Safety Pattern

LVGL is not thread-safe. Any time you create, modify, or read LVGL objects from outside the LVGL handler task, you must acquire the lock.

```c
// SAFE: Always wrap LVGL calls in lock/unlock
if (lvgl_port_lock(0)) {
    lv_label_set_text(my_label, "Updated text");
    lvgl_port_unlock();
}

// UNSAFE: Never do this without locking
// lv_label_set_text(my_label, "This may crash");
```

The `0` parameter means non-blocking. Use `pdMS_TO_TICKS(100)` for a timeout-based wait.

---

### Recipe 4.3 — Creating Interactive Buttons with Event Callbacks

A complete UI with ON/OFF buttons controlling an LED via LVGL events.

```c
#include "lvgl.h"

// Callback: LED ON
static void btn_on_click(lv_event_t *e) {
    (void)e;
    gpio_extra_set_level(true);
    ESP_LOGI("UI", "LED turned ON");
}

// Callback: LED OFF
static void btn_off_click(lv_event_t *e) {
    (void)e;
    gpio_extra_set_level(false);
    ESP_LOGI("UI", "LED turned OFF");
}

void create_led_control_ui(void) {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xFFFFFF), LV_PART_MAIN);

    // Title
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "LED Controller");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);

    // ON button
    lv_obj_t *btn_on = lv_btn_create(scr);
    lv_obj_set_size(btn_on, 120, 50);
    lv_obj_align(btn_on, LV_ALIGN_CENTER, 0, -40);
    lv_obj_add_event_cb(btn_on, btn_on_click, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_on = lv_label_create(btn_on);
    lv_label_set_text(lbl_on, "LED ON");

    // OFF button
    lv_obj_t *btn_off = lv_btn_create(scr);
    lv_obj_set_size(btn_off, 120, 50);
    lv_obj_align(btn_off, LV_ALIGN_CENTER, 0, 40);
    lv_obj_add_event_cb(btn_off, btn_off_click, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_off = lv_label_create(btn_off);
    lv_label_set_text(lbl_off, "LED OFF");
}
```

**Key Takeaway:** Use `lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, NULL)` to register click handlers. The last parameter is optional user data.

---

### Recipe 4.4 — Displaying Images (Weather App Style)

Use `LV_IMG_DECLARE` and `lv_img_create` for background images and icons.

```c
LV_IMG_DECLARE(image_both);  // Declare image (generated by LVGL image converter)

void create_weather_screen(void) {
    if (lvgl_port_lock(0)) {
        // Full-screen background image
        lv_obj_t *bg = lv_img_create(lv_scr_act());
        lv_img_set_src(bg, &image_both);
        lv_obj_align(bg, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_set_size(bg, LV_HOR_RES, LV_VER_RES);

        // Disable scrolling on the background
        lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE
                             | LV_OBJ_FLAG_SCROLL_ELASTIC
                             | LV_OBJ_FLAG_SCROLL_MOMENTUM);

        // Temperature overlay
        lv_obj_t *temp = lv_label_create(bg);
        lv_obj_align(temp, LV_ALIGN_TOP_RIGHT, -50, 80);
        lv_label_set_text(temp, "25.4°C");
        lv_obj_set_style_text_font(temp, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(temp, lv_color_hex(0xFFFFFF), 0);

        lvgl_port_unlock();
    }
}
```

---

### Recipe 4.5 — Dynamic Label Updates (Sensor Dashboard Pattern)

A pattern for continuously updating on-screen values from a sensor reading task.

```c
static lv_obj_t *data_label = NULL;

// Called once at startup
void init_display_label(void) {
    if (lvgl_port_lock(0)) {
        data_label = lv_label_create(lv_scr_act());

        static lv_style_t style;
        lv_style_init(&style);
        lv_style_set_bg_opa(&style, LV_OPA_TRANSP);
        lv_style_set_text_color(&style, LV_COLOR_WHITE);
        lv_style_set_text_font(&style, &lv_font_montserrat_30);
        lv_obj_add_style(data_label, &style, LV_PART_MAIN);

        lv_obj_center(data_label);
        lv_obj_set_style_bg_color(lv_scr_act(), LV_COLOR_BLACK, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, LV_PART_MAIN);
        lv_label_set_text(data_label, "Waiting for data...");

        lvgl_port_unlock();
    }
}

// Called from sensor task whenever new data arrives
void update_display(float temperature, float humidity) {
    if (data_label != NULL) {
        char buf[60];
        snprintf(buf, sizeof(buf), "Temp = %.1f C   Humidity = %.1f %%",
                 temperature, humidity);

        if (lvgl_port_lock(0)) {
            lv_label_set_text(data_label, buf);
            lvgl_port_unlock();
        }
    }
}
```

---

## 5. Input & Peripheral Recipes

### Recipe 5.1 — Reading Touch Coordinates

Continuous touch polling in a dedicated FreeRTOS task.

```c
#include "bsp_i2c.h"
#include "bsp_display.h"

TaskHandle_t touch_task_handle = NULL;

void touch_task(void *param) {
    while (1) {
        if (touch_read() == ESP_OK) {
            uint16_t x, y;
            bool pressed;
            get_coor(&x, &y, &pressed);

            if (pressed) {
                ESP_LOGI("TOUCH", "Touch at X=%d, Y=%d", x, y);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));  // 20 Hz polling
    }
}

void app_main(void) {
    i2c_init();
    touch_init();
    xTaskCreate(touch_task, "touch_task", 4096, NULL, 5, &touch_task_handle);
}
```

---

### Recipe 5.2 — GPIO / LED Control

```c
#include "bsp_extra.h"

void led_example(void) {
    esp_err_t err = gpio_extra_init();  // Initialize GPIO48 for LED
    if (err != ESP_OK) return;

    gpio_extra_set_level(true);   // LED ON
    vTaskDelay(pdMS_TO_TICKS(1000));
    gpio_extra_set_level(false);  // LED OFF
}
```

---

### Recipe 5.3 — SD Card Read/Write

```c
#include "main.h"

void sd_test(void) {
    esp_err_t err = sd_init();
    if (err != ESP_OK) {
        ESP_LOGE("SD", "SD card init failed");
        return;
    }

    get_sd_card_info();  // Print card size, type, speed

    const char *filepath = SD_MOUNT_POINT "/hello.txt";
    char *data = "hello world!";

    // Write
    err = write_string_file(filepath, data);
    if (err != ESP_OK) {
        ESP_LOGE("SD", "Write failed");
        return;
    }
    vTaskDelay(200 / portTICK_PERIOD_MS);

    // Read
    err = read_string_file(filepath);
    if (err != ESP_OK) {
        ESP_LOGE("SD", "Read failed");
    }
}
```

**Key Takeaway:** SD card operations use the standard ESP-IDF VFS mount at `SD_MOUNT_POINT`. Use `xTaskCreatePinnedToCore()` with core 1 for SD-intensive tasks.

---

### Recipe 5.4 — Serial/UART Communication (AT Commands)

Communicating with external WiFi modules (like ESP8266) via UART and AT commands.

```c
#include "bsp_uart.h"

static int uart_read_response(char *buffer, size_t len, TickType_t timeout) {
    int total = 0;
    TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < timeout && total < len - 1) {
        int bytes = uart_read_bytes(UART_NUM_2,
                        (uint8_t *)(buffer + total),
                        len - total - 1,
                        20 / portTICK_PERIOD_MS);
        if (bytes > 0) total += bytes;
    }
    buffer[total] = '\0';
    return total;
}

static bool send_at_command(const char *cmd, TickType_t timeout) {
    char response[512] = {0};
    SendData(cmd);
    SendData("\r\n");
    uart_read_response(response, 512, timeout);
    ESP_LOGI("AT", "Response: %s", response);
    return strstr(response, "OK") != NULL;
}

void wifi_via_uart(void) {
    uart_init();
    send_at_command("AT+CWMODE=3", pdMS_TO_TICKS(1000));
    send_at_command("AT+RST", pdMS_TO_TICKS(2000));
    vTaskDelay(pdMS_TO_TICKS(3000));

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", "MySSID", "MyPass");
    if (send_at_command(cmd, pdMS_TO_TICKS(5000))) {
        ESP_LOGI("AT", "WiFi connected!");
    }
}
```

---

### Recipe 5.5 — USB 2.0 HID Mouse Emulation

Use the touchscreen as a USB mouse input device.

```c
#include "bsp_usb.h"
#include "bsp_i2c.h"
#include "bsp_display.h"

void touch_mouse_task(void *param) {
    uint16_t prev_x = 0xffff, prev_y = 0xffff;
    bool prev_pressed = false;

    while (1) {
        if (touch_read() == ESP_OK) {
            uint16_t x, y;
            bool pressed;
            get_coor(&x, &y, &pressed);

            if (pressed && is_usb_ready()) {
                if (prev_pressed && prev_x != 0xffff) {
                    int16_t dx = (int16_t)x - (int16_t)prev_x;
                    int16_t dy = (int16_t)y - (int16_t)prev_y;
                    send_hid_mouse_delta(dx, dy);
                }
                prev_x = x;
                prev_y = y;
            } else if (!pressed) {
                prev_x = 0xffff;
                prev_y = 0xffff;
            }
            prev_pressed = pressed;
        }
        vTaskDelay(pdMS_TO_TICKS(10));  // 100 Hz
    }
}

void app_main(void) {
    i2c_init();
    touch_init();
    usb_init();
    xTaskCreate(touch_mouse_task, "touch_mouse", 4096, NULL, 5, NULL);
}
```

---

## 6. Sensor & Data Recipes

### Recipe 6.1 — Temperature & Humidity (DHT20 Sensor + LVGL Display)

A complete sensor-to-screen pipeline using I2C and LVGL.

```c
#include "main.h"

static lv_obj_t *dht20_label = NULL;

void dht20_display_init(void) {
    if (lvgl_port_lock(0)) {
        dht20_label = lv_label_create(lv_scr_act());
        lv_obj_set_style_text_color(dht20_label, LV_COLOR_WHITE, LV_PART_MAIN);
        lv_obj_set_style_text_font(dht20_label, &lv_font_montserrat_30, LV_PART_MAIN);
        lv_obj_center(dht20_label);
        lv_obj_set_style_bg_color(lv_scr_act(), LV_COLOR_BLACK, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, LV_PART_MAIN);
        lv_label_set_text(dht20_label, "Temperature = 0.0 C  Humidity = 0.0 %");
        lvgl_port_unlock();
    }
}

void dht20_read_task(void *param) {
    dht20_data_t measurements;
    while (1) {
        if (dht20_is_calibrated() != ESP_OK) {
            dht20_begin();  // Re-initialize if needed
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        if (dht20_read_data(&measurements) == ESP_OK) {
            char buf[60];
            snprintf(buf, sizeof(buf), "Temp = %.1f C  Humidity = %.1f %%",
                     measurements.temperature, measurements.humidity);

            if (lvgl_port_lock(0)) {
                lv_label_set_text(dht20_label, buf);
                lvgl_port_unlock();
            }
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void app_main(void) {
    // Init: LDOs → I2C → DHT20 → Display → Backlight
    ldo_init();
    i2c_init();
    dht20_begin();
    display_init();
    set_lcd_blight(100);

    dht20_display_init();
    xTaskCreate(dht20_read_task, "dht20", 4096, NULL,
                configMAX_PRIORITIES - 5, NULL);
}
```

---

### Recipe 6.2 — Weather Data via WiFi (HTTP + JSON)

Connecting to a weather API and displaying results on screen.

```c
#include "bsp_display.h"
#include "bsp_wifi.h"
#include "weather.h"
#include <nvs_flash.h>

void app_main(void) {
    // 1. Init LDO, NVS, I2C, Touch, Display
    ldo_init();

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    i2c_init();
    touch_init();
    display_init();

    // 2. Connect to WiFi
    bsp_wifi_init();
    bsp_wifi_sta_init();
    bsp_wifi_connect("Your_SSID", "Your_Password");

    // 3. Wait for connection and fetch weather
    weather_t *weather = weather_create();
    double temp_c = 0.0;
    char weather_text[64];
    int timestamp = 0;

    while (1) {
        if (WIFI_CONNECTED == bsp_wifi_get_state()) {
            if (weather_get_weather(weather, &temp_c, weather_text, &timestamp)) {
                break;  // Got weather data
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // 4. Display weather on screen
    char temp_str[32];
    snprintf(temp_str, sizeof(temp_str), "%.1lf°C", temp_c);

    if (lvgl_port_lock(0)) {
        lv_obj_t *label = lv_label_create(lv_scr_act());
        lv_label_set_text(label, temp_str);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(label);
        lvgl_port_unlock();
    }

    set_lcd_blight(100);
}
```

**WiFi initialization pattern:** Always init NVS Flash before WiFi. Use the `bsp_wifi` component for simplified connection management.

---

## 7. Audio & Media Recipes

### Recipe 7.1 — Record and Playback Audio (5-Second Clip)

```c
#include "main.h"

void app_main(void) {
    // Init audio amplifier + I2S
    esp_err_t err = audio_ctrl_init();
    if (err != ESP_OK) { /* halt */ }
    set_Audio_ctrl(false);  // Amp off during init

    err = audio_init();
    if (err != ESP_OK) { /* halt */ }

    // Init microphone
    err = mic_init();
    if (err != ESP_OK) { /* halt */ }

    // Record 5 seconds, then play back
    ESP_LOGI("AUDIO", "Recording 5 seconds...");
    err = mic_read_to_audio(5);
    if (err == ESP_OK) {
        ESP_LOGI("AUDIO", "Playback complete");
    }

    while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
```

### Recipe 7.2 — Play WAV from SD Card

```c
#include "main.h"

void app_main(void) {
    // Init LDOs (needed for SD card power)
    ldo_init();

    // Init audio hardware
    audio_ctrl_init();
    audio_init();

    // Init SD card
    sd_init();

    // Play a WAV file from the SD card
    play_wav_from_sd(SD_MOUNT_POINT "/music.wav");

    while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
```

**Tip:** Use the included `convert_wav` utility to convert audio files to the correct WAV format (16-bit PCM, mono or stereo, appropriate sample rate).

---

## 8. Camera Recipe

### Recipe 8.1 — Real-Time Camera Display

```c
#include "main.h"

TaskHandle_t cam_task;

void camera_display_task(void *param) {
    while (1) {
        if (lvgl_port_lock(0)) {
            camera_display_refresh();
            lvgl_port_unlock();
        }
        vTaskDelay(23 / portTICK_PERIOD_MS);  // ~43 FPS
    }
}

void app_main(void) {
    // Init: LDOs → GPIO ISR → Display → Backlight → Camera
    ldo_init();
    gpio_install_isr_service(0);
    display_init();
    set_lcd_blight(100);
    camera_init();
    camera_display();

    camera_refresh();  // First frame

    xTaskCreatePinnedToCore(camera_display_task, "cam_display",
                            4096, NULL,
                            configMAX_PRIORITIES - 4,
                            &cam_task, 1);  // Pin to Core 1
}
```

**Key Takeaway:** Camera display refresh is pinned to Core 1 for optimal performance. The 23ms delay yields approximately 43 FPS. Call `gpio_install_isr_service(0)` before camera init.

---

## 9. Wireless Communication Recipes

### Recipe 9.1 — LoRa Transmission (SX1262 Module)

```c
#include "include/main.h"

static lv_obj_t *counter_label = NULL;

void lora_tx_task(void *param) {
    TickType_t last_wake = xTaskGetTickCount();
    while (1) {
        bool ok = send_lora_pack_radio();
        if (!ok) ESP_LOGE("LORA", "TX failed");
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));
    }
}

void ui_counter_task(void *param) {
    char text[48];
    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        uint32_t count = sx1262_get_tx_counter();
        snprintf(text, sizeof(text), "TX_Hello World:%lu", (unsigned long)count);

        if (lvgl_port_lock(0)) {
            lv_label_set_text(counter_label, text);
            lvgl_port_unlock();
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));
    }
}

void app_main(void) {
    ldo_init();
    display_init();
    set_lcd_blight(100);
    sx1262_tx_init();

    // Init UI (counter_label setup omitted for brevity)

    xTaskCreatePinnedToCore(ui_counter_task, "ui", 4096, NULL,
                            configMAX_PRIORITIES - 5, NULL, 0);
    xTaskCreatePinnedToCore(lora_tx_task, "lora_tx", 8192, NULL,
                            configMAX_PRIORITIES - 5, NULL, 1);
}
```

**Key Takeaway:** Use `vTaskDelayUntil()` instead of `vTaskDelay()` for precise timing intervals. Pin the radio task to Core 1 and the UI task to Core 0.

---

## 10. Common Patterns & Best Practices

### 10.1 — Standard Initialization Sequence

Almost every CrowPanel application follows this order:

1. **LDO init** — Configure LDO3 (2.5V) and LDO4 (3.3V)
2. **I2C init** — Required for touch, sensors, and peripherals
3. **Touch init** — Initialize GT911 capacitive touch controller
4. **Display init** — MIPI-DSI display + LVGL framework
5. **Backlight** — `set_lcd_blight(100)` to turn on the screen
6. **Peripheral init** — Camera, audio, SD card, wireless modules
7. **Create UI** — LVGL widgets and screens
8. **Create tasks** — FreeRTOS tasks for background processing

### 10.2 — Error Handling Pattern

The official examples consistently use an "init or halt" pattern:

```c
static void init_or_halt(const char *name, esp_err_t err) {
    if (err != ESP_OK) {
        ESP_LOGE("INIT", "%s failed: %s", name, esp_err_to_name(err));
        while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }
}
```

This halts execution on failure so you can identify the broken component via serial monitor.

### 10.3 — FreeRTOS Task Creation Cheatsheet

```c
// Basic task (any core)
xTaskCreate(my_task, "name", 4096, NULL, 5, &handle);

// Pinned to specific core (0 or 1)
xTaskCreatePinnedToCore(my_task, "name", 4096, NULL, 5, &handle, 1);

// Priority: configMAX_PRIORITIES - N (lower N = higher priority)
// Typical stack: 4096 bytes (increase for complex tasks)

// Precise timing with vTaskDelayUntil
TickType_t last_wake = xTaskGetTickCount();
vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));  // Exact 1-second intervals
```

### 10.4 — Logging Macros

All examples use a consistent logging pattern:

```c
#define TAG "MY_MODULE"
#define MY_INFO(fmt, ...)  ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
#define MY_ERROR(fmt, ...) ESP_LOGE(TAG, fmt, ##__VA_ARGS__)
#define MY_DEBUG(fmt, ...) ESP_LOGD(TAG, fmt, ##__VA_ARGS__)
```

### 10.5 — LVGL Style Cheatsheet

```c
// Create and apply a style
static lv_style_t style;
lv_style_init(&style);
lv_style_set_text_font(&style, &lv_font_montserrat_24);
lv_style_set_text_color(&style, lv_color_hex(0xFF0000));
lv_style_set_bg_color(&style, lv_color_hex(0x000000));
lv_style_set_bg_opa(&style, LV_OPA_COVER);
lv_style_set_border_width(&style, 2);
lv_style_set_radius(&style, 10);
lv_style_set_pad_all(&style, 10);
lv_obj_add_style(my_obj, &style, LV_PART_MAIN);

// Available fonts (must be enabled in lv_conf.h):
// lv_font_montserrat_14, _16, _18, _20, _22, _24, _26, _28, _30,
// _32, _34, _36, _38, _40, _42, _44, _46, _48

// Alignment helpers:
lv_obj_center(obj);                              // Center on parent
lv_obj_align(obj, LV_ALIGN_TOP_MID, 0, 20);     // Top-center + 20px offset
lv_obj_align(obj, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
```

---

## 11. SquareLine Studio Integration — Complete LED Control Walkthrough

This section dissects the official **SquareLine_LVGL_LED** example end-to-end: a full working project that uses SquareLine Studio 1.5.1 to visually design an LED ON/OFF screen, then wires the generated UI code to actual GPIO hardware on the CrowPanel.

### 11.1 — Project Structure

```
SquareLine_LVGL_LED/
├── CMakeLists.txt              # Top-level project CMake
├── partitions.csv              # Flash partition table
├── sdkconfig                   # SDK config (ESP32-P4 target)
├── dependencies.lock           # Component versions (ESP-IDF 5.4.2)
├── main/
│   ├── CMakeLists.txt          # Component registration
│   ├── main.c                  # Hardware init + ui_init() call
│   ├── main.h                  # Shared macros/includes
│   └── ui/                     # SquareLine Studio generated files
│       ├── ui.c                # UI entry point, theme, screen loader
│       ├── ui.h                # Extern declarations for all widgets
│       ├── ui_Screen1.c        # Screen layout (buttons, image, labels)
│       ├── ui_events.c         # YOUR custom event handlers (LED control)
│       ├── ui_events.h         # Event function prototypes
│       ├── ui_helpers.c        # Generated animation/utility helpers
│       ├── ui_helpers.h        # Helper function prototypes
│       └── assets/             # Image assets (table_lamp400.png, etc.)
├── managed_components/         # Auto-downloaded ESP-IDF components
│   ├── espressif__esp_lcd_ek79007/
│   ├── espressif__esp_lcd_touch_gt911/
│   └── espressif__esp_lvgl_port/
└── peripheral/                 # Board Support Package (BSP) drivers
    ├── bsp_illuminate.c/.h     # LCD + MIPI-DSI + LVGL init + backlight
    ├── bsp_display.c/.h        # GT911 touch driver
    ├── bsp_i2c.c/.h            # I2C master bus driver
    └── bsp_extra.c/.h          # GPIO48 LED control
```

**Key difference from IDF-only examples:** The SquareLine project uses **LVGL 9.2.2** (not 8.3.11), and uses `esp_lvgl_port` for LVGL lifecycle management including display double-buffering in SPIRAM.

### 11.2 — How It Works: The Three Layers

The architecture has three cleanly separated layers:

**Layer 1 — Hardware (main.c + peripheral/):** Initializes LDOs, I2C, touch, display, backlight, and GPIO. Calls `ui_init()` at the end.

**Layer 2 — Generated UI (ui/ folder):** SquareLine Studio exports `ui_init()` which sets up the theme, creates Screen1 (with a lamp image, ON button, and OFF button), and registers event callbacks.

**Layer 3 — Custom Events (ui_events.c):** The only file you edit by hand. SquareLine generates stubs; you fill in the hardware logic.

### 11.3 — Recipe: main.c — Hardware Initialization

```c
// main.c
#include "main.h"
#include "ui.h"  // Include SquareLine-generated UI header

static esp_ldo_channel_handle_t ldo3 = NULL;
static esp_ldo_channel_handle_t ldo4 = NULL;

static void init_fail_handler(const char *module_name, esp_err_t err) {
    while (1) {
        MAIN_ERROR("[%s] init failed: %s", module_name, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void system_init(void) {
    esp_err_t err = ESP_OK;

    // 1. LDO power rails
    esp_ldo_channel_config_t ldo3_cfg = { .chan_id = 3, .voltage_mv = 2500 };
    err = esp_ldo_acquire_channel(&ldo3_cfg, &ldo3);
    if (err != ESP_OK) init_fail_handler("LDO3", err);

    esp_ldo_channel_config_t ldo4_cfg = { .chan_id = 4, .voltage_mv = 3300 };
    err = esp_ldo_acquire_channel(&ldo4_cfg, &ldo4);
    if (err != ESP_OK) init_fail_handler("LDO4", err);

    // 2. I2C bus (for GT911 touch)
    err = i2c_init();
    if (err != ESP_OK) init_fail_handler("I2C", err);

    // 3. Touch panel
    err = touch_init();
    if (err != ESP_OK) init_fail_handler("Touch", err);

    // 4. LCD + LVGL (display_init handles MIPI-DSI, LVGL port, and touch registration)
    err = display_init();
    if (err != ESP_OK) init_fail_handler("LCD", err);

    // 5. Backlight ON
    err = set_lcd_blight(100);
    if (err != ESP_OK) init_fail_handler("Backlight", err);

    // 6. LED GPIO
    err = gpio_extra_init();
    if (err != ESP_OK) init_fail_handler("GPIO48", err);
    gpio_extra_set_level(false);  // LED off initially
}

void app_main(void) {
    MAIN_INFO("Starting SquareLine LED control app...");
    system_init();

    // Load the SquareLine-designed UI
    ui_init();

    MAIN_INFO("UI loaded — touch ON/OFF buttons to control LED");
}
```

**Key Takeaway:** `app_main()` is only 4 lines of logic. All complexity lives in `system_init()` (hardware) and `ui_init()` (SquareLine). No manual LVGL widget creation needed.

### 11.4 — Recipe: SquareLine-Generated UI Code

SquareLine Studio exports these files automatically. You should **never edit** them directly (they get overwritten on re-export). Here's what they do:

**ui.c — Entry point and theme setup:**

```c
// Generated by SquareLine Studio 1.5.1, LVGL 9.2.2
#include "ui.h"
#include "ui_helpers.h"

void ui_event_ButtonON(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        LedOn(e);   // Calls YOUR function in ui_events.c
    }
}

void ui_event_ButtonOFF(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        LedOff(e);  // Calls YOUR function in ui_events.c
    }
}

void ui_init(void) {
    // Set up default theme (blue primary, red secondary)
    lv_disp_t *dispp = lv_display_get_default();
    lv_theme_t *theme = lv_theme_default_init(
        dispp,
        lv_palette_main(LV_PALETTE_BLUE),
        lv_palette_main(LV_PALETTE_RED),
        false,             // Light mode
        LV_FONT_DEFAULT
    );
    lv_disp_set_theme(dispp, theme);

    // Create and load Screen1
    ui_Screen1_screen_init();
    ui____initial_actions0 = lv_obj_create(NULL);
    lv_disp_load_scr(ui_Screen1);
}
```

**ui_Screen1.c — Screen layout with buttons and image:**

```c
void ui_Screen1_screen_init(void) {
    // Create screen (no scrolling)
    ui_Screen1 = lv_obj_create(NULL);
    lv_obj_remove_flag(ui_Screen1, LV_OBJ_FLAG_SCROLLABLE);

    // Lamp image (left side, 400×400px)
    ui_Image2 = lv_image_create(ui_Screen1);
    lv_image_set_src(ui_Image2, &ui_img_table_lamp400_png);
    lv_obj_set_width(ui_Image2, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_Image2, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_Image2, -260);
    lv_obj_set_y(ui_Image2, -10);
    lv_obj_set_align(ui_Image2, LV_ALIGN_CENTER);

    // ON button (right side, golden yellow, 280×130px)
    ui_ButtonON = lv_button_create(ui_Screen1);
    lv_obj_set_size(ui_ButtonON, 280, 130);
    lv_obj_set_x(ui_ButtonON, 220);
    lv_obj_set_y(ui_ButtonON, -110);
    lv_obj_set_align(ui_ButtonON, LV_ALIGN_CENTER);
    lv_obj_set_style_bg_color(ui_ButtonON, lv_color_hex(0xF6B239),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_ButtonON, 255,
                            LV_PART_MAIN | LV_STATE_DEFAULT);

    // "ON" label inside button
    ui_LabelON = lv_label_create(ui_ButtonON);
    lv_label_set_text(ui_LabelON, "ON");
    lv_obj_set_align(ui_LabelON, LV_ALIGN_CENTER);
    lv_obj_set_style_text_color(ui_LabelON, lv_color_hex(0xFF5529),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_LabelON, &lv_font_montserrat_48,
                               LV_PART_MAIN | LV_STATE_DEFAULT);

    // OFF button (below ON, same style)
    ui_ButtonOFF = lv_button_create(ui_Screen1);
    lv_obj_set_size(ui_ButtonOFF, 280, 130);
    lv_obj_set_x(ui_ButtonOFF, 220);
    lv_obj_set_y(ui_ButtonOFF, 110);
    lv_obj_set_align(ui_ButtonOFF, LV_ALIGN_CENTER);
    lv_obj_set_style_bg_color(ui_ButtonOFF, lv_color_hex(0xF6B239),
                              LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_LabelOFF = lv_label_create(ui_ButtonOFF);
    lv_label_set_text(ui_LabelOFF, "OFF");
    lv_obj_set_align(ui_LabelOFF, LV_ALIGN_CENTER);
    lv_obj_set_style_text_color(ui_LabelOFF, lv_color_hex(0xFF5529),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_LabelOFF, &lv_font_montserrat_48,
                               LV_PART_MAIN | LV_STATE_DEFAULT);

    // Register event callbacks
    lv_obj_add_event_cb(ui_ButtonON, ui_event_ButtonON, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_ButtonOFF, ui_event_ButtonOFF, LV_EVENT_ALL, NULL);
}
```

### 11.5 — Recipe: Custom Event Handlers (The Only File You Edit)

This is `ui_events.c` — the bridge between the visual UI and your hardware:

```c
// ui_events.c — YOUR custom code goes here
#include "ui.h"
#include "bsp_extra.h"  // Import GPIO control

void LedOn(lv_event_t *e) {
    gpio_extra_set_level(true);   // GPIO48 HIGH → LED ON
}

void LedOff(lv_event_t *e) {
    gpio_extra_set_level(false);  // GPIO48 LOW → LED OFF
}
```

**This is the entire custom logic.** Everything else — the screen layout, button styling, event routing — is generated by SquareLine Studio.

### 11.6 — SquareLine Studio Setup Guide

**Step 1: Create a new project in SquareLine Studio**

| Setting | Value |
|---|---|
| LVGL Version | 9.2.2 |
| Resolution | 1024 × 600 |
| Color Depth | 16-bit |
| Theme | Default (Light) |
| Project Name | e.g., `P4_7inch_Test` |

**Step 2: Design your UI visually**

- Drag widgets (buttons, labels, images, sliders, etc.) onto the canvas
- Set positions, sizes, colors, and fonts using the Properties panel
- Add images via the Assets panel (they get converted to C arrays automatically)

**Step 3: Add events in SquareLine**

- Select a widget (e.g., ButtonON)
- In the Events panel, add an event: Trigger = `RELEASED`, Action = `Call function`
- Set the function name (e.g., `LedOn`)
- SquareLine creates the stub in `ui_events.c`

**Step 4: Export to your ESP-IDF project**

- Go to **File → Export → Export UI Files**
- Set export path to your project's `main/ui/` directory
- SquareLine generates: `ui.c`, `ui.h`, `ui_Screen1.c`, `ui_events.c`, `ui_events.h`, `ui_helpers.c`, `ui_helpers.h`, plus image assets

**Step 5: Wire up hardware in `ui_events.c`**

- Open `ui_events.c` and add your `#include` for BSP headers
- Fill in the function bodies with GPIO, I2C, WiFi, or any other hardware calls
- This file is the only one SquareLine does NOT overwrite on re-export

**Step 6: Build and flash**

```bash
idf.py build
idf.py flash monitor
```

### 11.7 — Hardware Driver Details (BSP Peripheral Layer)

The SquareLine LED project includes a complete BSP in the `peripheral/` folder. Here are the key internals:

**Display (bsp_illuminate.c) — MIPI-DSI + LVGL 9 + PWM Backlight:**

```c
// Key configuration values from the actual BSP
#define V_size          600         // Vertical resolution
#define H_size          1024        // Horizontal resolution
#define BITS_PER_PIXEL  16          // RGB565
#define LCD_GPIO_BLIGHT 31          // Backlight PWM pin
#define BLIGHT_PWM_Hz   30000       // 30 kHz PWM frequency

// MIPI-DSI bus: 2 data lanes at 900 Mbps
esp_lcd_dsi_bus_config_t bus_config = {
    .bus_id = 0,
    .num_data_lanes = 2,
    .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
    .lane_bit_rate_mbps = 900,
};

// DPI timing (51 MHz pixel clock)
esp_lcd_dpi_panel_config_t dpi_config = {
    .dpi_clock_freq_mhz = 51,
    .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
    .num_fbs = 1,
    .video_timing = {
        .h_size = 1024, .v_size = 600,
        .hsync_back_porch = 160, .hsync_pulse_width = 70,
        .hsync_front_porch = 160,
        .vsync_back_porch = 23, .vsync_pulse_width = 10,
        .vsync_front_porch = 12,
    },
    .flags.use_dma2d = true,
};

// LVGL port config (LVGL 9.x)
lvgl_port_cfg_t lvgl_cfg = {
    .task_priority = configMAX_PRIORITIES - 4,
    .task_stack = 8192 * 2,   // 16KB stack
    .task_affinity = -1,       // No core affinity
    .task_max_sleep_ms = 10,
    .timer_period_ms = 5,
};

// Display buffer in SPIRAM with double buffering
lvgl_port_display_cfg_t disp_cfg = {
    .buffer_size = (1024 * 600 * 2),   // Full framebuffer
    .double_buffer = true,
    .flags.buff_spiram = true,          // Use PSRAM
    .color_format = LV_COLOR_FORMAT_RGB565,
};
```

**Touch (bsp_display.c) — GT911 with I2C fallback addressing:**

```c
// I2C pin configuration
#define I2C_GPIO_SDA    45
#define I2C_GPIO_SCL    46
#define Touch_GPIO_RST  40
#define Touch_GPIO_INT  42

// GT911 initialization with automatic address detection
esp_lcd_panel_io_i2c_config_t io_config = {
    .dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,  // Primary: 0x5D
    .scl_speed_hz = 400000,
};

// If primary address fails, automatically tries backup address
err = esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &tp);
if (err != ESP_OK) {
    io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
    // ... retry with backup address
}
```

**GPIO LED (bsp_extra.c) — Simple GPIO48 output:**

```c
esp_err_t gpio_extra_init() {
    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << 48),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = false,
        .pull_down_en = false,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&cfg);
}

esp_err_t gpio_extra_set_level(bool level) {
    gpio_set_level(48, level);
    return ESP_OK;
}
```

### 11.8 — Partition Table

The SquareLine project uses a custom partition layout to accommodate the large LVGL assets:

```csv
# Name,   Type, SubType, Offset,  Size,   Flags
nvs,      data, nvs,     ,        0x6000,
phy_init, data, phy,     ,        0x1000,
factory,  app,  factory, ,        0xF00000,
```

The factory partition is 15 MB — large enough for LVGL image assets and the full application binary.

### 11.9 — LVGL 8.x vs 9.x API Differences

The SquareLine LED example uses **LVGL 9.2.2**, which has some API changes from the 8.3.x used in the IDF-code examples:

| LVGL 8.x (idf-code examples) | LVGL 9.x (SquareLine example) |
|---|---|
| `lv_btn_create(parent)` | `lv_button_create(parent)` |
| `lv_img_create(parent)` | `lv_image_create(parent)` |
| `lv_img_set_src(obj, src)` | `lv_image_set_src(obj, src)` |
| `lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE)` | `lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE)` |
| `lvgl_port_lock(0)` / `lvgl_port_unlock()` | Handled internally by `esp_lvgl_port` |
| `lv_disp_drv_register(&drv)` | `lv_display_get_default()` |
| Theme: `lv_theme_default_init(disp, ...)` | Same API, slightly different params |

**Tip:** If you're using SquareLine Studio, always match its LVGL version with your project. The managed component `esp_lvgl_port` handles version compatibility.

---

## 12. Troubleshooting

| Symptom | Likely Cause | Fix |
|---|---|---|
| Black screen after flash | LDO not initialized | Add LDO3/LDO4 init before `display_init()` |
| Touch not responding | I2C not initialized | Call `i2c_init()` then `touch_init()` before display |
| LVGL crashes / garbled display | Thread safety | Wrap all LVGL calls in `lvgl_port_lock()`/`unlock()` |
| Build fails: component not found | Missing dependency | Check `CMakeLists.txt` REQUIRES list |
| SD card not mounting | Power issue | Ensure LDO4 (3.3V) is initialized |
| Camera image frozen | Wrong core / no ISR | Call `gpio_install_isr_service(0)` and pin task to Core 1 |
| WiFi won't connect | NVS not initialized | Call `nvs_flash_init()` before WiFi setup |
| Audio distortion | Amp enabled during init | Call `set_Audio_ctrl(false)` before `audio_init()` |
| No speaker sound (Arduino) | Speaker on wrong I2S port | Speaker MUST use I2S_NUM_1 (STD mode), not I2S_NUM_0 |
| No mic input (Arduino) | Mic is PDM, not I2S standard | Mic uses I2S_NUM_0 PDM RX (CLK=24, DIN=26), not STD mode |
| Amp enable polarity | NS4168 CTRL is active LOW | GPIO 30: LOW=on, HIGH=off — BSP inverts: `gpio_set_level(CTRL, !state)` |

---

## 13. Quick Reference — BSP API Summary

| Module | Key Functions |
|---|---|
| **bsp_display** | `display_init()`, `set_lcd_blight(brightness)`, `touch_init()`, `touch_read()`, `get_coor(&x, &y, &pressed)` |
| **bsp_i2c** | `i2c_init()` |
| **bsp_extra** | `gpio_extra_init()`, `gpio_extra_set_level(bool)` |
| **bsp_wifi** | `bsp_wifi_init()`, `bsp_wifi_sta_init()`, `bsp_wifi_connect(ssid, pass)`, `bsp_wifi_get_state()` |
| **bsp_uart** | `uart_init()`, `SendData(str)`, `uart_read_bytes()` |
| **bsp_usb** | `usb_init()`, `is_usb_ready()`, `send_hid_mouse_delta(dx, dy)` |
| **Audio** | `audio_ctrl_init()`, `audio_init()`, `set_Audio_ctrl(bool)`, `mic_init()`, `mic_read_to_audio(seconds)` |
| **Camera** | `camera_init()`, `camera_display()`, `camera_refresh()`, `camera_display_refresh()` |
| **SD Card** | `sd_init()`, `get_sd_card_info()`, `write_string_file(path, data)`, `read_string_file(path)` |
| **DHT20** | `dht20_begin()`, `dht20_is_calibrated()`, `dht20_read_data(&data)` |
| **SX1262** | `sx1262_tx_init()`, `send_lora_pack_radio()`, `sx1262_get_tx_counter()` |
| **LVGL port** | `lvgl_port_lock(timeout)`, `lvgl_port_unlock()` |

---

## 14. Resources

- **GitHub Repository:** [Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display](https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen)
- **ESP-IDF Documentation:** [docs.espressif.com](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/)
- **LVGL 8.3 Documentation:** [docs.lvgl.io/8.3](https://docs.lvgl.io/8.3/)
- **SquareLine Studio:** [squareline.io](https://squareline.io/)
- **Elecrow Wiki:** [elecrow.com/wiki](https://www.elecrow.com/wiki/)

---

## 15. Arduino IDE Development (Field-Tested)

The following recipes and findings were discovered through hands-on Arduino development and testing on the CrowPanel Advanced 7" ESP32-P4. These supplement the ESP-IDF recipes above.

### 15.1 — Arduino Board Settings

| Setting | Value |
|---|---|
| **Board** | ESP32P4 Dev Module (via esp32 board package) |
| **Partition Scheme** | Default (1,310,720 bytes max) |
| **Flash Size** | 16MB |
| **PSRAM** | OPI PSRAM |
| **Upload Speed** | 921600 |

### 15.2 — Display + Touch Initialization (Arduino/LVGL 9)

The CrowPanel uses MIPI DSI via the `esp_display_panel` library. Touch is GT911 over I2C. This is the proven boilerplate:

```cpp
#include <esp_display_panel.hpp>
#include <esp_lcd_panel_ops.h>
#include <lvgl.h>

using namespace esp_panel::drivers;

#define LCD_WIDTH 1024
#define LCD_HEIGHT 600
#define LCD_DSI_LANE_NUM 2
#define LCD_DSI_LANE_RATE 1000
#define LCD_DPI_CLK_MHZ 52
#define LCD_COLOR_BITS ESP_PANEL_LCD_COLOR_BITS_RGB565
#define LCD_DPI_HPW 10
#define LCD_DPI_HBP 160
#define LCD_DPI_HFP 160
#define LCD_DPI_VPW 1
#define LCD_DPI_VBP 23
#define LCD_DPI_VFP 12
#define LCD_DSI_PHY_LDO_ID 3
#define LCD_RST_IO -1
#define LCD_BL_IO 31
#define LCD_BL_ON_LEVEL 1
#define TOUCH_I2C_SDA 45
#define TOUCH_I2C_SCL 46
#define TOUCH_I2C_FREQ (400 * 1000)
#define TOUCH_RST_IO 40
#define TOUCH_INT_IO 42
#define LVGL_BUF_LINES 60

// Display init
BusDSI *bus = new BusDSI(
  LCD_DSI_LANE_NUM, LCD_DSI_LANE_RATE,
  LCD_DPI_CLK_MHZ, LCD_COLOR_BITS,
  LCD_WIDTH, LCD_HEIGHT,
  LCD_DPI_HPW, LCD_DPI_HBP, LCD_DPI_HFP,
  LCD_DPI_VPW, LCD_DPI_VBP, LCD_DPI_VFP,
  LCD_DSI_PHY_LDO_ID);
bus->configDpiFrameBufferNumber(1);
bus->begin();

LCD_EK79007 *lcd = new LCD_EK79007(bus, LCD_WIDTH, LCD_HEIGHT, LCD_COLOR_BITS, LCD_RST_IO);
lcd->begin();

BacklightPWM_LEDC *bl = new BacklightPWM_LEDC(LCD_BL_IO, LCD_BL_ON_LEVEL);
bl->begin(); bl->on();

// Touch init
BusI2C *touch_bus = new BusI2C(
  TOUCH_I2C_SCL, TOUCH_I2C_SDA,
  (BusI2C::ControlPanelFullConfig)ESP_PANEL_TOUCH_I2C_CONTROL_PANEL_CONFIG(GT911));
touch_bus->configI2C_FreqHz(TOUCH_I2C_FREQ);
touch_bus->configI2C_PullupEnable(true, true);
TouchGT911 *touch = new TouchGT911(touch_bus, LCD_WIDTH, LCD_HEIGHT, TOUCH_RST_IO, TOUCH_INT_IO);
touch->begin();

// LVGL init
lv_init();
lv_tick_set_cb((lv_tick_get_cb_t)millis);
size_t buf_size = LCD_WIDTH * LVGL_BUF_LINES * sizeof(lv_color_t);
uint8_t *buf1 = (uint8_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
lv_display_t *disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
lv_display_set_flush_cb(disp, flush_cb);
lv_display_set_buffers(disp, buf1, NULL, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
```

**Key Takeaway:** Uses LVGL 9.x API (`lv_button_create`, `lv_display_create`, etc.). The LVGL buffer is allocated from PSRAM. Call `lv_timer_handler()` in `loop()`.

### 15.3 — Critical Arduino Gotchas

| Issue | Symptom | Root Cause | Fix |
|---|---|---|---|
| **SPI.h crashes** | ESP32-P4 reboots before `setup()` runs | Static initializer conflict in SPI library | Do NOT `#include <SPI.h>`. Use bit-bang SPI instead. |
| **ETH.h crashes** | Immediate reboot | Same static initializer issue | Do NOT `#include <ETH.h>`. Use raw W5500 register access. |
| **Ethernet.h crashes** | "no memory for frame buffer" | Library consumes too much RAM | Do NOT use Arduino Ethernet library. |
| **SD_MMC.begin() kills display** | Screen goes dark after SD init | LDO4 init inside SD_MMC disrupts DSI PHY LDO3 | **Init SD_MMC BEFORE display init.** |
| **Touch I2C errors after SD init** | `panel_io_i2c_rx_buffer: i2c transaction failed` spam | SD_MMC LDO4 init interferes with I2C bus | Init SD_MMC before display/touch. Errors go away. |
| **lv_obj_create() containers** | Display goes dark with complex UI | Possibly DSI overload with partial rendering | Use simple labels directly on `lv_screen_active()`. Avoid nested containers. |
| **LVGL montserrat fonts** | Compile error: undefined font | Fonts not enabled in lv_conf.h | Enable needed fonts: `#define LV_FONT_MONTSERRAT_14 1` etc. |
| **i2c_master.h + BusI2C conflict** | `CONFLICT! driver_ng is not allowed to be used with this old driver` → abort() | BusI2C uses legacy `driver/i2c.h`; including `driver/i2c_master.h` (new API) on the same port crashes | Use legacy API: `#include "driver/i2c.h"` with `i2c_master_write_read_device()` / `i2c_master_write_to_device()`. No bus handle or device registration needed — just use the port number. |
| **Struct return in .ino** | `'MyStruct' does not name a type` on a function returning a struct | Arduino auto-generates forward declarations at file top, before the struct is defined | Split into two functions returning primitives (`const char*`, `uint32_t`) instead of a struct. Or move struct+function to a `.h` file. |

### 15.4 — W5500 Ethernet (Bit-Bang SPI)

The W5500 Ethernet module connects via the **wireless module header** (2x7 pin connector). Since `SPI.h` crashes on ESP32-P4, all communication uses bit-bang SPI.

**Confirmed Pin Mapping (from PCB silkscreen):**

| Function | GPIO | Header Position |
|---|---|---|
| SCK | 6 | Left column, row 4 |
| MOSI | 7 | Left column, row 3 |
| MISO | 8 | Left column, row 2 |
| CS | 53 | Left column, row 1 |
| VCC | 3.3V | Left column, row 5 |
| GND | GND | Left column, row 6 |
| RST | Not connected | — |

**Wireless Header Physical Layout:**

```
Left column (active):     Right column (unused):
  IO53  |  IO54
  IO8   |  IO46
  IO7   |  IO45
  IO6   |  NC
  3V3   |  IO9
  GND   |  IO10
  NC    |  NC
```

**WARNING:** The BSP pin defines (`RADIO_GPIO_CLK=8`, `RADIO_GPIO_MISO=7`, `RADIO_GPIO_MOSI=6`) do NOT match the actual physical wiring. The physical header maps them differently. Always verify with the silkscreen or multimeter.

**Bit-Bang SPI Mode 0 (CPOL=0, CPHA=0):**

```cpp
#define W5_SCK    6
#define W5_CS    53
#define W5_MOSI   7
#define W5_MISO   8

static uint8_t spi_xfer(uint8_t out) {
  uint8_t in = 0;
  for (int i = 7; i >= 0; i--) {
    digitalWrite(W5_MOSI, (out >> i) & 1);
    delayMicroseconds(2);
    digitalWrite(W5_SCK, HIGH);
    delayMicroseconds(2);
    in |= (digitalRead(W5_MISO) << i);
    digitalWrite(W5_SCK, LOW);
    delayMicroseconds(2);
  }
  return in;
}
```

**W5500 Register Access (3-byte header):**

```cpp
static uint8_t w5_read(uint16_t addr, uint8_t bsb) {
  uint8_t ctrl = (bsb << 3) | 0x00;  // Read
  digitalWrite(W5_CS, LOW);
  delayMicroseconds(5);
  spi_xfer((uint8_t)(addr >> 8));
  spi_xfer((uint8_t)(addr & 0xFF));
  spi_xfer(ctrl);
  uint8_t val = spi_xfer(0x00);
  delayMicroseconds(2);
  digitalWrite(W5_CS, HIGH);
  delayMicroseconds(5);
  return val;
}

static void w5_write(uint16_t addr, uint8_t bsb, uint8_t val) {
  uint8_t ctrl = (bsb << 3) | 0x04;  // Write
  digitalWrite(W5_CS, LOW);
  delayMicroseconds(5);
  spi_xfer((uint8_t)(addr >> 8));
  spi_xfer((uint8_t)(addr & 0xFF));
  spi_xfer(ctrl);
  spi_xfer(val);
  delayMicroseconds(2);
  digitalWrite(W5_CS, HIGH);
  delayMicroseconds(5);
}
```

**Warm-boot SPI recovery:** After soft reset, W5500 may be mid-transaction. Send 128 flush clocks with CS HIGH before any communication.

**DHCP:** Implemented via W5500 UDP sockets (socket 0, port 68). Flow: DISCOVER → OFFER → REQUEST → ACK. Static fallback: 192.168.1.200.

**Verification:** Read register 0x0039 (VERSIONR) — should return 0x04. PHY register 0x002E bit 0 = link status.

### 15.5 — SD Card (SD_MMC Library)

The built-in SD card slot uses the ESP32-P4 SDMMC peripheral.

**Confirmed Configuration:**

| Setting | Value |
|---|---|
| **Mode** | SD_MMC 1-bit |
| **CLK** | GPIO 43 |
| **CMD** | GPIO 44 |
| **D0** | GPIO 39 |
| **Power** | LDO4 (3.3V) — handled internally by `SD_MMC.begin()` |

**CRITICAL Init Order:** SD_MMC must be initialized BEFORE the display. `SD_MMC.begin()` acquires LDO4 internally, which disrupts the DSI display if the panel is already running. This also fixes touch I2C errors.

```cpp
void setup() {
  Serial.begin(115200);

  // 1. SD card FIRST (before display!)
  SD_MMC.setPins(43, 44, 39);
  SD_MMC.begin("/sdcard", true);  // true = 1-bit mode
  delay(100);  // Let LDO settle

  // 2. Display + touch AFTER SD
  init_hardware();  // DSI, backlight, touch, LVGL
  build_ui();
  lv_timer_handler();
}
```

**Do NOT acquire LDO4 manually** — `SD_MMC.begin()` handles it internally. Calling `esp_ldo_acquire_channel()` for LDO4 yourself will cause `SD_MMC.begin()` to fail with "can't acquire the channel."

**4-bit mode conflicts with touch:** Default SDMMC 4-bit uses GPIO 40 (D2) and 42 (D3), which are Touch RST and Touch INT. Use 1-bit mode to avoid this conflict.

### 15.6 — Audio: Speaker (I2S Standard TX)

The CrowPanel uses an **NS4168** mono Class-D audio amplifier with I2S input.

**Confirmed Configuration:**

| Setting | Value |
|---|---|
| **I2S Port** | I2S_NUM_1 |
| **Mode** | I2S Standard (MSB/Philips) TX |
| **BCLK** | GPIO 22 |
| **LRCLK (WS)** | GPIO 21 |
| **DOUT (SDATA)** | GPIO 23 |
| **Amp Enable** | GPIO 30 (**active LOW** — set LOW to enable, HIGH to disable) |
| **Sample Rate** | 16,000 Hz |
| **Bit Depth** | 16-bit |
| **Channels** | Stereo (L+R interleaved) |

**Key Points:**
- Use ESP-IDF `driver/i2s_std.h` directly — Arduino I2S wrapper may crash on ESP32-P4.
- The amp MUST be disabled during I2S init to avoid pop/distortion: `gpio_set_level(GPIO_NUM_30, 1)` (HIGH = off).
- After enabling the channel, set amp on: `gpio_set_level(GPIO_NUM_30, 0)` (LOW = on).
- The BSP `set_Audio_ctrl(state)` function uses inverted logic: `gpio_set_level(CTRL, !state)`.

```cpp
#include "driver/i2s_std.h"

i2s_chan_handle_t tx_chan = NULL;

bool init_speaker() {
  i2s_chan_config_t chan_cfg = {
    .id = I2S_NUM_1,  // Speaker on port 1
    .role = I2S_ROLE_MASTER,
    .dma_desc_num = 6, .dma_frame_num = 256,
    .auto_clear_after_cb = true, .auto_clear_before_cb = false,
    .intr_priority = 0,
  };
  i2s_new_channel(&chan_cfg, &tx_chan, NULL);

  i2s_std_config_t std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
    .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
                    I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = GPIO_NUM_22, .ws = GPIO_NUM_21,
      .dout = GPIO_NUM_23, .din = I2S_GPIO_UNUSED,
      .invert_flags = { false, false, false },
    },
  };
  i2s_channel_init_std_mode(tx_chan, &std_cfg);
  i2s_channel_enable(tx_chan);
  return true;
}
```

### 15.7 — Audio: Microphone (I2S PDM RX)

The CrowPanel has dual digital MEMS microphones using **PDM (Pulse Density Modulation)** mode.

**Confirmed Configuration:**

| Setting | Value |
|---|---|
| **I2S Port** | I2S_NUM_0 |
| **Mode** | I2S PDM RX |
| **CLK** | GPIO 24 |
| **DIN (SDIN2)** | GPIO 26 |
| **Sample Rate** | 16,000 Hz |
| **Bit Depth** | 16-bit |
| **Channels** | Mono (left slot) |
| **Downsample** | I2S_PDM_DSR_8S |

**CRITICAL:** The mic uses a **different I2S port** (NUM_0) than the speaker (NUM_1). They cannot share a port because they use different modes (PDM vs Standard).

```cpp
#include "driver/i2s_pdm.h"

i2s_chan_handle_t rx_chan = NULL;

bool init_mic() {
  i2s_chan_config_t rx_cfg = {
    .id = I2S_NUM_0,  // Mic on port 0
    .role = I2S_ROLE_MASTER,
    .dma_desc_num = 6, .dma_frame_num = 256,
    .auto_clear_after_cb = true, .auto_clear_before_cb = true,
    .intr_priority = 0,
  };
  i2s_new_channel(&rx_cfg, NULL, &rx_chan);

  i2s_pdm_rx_config_t pdm_cfg = {
    .clk_cfg = {
      .sample_rate_hz = 16000,
      .clk_src = I2S_CLK_SRC_DEFAULT,
      .mclk_multiple = I2S_MCLK_MULTIPLE_256,
      .dn_sample_mode = I2S_PDM_DSR_8S,
      .bclk_div = 8,
    },
    .slot_cfg = {
      .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
      .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
      .slot_mode = I2S_SLOT_MODE_MONO,
      .slot_mask = I2S_PDM_SLOT_LEFT,
      .hp_en = true,
      .hp_cut_off_freq_hz = 35.5,
      .amplify_num = 1,
    },
    .gpio_cfg = {
      .clk = GPIO_NUM_24, .din = GPIO_NUM_26,
      .invert_flags = { .clk_inv = false },
    },
  };
  i2s_channel_init_pdm_rx_mode(rx_chan, &pdm_cfg);
  i2s_channel_enable(rx_chan);
  return true;
}
```

**Record & Playback pattern:** Record mono from mic, amplify x10, duplicate to stereo L+R for playback:

```cpp
// Record
int16_t chunk[256];
size_t br;
i2s_channel_read(rx_chan, chunk, sizeof(chunk), &br, 500);

// Playback (mono → stereo, amplified)
int16_t stereo[512];
for (int i = 0; i < 256; i++) {
  int32_t s = (int32_t)mono_buf[i] * 10;  // Volume boost
  if (s > 32767) s = 32767;
  if (s < -32767) s = -32767;
  stereo[i*2] = stereo[i*2+1] = (int16_t)s;
}
i2s_channel_write(tx_chan, stereo, sizeof(stereo), &bw, 200);
```

### 15.8 — Peripheral Init Order

The correct initialization sequence for Arduino sketches that use multiple peripherals:

```
1. Serial.begin(115200)
2. SD_MMC.begin()          ← MUST be before display (LDO4 issue)
3. Display DSI bus init
4. LCD panel init (EK79007)
5. Backlight init
6. Touch I2C + GT911 init
7. LVGL init + buffers
8. Amp GPIO init (OFF)     ← Before I2S to avoid pop
9. Speaker I2S_NUM_1 init  ← Can be before or after display
10. Mic I2S_NUM_0 init     ← Can be before or after display
11. W5500 GPIO init        ← Can be before or after display
12. Build LVGL UI
13. lv_timer_handler() in loop()
```

### 15.9 — BLE (Bluetooth Low Energy)

BLE works on the ESP32-P4 through the **ESP32-C6 co-processor** via SDIO, using the standard Arduino `BLEDevice` library. Despite the P4 having no native Bluetooth hardware, the hosted HCI transport layer routes BLE commands to the C6 transparently.

**Confirmed working:** `BLEDevice::init()`, `BLEScan`, advertised device callbacks, RSSI readings.

```cpp
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

BLEScan *pBLEScan = NULL;

void setup() {
  BLEDevice::init("CrowPanel-P4");      // Works! Routes to C6
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  BLEScanResults *results = pBLEScan->start(5, false);  // 5-second scan
  pBLEScan->clearResults();
}
```

**Note:** Earlier versions of the Arduino-ESP32 core did NOT support BLE on P4. If `BLEDevice::init()` crashes, update to a newer core version (3.x+).

### 15.10 — ESP-NOW (NOT SUPPORTED)

ESP-NOW does **not** work on the ESP32-P4. While the header `<esp_now.h>` is present (included via `esp_wifi_remote`), the actual function implementations are missing — linking fails with `undefined reference` to `esp_now_init`, `esp_now_send`, `esp_now_register_send_cb`, etc.

**Root cause:** The ESP32-P4 has no native WiFi. WiFi is proxied to the ESP32-C6 co-processor via SDIO through the `esp_wifi_remote` component. This proxy layer only implements basic WiFi APIs (STA/AP, TCP/IP). ESP-NOW protocol tunneling was never added.

**Why BLE works but ESP-NOW doesn't:** BLE uses a different transport path (hosted HCI over SDIO) that fully supports the BLE protocol stack. ESP-NOW would require extending the `esp_wifi_remote` RPC protocol, which Espressif has not done.

**Alternatives for peer-to-peer communication:**

- **BLE** — Already confirmed working. Good for low-bandwidth, low-power P2P.
- **WiFi UDP broadcast** — Use standard `WiFiUDP` with broadcast address. Requires a router/AP but uses fully supported WiFi APIs.
- **Community C6 firmware** — An experimental project (`esp32-p4-c6-espnow-enabler` on GitHub) flashes custom firmware to the C6 that adds ESP-NOW support. Not officially supported.

### 15.11 — Working Arduino Sketches Summary

| Sketch | Key Features | Status |
|---|---|---|
| LED Control | GPIO 48 blink | Working |
| WiFi Scanner | ESP32-C6 co-processor SDIO WiFi scan, LVGL list | Working |
| Hello World | Basic LVGL label on DSI display | Working |
| LVGL Hello | Touch-responsive LVGL with fonts, styles | Working |
| NTP Clock | WiFi + NTP time sync, timezone selector | Working |
| W5500 Ethernet | Bit-bang SPI, DHCP, link monitoring, LVGL status | Working |
| SD Card Browser | SD_MMC 1-bit, touch file browser, file preview | Working |
| Sound Test | I2S STD TX, tone generator, WAV playback from SD | Working |
| Mic + Speaker | PDM mic record, I2S playback, live level meter | Working |
| BLE Scanner | BLEDevice via C6 co-processor, device list, RSSI | Working |
| USB HID Mouse | USB 2.0 HID mouse emulation, on-screen controls | Working |
| DHT11 | Software I2C (IO3/IO4), temp + humidity, LVGL gauge | Working |
| MPU6050 | **Hardware I2C1** (shared with GT911), legacy API, 6-axis motion, spirit level | Working (requires legacy `driver/i2c.h` API) |
| I2C Scanner | Software I2C (IO3/IO4), scans for devices on bus | Working |
| BME280/BMP280 | **Hardware I2C1** (shared with GT911), legacy API, auto-detects BME280 vs BMP280, temp/hum/pressure/alt | Working (requires legacy `driver/i2c.h` API) |
| GY-30 (BH1750) | **Hardware I2C1** (shared with GT911), legacy API, lux + foot-candles, log-scale bar, min/max tracking | Working (requires legacy `driver/i2c.h` API) |
| ESP-NOW | — | NOT SUPPORTED on ESP32-P4 (no WiFi on main SoC) |

---

*This cookbook was compiled from the official Elecrow example code repository, factory source code, and hands-on Arduino development and testing. IDF-code examples use ESP-IDF 5.4.x with LVGL 8.3.11; the SquareLine Studio example uses LVGL 9.2.2 with SquareLine Studio 1.5.1. Arduino sections use the esp32 board package with LVGL 9.x.*
