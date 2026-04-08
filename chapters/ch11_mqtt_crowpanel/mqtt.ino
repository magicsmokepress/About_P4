#include <esp_display_panel.hpp>
#include <esp_lcd_panel_ops.h>
#include <lvgl.h>
#include <WiFi.h>
#include <PubSubClient.h>

using namespace esp_panel::drivers;

// ─── Display config (standard boilerplate) ────────────────────────
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
#define LED_GPIO 48

// ─── Wi-Fi + MQTT config ──────────────────────────────────────────
// IMPORTANT: Replace with your own credentials before uploading
#define WIFI_SSID     "YOUR_SSID"
#define WIFI_PASSWORD "YOUR_PASSWORD"
#define MQTT_BROKER   "test.mosquitto.org"  // public test broker
#define MQTT_PORT     1883
#define MQTT_CLIENT   "crowpanel-p4"
#define TOPIC_PUB     "crowpanel/sensors"
#define TOPIC_SUB     "crowpanel/control"
#define TOPIC_LWT     "crowpanel/status"

// ─── Globals ──────────────────────────────────────────────────────
static LCD_EK79007 *g_lcd = NULL;
static TouchGT911 *g_touch = NULL;
static lv_display_t *lvgl_disp = NULL;
static lv_indev_t *lvgl_touch_dev = NULL;
static volatile bool touch_pressed = false;
static volatile int16_t touch_x = 0, touch_y = 0;

WiFiClient espClient;
PubSubClient mqtt(espClient);

static lv_obj_t *wifi_lbl = NULL;
static lv_obj_t *mqtt_lbl = NULL;
static lv_obj_t *pub_lbl = NULL;
static lv_obj_t *sub_lbl = NULL;
static lv_obj_t *count_lbl = NULL;

static uint32_t pub_count = 0;
static uint32_t sub_count = 0;
static uint32_t last_pub = 0;
static uint32_t reconnect_delay = 1000;
static uint32_t last_reconnect = 0;

// (init_hardware() - identical to Chapter 7, omitted for brevity)
// Include the same init_hardware() function from Chapter 7

// ─── MQTT callback ───────────────────────────────────────────────
static void mqtt_callback(char *topic, byte *payload,
                          unsigned int length) {
    char msg[256];
    int len = min((int)length, 255);
    memcpy(msg, payload, len);
    msg[len] = 0;

    Serial.printf("[MQTT] Received on %s: %s\n", topic, msg);
    sub_count++;

    // Handle control commands
    if (strcmp(msg, "led_on") == 0) {
        digitalWrite(LED_GPIO, HIGH);
        if (sub_lbl) lv_label_set_text(sub_lbl,
            "Last: led_on (LED ON)");
    } else if (strcmp(msg, "led_off") == 0) {
        digitalWrite(LED_GPIO, LOW);
        if (sub_lbl) lv_label_set_text(sub_lbl,
            "Last: led_off (LED OFF)");
    } else {
        char buf[300];
        snprintf(buf, sizeof(buf), "Last: %s", msg);
        if (sub_lbl) lv_label_set_text(sub_lbl, buf);
    }
}

// ─── MQTT connect with LWT ──────────────────────────────────────
static bool mqtt_connect() {
    Serial.println("[MQTT] Connecting...");
    if (mqtt_lbl) {
        lv_label_set_text(mqtt_lbl, "MQTT: Connecting...");
        lv_obj_set_style_text_color(mqtt_lbl,
            lv_color_hex(0xFFAA00), 0);
    }

    // Last Will: publish "offline" to status topic if we disconnect
    bool connected = mqtt.connect(MQTT_CLIENT,
        TOPIC_LWT, 1, true, "offline");

    if (connected) {
        Serial.println("[MQTT] Connected");
        mqtt.subscribe(TOPIC_SUB);
        mqtt.publish(TOPIC_LWT, "online", true);  // retained
        reconnect_delay = 1000;  // reset backoff

        if (mqtt_lbl) {
            lv_label_set_text_fmt(mqtt_lbl,
                "MQTT: Connected to %s", MQTT_BROKER);
            lv_obj_set_style_text_color(mqtt_lbl,
                lv_color_hex(0x00E676), 0);
        }
    } else {
        Serial.printf("[MQTT] Failed, rc=%d\n", mqtt.state());
        if (mqtt_lbl) {
            lv_label_set_text_fmt(mqtt_lbl,
                "MQTT: Failed (rc=%d)", mqtt.state());
            lv_obj_set_style_text_color(mqtt_lbl,
                lv_color_hex(0xFF4444), 0);
        }
        // Exponential backoff: 1s, 2s, 4s, 8s, max 30s
        reconnect_delay = min(reconnect_delay * 2, (uint32_t)30000);
    }
    return connected;
}

// ─── Publish sensor data as JSON ────────────────────────────────
static void publish_sensor_data() {
    // Simulated sensor readings (replace with real sensors)
    float temperature = 22.5 + random(-20, 20) / 10.0;
    float humidity = 55.0 + random(-50, 50) / 10.0;
    uint32_t uptime = millis() / 1000;

    char json[256];
    snprintf(json, sizeof(json),
        "{\"temperature\":%.1f,\"humidity\":%.1f,"
        "\"uptime\":%lu,\"rssi\":%d}",
        temperature, humidity, uptime, WiFi.RSSI());

    if (mqtt.publish(TOPIC_PUB, json)) {
        pub_count++;
        Serial.printf("[MQTT] Published: %s\n", json);
        if (pub_lbl) lv_label_set_text_fmt(pub_lbl,
            "Last pub: %.1f°C  %.1f%%", temperature, humidity);
    }

    if (count_lbl)
        lv_label_set_text_fmt(count_lbl,
            "Published: %lu  |  Received: %lu",
            pub_count, sub_count);
}

// ─── UI ───────────────────────────────────────────────────────────
static void build_ui() {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1A1A2E), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, LV_SYMBOL_WIFI " MQTT Dashboard");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    wifi_lbl = lv_label_create(scr);
    lv_obj_set_style_text_font(wifi_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(wifi_lbl, 20, 55);

    mqtt_lbl = lv_label_create(scr);
    lv_obj_set_style_text_font(mqtt_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(mqtt_lbl, 20, 80);

    // Publish section
    lv_obj_t *pub_title = lv_label_create(scr);
    lv_label_set_text_fmt(pub_title,
        "Publishing to: %s", TOPIC_PUB);
    lv_obj_set_style_text_font(pub_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(pub_title,
        lv_color_hex(0x888888), 0);
    lv_obj_set_pos(pub_title, 20, 130);

    pub_lbl = lv_label_create(scr);
    lv_label_set_text(pub_lbl, "Waiting...");
    lv_obj_set_style_text_font(pub_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(pub_lbl,
        lv_color_hex(0x4488FF), 0);
    lv_obj_set_pos(pub_lbl, 20, 155);

    // Subscribe section
    lv_obj_t *sub_title = lv_label_create(scr);
    lv_label_set_text_fmt(sub_title,
        "Subscribed to: %s", TOPIC_SUB);
    lv_obj_set_style_text_font(sub_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sub_title,
        lv_color_hex(0x888888), 0);
    lv_obj_set_pos(sub_title, 20, 210);

    sub_lbl = lv_label_create(scr);
    lv_label_set_text(sub_lbl, "Waiting for messages...");
    lv_obj_set_style_text_font(sub_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(sub_lbl,
        lv_color_hex(0x00E676), 0);
    lv_obj_set_pos(sub_lbl, 20, 235);

    // Message counter
    count_lbl = lv_label_create(scr);
    lv_label_set_text(count_lbl, "Published: 0  |  Received: 0");
    lv_obj_set_style_text_font(count_lbl,
        &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(count_lbl,
        lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(count_lbl, LV_ALIGN_BOTTOM_MID, 0, -20);

    // Instructions
    lv_obj_t *help = lv_label_create(scr);
    lv_label_set_text_fmt(help,
        "Send 'led_on' or 'led_off' to %s to control LED",
        TOPIC_SUB);
    lv_obj_set_style_text_font(help, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(help, lv_color_hex(0x555555), 0);
    lv_obj_align(help, LV_ALIGN_BOTTOM_MID, 0, -50);
}

// ─── Setup ────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("ESP32-P4 MQTT Dashboard");

    pinMode(LED_GPIO, OUTPUT);
    init_hardware();
    build_ui();

    // Connect WiFi
    lv_label_set_text(wifi_lbl, "Wi-Fi: Connecting...");
    lv_obj_set_style_text_color(wifi_lbl,
        lv_color_hex(0xFFAA00), 0);
    lv_timer_handler();

    WiFi.mode(WIFI_STA);
    delay(3000);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        attempts++;
        lv_timer_handler();
    }

    if (WiFi.status() == WL_CONNECTED) {
        lv_label_set_text_fmt(wifi_lbl, "Wi-Fi: %s (%s)",
            WIFI_SSID, WiFi.localIP().toString().c_str());
        lv_obj_set_style_text_color(wifi_lbl,
            lv_color_hex(0x00E676), 0);
    } else {
        lv_label_set_text(wifi_lbl, "Wi-Fi: FAILED");
        lv_obj_set_style_text_color(wifi_lbl,
            lv_color_hex(0xFF4444), 0);
    }

    // Configure MQTT
    mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    mqtt.setCallback(mqtt_callback);
    mqtt.setBufferSize(512);
    mqtt_connect();

    lv_timer_handler();
}

// ─── Loop ─────────────────────────────────────────────────────────
void loop() {
    // Touch polling
    if (g_touch) {
        esp_lcd_touch_handle_t tp = g_touch->getHandle();
        if (tp) {
            uint16_t x[1], y[1], strength[1];
            uint8_t cnt = 0;
            esp_lcd_touch_read_data(tp);
            if (esp_lcd_touch_get_coordinates(tp, x, y, strength,
                    &cnt, 1) && cnt > 0) {
                touch_pressed = true;
                touch_x = x[0];
                touch_y = y[0];
            } else {
                touch_pressed = false;
            }
        }
    }

    // MQTT loop
    if (!mqtt.connected()) {
        if (millis() - last_reconnect > reconnect_delay) {
            last_reconnect = millis();
            mqtt_connect();
        }
    } else {
        mqtt.loop();
    }

    // Publish every 5 seconds
    if (mqtt.connected() && millis() - last_pub > 5000) {
        last_pub = millis();
        publish_sensor_data();
    }

    lv_timer_handler();
    delay(10);
}
