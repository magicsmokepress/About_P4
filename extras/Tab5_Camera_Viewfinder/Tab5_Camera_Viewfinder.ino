/**
 * M5Stack Tab5 — Camera Viewfinder
 * SC202CS (SC2356) 2MP → CSI → ISP → DSI Display
 *
 * CORRECT sensor info (from M5Stack BSP source):
 *   Sensor: SC202CS (marketed as SC2356)
 *   SCCB address: 0x36
 *   SCCB pins: SDA=7, SCL=8 (NOT on internal bus)
 *   XCLK: 24 MHz LEDC on GPIO 36
 *   CSI: 1-lane MIPI (not 2-lane!)
 *   Chip ID: 0xEB52 (regs 0x3107/0x3108)
 *   Output: RAW8 at 1280x720 30fps
 *
 * Board: M5Stack Tab5 (M5Stack board manager)
 * PSRAM: OPI PSRAM
 */

#include <Wire.h>

extern "C" {
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_cache.h"
#include "driver/i2c_master.h"
#include "driver/isp.h"
#include "driver/ledc.h"
#include "esp_cam_ctlr_csi.h"
#include "esp_cam_ctlr.h"

// Declare just the function we need (avoiding conflicting esp_lcd_io_i2c.h)
esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t io,
    int lcd_cmd, const void *param, size_t param_size);
}

// ═══════════════════════════════════════════════════════════
//  SC202CS (SC2356) — correct hardware config
// ═══════════════════════════════════════════════════════════
#define CAM_SCCB_SDA      7     // Camera I2C data
#define CAM_SCCB_SCL      8     // Camera I2C clock
static uint8_t cam_addr = 0x36;  // SC202CS SCCB address (may be overridden)
#define CAM_XCLK_PIN      36    // 24 MHz clock output
#define CAM_XCLK_FREQ     24000000
#define CAM_CHIP_ID       0xEB52

// IO expanders (internal bus SDA=31, SCL=32)
#define INT_I2C_SDA       31
#define INT_I2C_SCL       32
#define IOEXP_ADDR1       0x43
#define IOEXP_ADDR2       0x44

// Camera resolution (720p RAW8)
#define CAM_H_RES         1280
#define CAM_V_RES         720

// SC202CS MIPI CSI — 576 Mbps per lane, 1 lane
// (from esp_cam_sensor driver: mipi_clk = 576000000, lane_num = 1)
#define CSI_LANE_BITRATE  576
#define CSI_DATA_LANES    1

// LDO / display
#define LDO_CHAN_ID       3
#define LDO_VOLTAGE_MV    2500
#define BYTES_PER_PIXEL   2

// ═══════════════════════════════════════════════════════════
//  Globals
// ═══════════════════════════════════════════════════════════
static esp_cam_ctlr_handle_t cam_handle = NULL;
static isp_proc_handle_t isp_handle = NULL;
static esp_lcd_panel_handle_t dsi_panel = NULL;
static esp_lcd_dsi_bus_handle_t dsi_bus = NULL;
static void *frame_buffer = NULL;
static size_t frame_buffer_size = 0;
static esp_cam_ctlr_trans_t cam_trans;

// Track which I2C bus the sensor was found on
int sensor_sda = INT_I2C_SDA;
int sensor_scl = INT_I2C_SCL;

// ═══════════════════════════════════════════════════════════
//  SC202CS register table — 1280x720 RAW8 30fps 1-lane
//  From esp_cam_sensor/sensors/sc202cs (Apache 2.0)
// ═══════════════════════════════════════════════════════════
struct RegVal { uint16_t reg; uint8_t val; };

static const RegVal sc202cs_720p_raw8[] = {
    {0x0103, 0x01}, {0x0100, 0x00},
    {0x36e9, 0x80}, {0x36ea, 0x06},
    {0x36eb, 0x0a}, {0x36ec, 0x01},
    {0x36ed, 0x18}, {0x36e9, 0x24},
    {0x301f, 0x18}, {0x3031, 0x08},
    {0x3037, 0x00}, {0x3200, 0x00},
    {0x3201, 0xa0}, {0x3202, 0x00},
    {0x3203, 0xf0}, {0x3204, 0x05},
    {0x3205, 0xa7}, {0x3206, 0x03},
    {0x3207, 0xc7}, {0x3208, 0x05},
    {0x3209, 0x00}, {0x320a, 0x02},
    {0x320b, 0xd0}, {0x3210, 0x00},
    {0x3211, 0x04}, {0x3212, 0x00},
    {0x3213, 0x04}, {0x3301, 0xff},
    {0x3304, 0x68}, {0x3306, 0x40},
    {0x3308, 0x08}, {0x3309, 0xa8},
    {0x330b, 0xd0}, {0x330c, 0x18},
    {0x330d, 0xff}, {0x330e, 0x20},
    {0x331e, 0x59}, {0x331f, 0x99},
    {0x3333, 0x10}, {0x335e, 0x06},
    {0x335f, 0x08}, {0x3364, 0x1f},
    {0x337c, 0x02}, {0x337d, 0x0a},
    {0x338f, 0xa0}, {0x3390, 0x01},
    {0x3391, 0x03}, {0x3392, 0x1f},
    {0x3393, 0xff}, {0x3394, 0xff},
    {0x3395, 0xff}, {0x33a2, 0x04},
    {0x33ad, 0x0c}, {0x33b1, 0x20},
    {0x33b3, 0x38}, {0x33f9, 0x40},
    {0x33fb, 0x48}, {0x33fc, 0x0f},
    {0x33fd, 0x1f}, {0x349f, 0x03},
    {0x34a6, 0x03}, {0x34a7, 0x1f},
    {0x34a8, 0x38}, {0x34a9, 0x30},
    {0x34ab, 0xd0}, {0x34ad, 0xd8},
    {0x34f8, 0x1f}, {0x34f9, 0x20},
    {0x3630, 0xa0}, {0x3631, 0x92},
    {0x3632, 0x64}, {0x3633, 0x43},
    {0x3637, 0x49}, {0x363a, 0x85},
    {0x363c, 0x0f}, {0x3650, 0x31},
    {0x3670, 0x0d}, {0x3674, 0xc0},
    {0x3675, 0xa0}, {0x3676, 0xa0},
    {0x3677, 0x92}, {0x3678, 0x96},
    {0x3679, 0x9a}, {0x367c, 0x03},
    {0x367d, 0x0f}, {0x367e, 0x01},
    {0x367f, 0x0f}, {0x3698, 0x83},
    {0x3699, 0x86}, {0x369a, 0x8c},
    {0x369b, 0x94}, {0x36a2, 0x01},
    {0x36a3, 0x03}, {0x36a4, 0x07},
    {0x36ae, 0x0f}, {0x36af, 0x1f},
    {0x36bd, 0x22}, {0x36be, 0x22},
    {0x36bf, 0x22}, {0x36d0, 0x01},
    {0x370f, 0x02}, {0x3721, 0x6c},
    {0x3722, 0x8d}, {0x3725, 0xc5},
    {0x3727, 0x14}, {0x3728, 0x04},
    {0x37b7, 0x04}, {0x37b8, 0x04},
    {0x37b9, 0x06}, {0x37bd, 0x07},
    {0x37be, 0x0f}, {0x3901, 0x02},
    {0x3903, 0x40}, {0x3905, 0x8d},
    {0x3907, 0x00}, {0x3908, 0x41},
    {0x391f, 0x41}, {0x3933, 0x80},
    {0x3934, 0x02}, {0x3937, 0x6f},
    {0x393a, 0x01}, {0x393d, 0x01},
    {0x393e, 0xc0}, {0x39dd, 0x41},
    {0x3e00, 0x00}, {0x3e01, 0x4d},
    {0x3e02, 0xc0}, {0x3e09, 0x00},
    {0x4509, 0x28}, {0x450d, 0x61},
    {0x0100, 0x01}, // Start streaming
    {0, 0}
};

// ═══════════════════════════════════════════════════════════
//  SCCB via Wire — STOP between address and data phases
// ═══════════════════════════════════════════════════════════

static void sc202cs_write_reg(uint16_t reg, uint8_t val) {
    Wire.beginTransmission(cam_addr);
    Wire.write((reg >> 8) & 0xFF);
    Wire.write(reg & 0xFF);
    Wire.write(val);
    Wire.endTransmission(true);
}

static uint8_t sc202cs_read_reg(uint16_t reg) {
    Wire.beginTransmission(cam_addr);
    Wire.write((reg >> 8) & 0xFF);
    Wire.write(reg & 0xFF);
    Wire.endTransmission(true);  // STOP for SCCB
    Wire.requestFrom(cam_addr, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0xFF;
}

// ═══════════════════════════════════════════════════════════
//  Camera clock, power, sensor init
// ═══════════════════════════════════════════════════════════

static bool start_camera_clock() {
    ledc_timer_config_t t = {};
    t.duty_resolution = LEDC_TIMER_1_BIT;
    t.freq_hz = CAM_XCLK_FREQ;
    t.speed_mode = LEDC_LOW_SPEED_MODE;
    t.clk_cfg = LEDC_AUTO_CLK;
    t.timer_num = LEDC_TIMER_0;
    if (ledc_timer_config(&t) != ESP_OK) return false;

    ledc_channel_config_t c = {};
    c.gpio_num = CAM_XCLK_PIN;
    c.speed_mode = LEDC_LOW_SPEED_MODE;
    c.channel = LEDC_CHANNEL_0;
    c.timer_sel = LEDC_TIMER_0;
    c.duty = 1;
    c.hpoint = 0;
    c.sleep_mode = LEDC_SLEEP_MODE_KEEP_ALIVE;
    return ledc_channel_config(&c) == ESP_OK;
}

static void read_io_exp(uint8_t addr, uint8_t reg) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom(addr, (uint8_t)1);
    uint8_t val = Wire.available() ? Wire.read() : 0xEE;
    Serial.printf("[PWR]   0x%02X reg 0x%02X = 0x%02X\n", addr, reg, val);
}

static void enable_io_expanders() {
    Wire.begin(INT_I2C_SDA, INT_I2C_SCL, 400000);
    delay(50);

    // First, read the CURRENT state of both expanders
    // to understand what M5Stack's BSP sets at boot
    Serial.println("[PWR] Reading IO expander state:");
    uint8_t addrs[] = {IOEXP_ADDR1, IOEXP_ADDR2};
    for (uint8_t addr : addrs) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() != 0) continue;
        Serial.printf("[PWR] IO Exp 0x%02X:\n", addr);
        read_io_exp(addr, 0x01);  // ID
        read_io_exp(addr, 0x03);  // Direction (0=out, 1=in)
        read_io_exp(addr, 0x05);  // Output state
        read_io_exp(addr, 0x07);  // High-Z
        read_io_exp(addr, 0x0F);  // Input status
    }

    // ══════════════════════════════════════════════════════
    // Replicate EXACT M5Stack BSP init sequence for IO expanders
    // From bsp_io_expander_pi4ioe_init() in m5stack_tab5.c
    // ══════════════════════════════════════════════════════

    // ── IO Expander 0x43 ──
    // P1=SPK_EN, P2=EXT5V_EN, P4=LCD_RST, P5=TP_RST, P6=CAM_RST
    Serial.println("\n[PWR] Init IO Exp 0x43 (BSP sequence)...");
    // Chip reset
    Wire.beginTransmission(0x43); Wire.write(0x01); Wire.write(0xFF); Wire.endTransmission();
    delay(10);
    // IO direction: P0-P6 output, P7 input
    Wire.beginTransmission(0x43); Wire.write(0x03); Wire.write(0b01111111); Wire.endTransmission();
    // High-Z: all disabled
    Wire.beginTransmission(0x43); Wire.write(0x07); Wire.write(0b00000000); Wire.endTransmission();
    // Pull-up select
    Wire.beginTransmission(0x43); Wire.write(0x0D); Wire.write(0b01111111); Wire.endTransmission();
    // Pull-up enable
    Wire.beginTransmission(0x43); Wire.write(0x0B); Wire.write(0b01111111); Wire.endTransmission();
    // Output: P1,P2,P4,P5,P6 HIGH (SPK,EXT5V,LCD_RST,TP_RST,CAM_RST)
    Wire.beginTransmission(0x43); Wire.write(0x05); Wire.write(0b01110110); Wire.endTransmission();
    Serial.println("[PWR] 0x43: CAM_RST(P6)=HIGH, LCD_RST(P4)=HIGH");

    // ── IO Expander 0x44 ──
    // P0=WLAN_PWR_EN, P3=USB5V_EN, P7=CHG_EN
    Serial.println("[PWR] Init IO Exp 0x44 (BSP sequence)...");
    // Chip reset
    Wire.beginTransmission(0x44); Wire.write(0x01); Wire.write(0xFF); Wire.endTransmission();
    delay(10);
    // IO direction: bits 0,3,4,5,7 output; bits 1,2,6 input
    Wire.beginTransmission(0x44); Wire.write(0x03); Wire.write(0b10111001); Wire.endTransmission();
    // High-Z: bits 1,2 disabled (rest in high-Z)
    Wire.beginTransmission(0x44); Wire.write(0x07); Wire.write(0b00000110); Wire.endTransmission();
    // Pull-up select
    Wire.beginTransmission(0x44); Wire.write(0x0D); Wire.write(0b10111001); Wire.endTransmission();
    // Pull-up enable
    Wire.beginTransmission(0x44); Wire.write(0x0B); Wire.write(0b11111001); Wire.endTransmission();
    // Output: P0(WLAN_PWR_EN), P3(USB5V_EN) HIGH
    Wire.beginTransmission(0x44); Wire.write(0x05); Wire.write(0b00001001); Wire.endTransmission();
    Serial.println("[PWR] 0x44: WLAN_PWR(P0)=HIGH, USB5V(P3)=HIGH");

    delay(300);  // Let peripherals power up

    // Scan for camera
    Serial.println("[PWR] Scanning for camera after BSP init:");
    uint8_t cam_addrs[] = {0x36, 0x30, 0x10, 0x20, 0x21, 0x37};
    for (uint8_t a : cam_addrs) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0)
            Serial.printf("[PWR]   0x%02X ACK!\n", a);
    }

    // Full bus scan
    Serial.println("[PWR] Full bus:");
    for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0)
            Serial.printf("[PWR]   0x%02X\n", a);
    }

    Wire.end();
}

// Phase A: verify sensor and write config registers (but DON'T start streaming)
// Try GPIO 7/8 first (dedicated camera SCCB), fall back to 31/32 (internal bus)
static bool verify_and_configure_sensor() {
    // Try GPIO 7/8 FIRST (dedicated camera SCCB bus per BSP docs)
    // Then fall back to internal bus 31/32
    int bus_pairs[][2] = {{7, 8}, {INT_I2C_SDA, INT_I2C_SCL}, {8, 9}};
    uint8_t addrs[] = {0x36, 0x30, 0x10};
    bool found = false;
    int found_sda = -1, found_scl = -1;
    uint8_t found_addr = 0;

    for (auto &pins : bus_pairs) {
        Wire.end();
        Wire.begin(pins[0], pins[1], 100000);
        delay(50);

        for (uint8_t addr : addrs) {
            Wire.beginTransmission(addr);
            uint8_t err = Wire.endTransmission();
            if (err == 0) {
                Serial.printf("[CAM] Found device at 0x%02X on SDA=%d SCL=%d\n",
                              addr, pins[0], pins[1]);
                found = true;
                found_sda = pins[0];
                found_scl = pins[1];
                found_addr = addr;
                break;
            }
        }
        if (found) break;

        // Full scan on this bus
        Serial.printf("[CAM] SDA=%d SCL=%d:", pins[0], pins[1]);
        int cnt = 0;
        for (uint8_t a = 1; a < 127; a++) {
            Wire.beginTransmission(a);
            if (Wire.endTransmission() == 0) {
                Serial.printf(" 0x%02X", a);
                cnt++;
            }
        }
        if (cnt == 0) Serial.printf(" (empty)");
        Serial.println();
    }

    if (!found) {
        Serial.println("[CAM] SC202CS not found on any bus/address!");
        Wire.end();
        return false;
    }

    // Update globals for read/write functions and later streaming start
    cam_addr = found_addr;
    sensor_sda = found_sda;
    sensor_scl = found_scl;
    Serial.printf("[CAM] Using addr=0x%02X on SDA=%d SCL=%d\n",
                  cam_addr, found_sda, found_scl);

    // Read chip ID
    uint8_t id_h = sc202cs_read_reg(0x3107);
    uint8_t id_l = sc202cs_read_reg(0x3108);
    uint16_t id = (id_h << 8) | id_l;
    Serial.printf("[CAM] Chip ID: 0x%04X (expect 0x%04X)\n",
                  id, CAM_CHIP_ID);

    // Write register table but SKIP the final 0x0100=0x01 (start streaming).
    // We'll start streaming AFTER the CSI receiver is ready.
    int count = 0;
    for (int i = 0; sc202cs_720p_raw8[i].reg != 0; i++) {
        // Skip the streaming start command — we'll send it later
        if (sc202cs_720p_raw8[i].reg == 0x0100 &&
            sc202cs_720p_raw8[i].val == 0x01) {
            Serial.println("[CAM] Skipping stream start (will do after CSI init)");
            continue;
        }
        sc202cs_write_reg(sc202cs_720p_raw8[i].reg,
                          sc202cs_720p_raw8[i].val);
        if (sc202cs_720p_raw8[i].reg == 0x0103) delay(50);
        if (sc202cs_720p_raw8[i].reg == 0x0100) delay(30);
        count++;
    }
    Serial.printf("[CAM] Wrote %d config registers (not streaming yet)\n", count);

    // Verify NOT streaming
    uint8_t streaming = sc202cs_read_reg(0x0100);
    Serial.printf("[CAM] Reg 0x0100 = 0x%02X (%s)\n",
                  streaming,
                  streaming == 0x01 ? "STREAMING" : "standby — good");

    // Verify a register we wrote
    uint8_t r3208 = sc202cs_read_reg(0x3208);
    Serial.printf("[CAM] Reg 0x3208 = 0x%02X (expect 0x05)\n", r3208);

    Wire.end();
    return true;
}

// ═══════════════════════════════════════════════════════════
//  CSI / ISP / DSI
// ═══════════════════════════════════════════════════════════

static bool IRAM_ATTR on_get_new_trans(
    esp_cam_ctlr_handle_t h, esp_cam_ctlr_trans_t *t, void *ud) {
    t->buffer = frame_buffer;
    t->buflen = frame_buffer_size;
    return false;
}
static bool IRAM_ATTR on_trans_finished(
    esp_cam_ctlr_handle_t h, esp_cam_ctlr_trans_t *t, void *ud) {
    return false;
}

// Send one DBI command to the ILI9881C
static void ili9881c_cmd(esp_lcd_panel_io_handle_t io, uint8_t cmd,
                         const uint8_t *data, size_t len) {
    esp_lcd_panel_io_tx_param(io, cmd, data, len);
}

// Full ILI9881C initialization sequence (176 commands)
// Extracted from esp_lcd_ili9881c component (Apache 2.0)
static void ili9881c_init(esp_lcd_panel_io_handle_t io) {
    Serial.println("[ILI] Sending 176 init commands...");

    // Page 3
    uint8_t p3[] = {0x98, 0x81, 0x03};
    ili9881c_cmd(io, 0xFF, p3, 3);
    uint8_t page3_regs[][2] = {
        {0x01,0x00},{0x02,0x00},{0x03,0x53},{0x04,0x53},{0x05,0x13},
        {0x06,0x04},{0x07,0x02},{0x08,0x02},{0x09,0x00},{0x0a,0x00},
        {0x0b,0x00},{0x0c,0x00},{0x0d,0x00},{0x0e,0x00},{0x0f,0x00},
        {0x10,0x00},{0x11,0x00},{0x12,0x00},{0x13,0x00},{0x14,0x00},
        {0x15,0x00},{0x16,0x00},{0x17,0x00},{0x18,0x00},{0x19,0x00},
        {0x1a,0x00},{0x1b,0x00},{0x1c,0x00},{0x1d,0x00},{0x1e,0xc0},
        {0x1f,0x80},{0x20,0x02},{0x21,0x09},{0x22,0x00},{0x23,0x00},
        {0x24,0x00},{0x25,0x00},{0x26,0x00},{0x27,0x00},{0x28,0x55},
        {0x29,0x03},{0x2a,0x00},{0x2b,0x00},{0x2c,0x00},{0x2d,0x00},
        {0x2e,0x00},{0x2f,0x00},{0x30,0x00},{0x31,0x00},{0x32,0x00},
        {0x33,0x00},{0x34,0x00},{0x35,0x00},{0x36,0x00},{0x37,0x00},
        {0x38,0x3C},{0x39,0x00},{0x3a,0x00},{0x3b,0x00},{0x3c,0x00},
        {0x3d,0x00},{0x3e,0x00},{0x3f,0x00},{0x40,0x00},{0x41,0x00},
        {0x42,0x00},{0x43,0x00},{0x44,0x00},
        {0x50,0x01},{0x51,0x23},{0x52,0x45},{0x53,0x67},{0x54,0x89},
        {0x55,0xab},{0x56,0x01},{0x57,0x23},{0x58,0x45},{0x59,0x67},
        {0x5a,0x89},{0x5b,0xab},{0x5c,0xcd},{0x5d,0xef},{0x5e,0x01},
        {0x5f,0x08},{0x60,0x02},{0x61,0x02},{0x62,0x0A},{0x63,0x15},
        {0x64,0x14},{0x65,0x02},{0x66,0x11},{0x67,0x10},{0x68,0x02},
        {0x69,0x0F},{0x6a,0x0E},{0x6b,0x02},{0x6c,0x0D},{0x6d,0x0C},
        {0x6e,0x06},{0x6f,0x02},{0x70,0x02},{0x71,0x02},{0x72,0x02},
        {0x73,0x02},{0x74,0x02},{0x75,0x06},{0x76,0x02},{0x77,0x02},
        {0x78,0x0A},{0x79,0x15},{0x7a,0x14},{0x7b,0x02},{0x7c,0x10},
        {0x7d,0x11},{0x7e,0x02},{0x7f,0x0C},{0x80,0x0D},{0x81,0x02},
        {0x82,0x0E},{0x83,0x0F},{0x84,0x08},{0x85,0x02},{0x86,0x02},
        {0x87,0x02},{0x88,0x02},{0x89,0x02},{0x8A,0x02},
    };
    for (auto &r : page3_regs) {
        ili9881c_cmd(io, r[0], &r[1], 1);
    }

    // Page 4
    uint8_t p4[] = {0x98, 0x81, 0x04};
    ili9881c_cmd(io, 0xFF, p4, 3);
    uint8_t page4_regs[][2] = {
        {0x6C,0x15},{0x6E,0x30},{0x6F,0x33},{0x8D,0x1F},{0x87,0xBA},
        {0x26,0x76},{0xB2,0xD1},{0x35,0x1F},{0x33,0x14},{0x3A,0xA9},
        {0x3B,0x3D},{0x38,0x01},{0x39,0x00},
    };
    for (auto &r : page4_regs) {
        ili9881c_cmd(io, r[0], &r[1], 1);
    }

    // Page 1 (gamma)
    uint8_t p1[] = {0x98, 0x81, 0x01};
    ili9881c_cmd(io, 0xFF, p1, 3);
    uint8_t page1_regs[][2] = {
        {0x22,0x09},{0x31,0x00},{0x40,0x53},{0x50,0xC0},{0x51,0xC0},
        {0x53,0x47},{0x55,0x46},{0x60,0x28},{0x2E,0xC8},
        {0xA0,0x01},{0xA1,0x10},{0xA2,0x1B},{0xA3,0x0C},{0xA4,0x14},
        {0xA5,0x25},{0xA6,0x1A},{0xA7,0x1D},{0xA8,0x68},{0xA9,0x1B},
        {0xAA,0x26},{0xAB,0x5B},{0xAC,0x1B},{0xAD,0x17},{0xAE,0x4F},
        {0xAF,0x24},{0xB0,0x2A},{0xB1,0x4E},{0xB2,0x5F},{0xB3,0x39},
        {0xC0,0x0F},{0xC1,0x1B},{0xC2,0x27},{0xC3,0x16},{0xC4,0x14},
        {0xC5,0x28},{0xC6,0x1D},{0xC7,0x21},{0xC8,0x6C},{0xC9,0x1B},
        {0xCA,0x26},{0xCB,0x5B},{0xCC,0x1B},{0xCD,0x1B},{0xCE,0x4F},
        {0xCF,0x24},{0xD0,0x2A},{0xD1,0x4E},{0xD2,0x5F},{0xD3,0x39},
    };
    for (auto &r : page1_regs) {
        ili9881c_cmd(io, r[0], &r[1], 1);
    }

    // Page 0 + display on
    uint8_t p0[] = {0x98, 0x81, 0x00};
    ili9881c_cmd(io, 0xFF, p0, 3);
    uint8_t te[] = {0x00};
    ili9881c_cmd(io, 0x35, te, 1);  // Tearing effect line on

    // Sleep out + Display on
    ili9881c_cmd(io, 0x11, NULL, 0);  // Sleep out
    delay(120);
    ili9881c_cmd(io, 0x29, NULL, 0);  // Display on
    delay(20);

    Serial.println("[ILI] Init complete");
}

static bool init_display() {
    esp_lcd_dsi_bus_config_t bus_cfg = {};
    bus_cfg.bus_id = 0;
    bus_cfg.num_data_lanes = 2;
    bus_cfg.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
    bus_cfg.lane_bit_rate_mbps = 500;
    if (esp_lcd_new_dsi_bus(&bus_cfg, &dsi_bus) != ESP_OK) return false;

    // DBI IO for sending init commands to ILI9881C
    esp_lcd_panel_io_handle_t dbi_io = NULL;
    esp_lcd_dbi_io_config_t dbi_cfg = {};
    dbi_cfg.virtual_channel = 0;
    dbi_cfg.lcd_cmd_bits = 8;
    dbi_cfg.lcd_param_bits = 8;
    if (esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &dbi_io) != ESP_OK) {
        Serial.println("[DSI] DBI IO failed");
        return false;
    }

    // Send the full ILI9881C init sequence via DBI
    ili9881c_init(dbi_io);

    esp_lcd_dpi_panel_config_t dpi = {};
    dpi.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    dpi.dpi_clock_freq_mhz = 50;
    dpi.pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565;
    dpi.num_fbs = 1;
    dpi.video_timing.h_size = CAM_H_RES;
    dpi.video_timing.v_size = CAM_V_RES;
    dpi.video_timing.hsync_back_porch = 30;
    dpi.video_timing.hsync_pulse_width = 4;
    dpi.video_timing.hsync_front_porch = 30;
    dpi.video_timing.vsync_back_porch = 16;
    dpi.video_timing.vsync_pulse_width = 2;
    dpi.video_timing.vsync_front_porch = 16;
    if (esp_lcd_new_panel_dpi(dsi_bus, &dpi, &dsi_panel) != ESP_OK) return false;

    esp_lcd_dpi_panel_get_frame_buffer(dsi_panel, 1, &frame_buffer);
    frame_buffer_size = CAM_H_RES * CAM_V_RES * BYTES_PER_PIXEL;
    return true;
}

static bool init_isp() {
    esp_isp_processor_cfg_t c = {};
    c.clk_hz = 120 * 1000 * 1000;  // 120 MHz (skill doc recommends this)
    c.input_data_source = ISP_INPUT_DATA_SOURCE_CSI;
    c.input_data_color_type = ISP_COLOR_RAW8;  // Match register table
    c.output_data_color_type = ISP_COLOR_RGB565;
    c.h_res = CAM_H_RES;
    c.v_res = CAM_V_RES;
    if (esp_isp_new_processor(&c, &isp_handle) != ESP_OK) return false;
    return esp_isp_enable(isp_handle) == ESP_OK;
}

static bool init_csi() {
    esp_cam_ctlr_csi_config_t c = {};
    c.ctlr_id = 0;
    c.h_res = CAM_H_RES;
    c.v_res = CAM_V_RES;
    c.lane_bit_rate_mbps = CSI_LANE_BITRATE;
    c.input_data_color_type = CAM_CTLR_COLOR_RAW8;
    c.output_data_color_type = CAM_CTLR_COLOR_RGB565;
    c.data_lane_num = CSI_DATA_LANES;  // 1 lane!
    c.byte_swap_en = false;
    c.queue_items = 1;
    if (esp_cam_new_csi_ctlr(&c, &cam_handle) != ESP_OK) return false;

    esp_cam_ctlr_evt_cbs_t cbs = {};
    cbs.on_get_new_trans = on_get_new_trans;
    cbs.on_trans_finished = on_trans_finished;
    ESP_ERROR_CHECK(esp_cam_ctlr_register_event_callbacks(cam_handle, &cbs, &cam_trans));
    ESP_ERROR_CHECK(esp_cam_ctlr_enable(cam_handle));
    return true;
}

// ═══════════════════════════════════════════════════════════
//  Setup & Loop
// ═══════════════════════════════════════════════════════════

void setup() {
    Serial.begin(115200);
    delay(3000);

    Serial.println("\n========================================");
    Serial.println("  Tab5 Camera Viewfinder");
    Serial.println("  SC202CS @ 0x36, 1-lane, 720p RAW8");
    Serial.println("========================================\n");

    uint32_t psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    Serial.printf("PSRAM: %u MB\n", psram / (1024 * 1024));

    // 1. LDO — both LDO3 (2.5V for MIPI PHY) and LDO4 (3.3V for peripherals)
    esp_ldo_channel_handle_t ldo3 = NULL;
    esp_ldo_channel_config_t ldo3_cfg = {.chan_id = 3, .voltage_mv = 2500};
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo3_cfg, &ldo3));
    Serial.println("[LDO] LDO3 (2.5V) OK");

    esp_ldo_channel_handle_t ldo4 = NULL;
    esp_ldo_channel_config_t ldo4_cfg = {.chan_id = 4, .voltage_mv = 3300};
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo4_cfg, &ldo4));
    Serial.println("[LDO] LDO4 (3.3V) OK");

    // 2. XCLK FIRST — sensor needs clock before it responds on I2C
    if (!start_camera_clock()) { Serial.println("[XCLK] FAIL"); while(1) delay(1000); }
    Serial.println("[XCLK] 24 MHz on GPIO 36 — OK");
    delay(100);

    // 3. IO expanders — find which pin enables camera power
    // Key fix: HIGH-Z must be disabled (reg 0x07 = 0x00) or outputs don't drive
    enable_io_expanders();
    delay(200);  // Let sensor boot with power + clock

    // 4. Verify sensor and write register table
    if (!verify_and_configure_sensor()) {
        Serial.println("[CAM] SENSOR INIT FAILED");
        while(1) delay(1000);
    }

    // 5. DSI display
    if (!init_display()) { Serial.println("[DSI] FAIL"); while(1) delay(1000); }
    Serial.printf("[DSI] FB=%p, %u bytes\n", frame_buffer, frame_buffer_size);

    // 6. ISP
    if (!init_isp()) { Serial.println("[ISP] FAIL"); while(1) delay(1000); }
    Serial.println("[ISP] RAW8→RGB565 OK");

    // 7. CSI — 1 lane!
    if (!init_csi()) { Serial.println("[CSI] FAIL"); while(1) delay(1000); }
    Serial.printf("[CSI] %dx%d, 1 lane @ %d Mbps\n", CAM_H_RES, CAM_V_RES, CSI_LANE_BITRATE);

    // 8. Start CSI receiver FIRST (before sensor starts streaming)
    esp_lcd_panel_init(dsi_panel);
    memset(frame_buffer, 0xFF, frame_buffer_size);
    esp_cache_msync(frame_buffer, frame_buffer_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

    cam_trans.buffer = frame_buffer;
    cam_trans.buflen = frame_buffer_size;
    ESP_ERROR_CHECK(esp_cam_ctlr_start(cam_handle));
    Serial.println("[CSI] Receiver started, waiting for frames...");

    // 9. NOW start the sensor streaming — CSI is already listening
    // Re-open Wire on whichever bus the sensor was found on
    // (stored from verify_and_configure_sensor)
    extern int sensor_sda, sensor_scl;
    Wire.begin(sensor_sda, sensor_scl, 400000);
    delay(50);
    sc202cs_write_reg(0x0100, 0x01);  // Start streaming!
    uint8_t streaming = sc202cs_read_reg(0x0100);
    Serial.printf("[CAM] Stream start sent. Reg 0x0100 = 0x%02X (%s)\n",
                  streaming,
                  streaming == 0x01 ? "STREAMING!" : "failed");
    Wire.end();

    Serial.println("\n*** CAMERA LIVE ***\n");
}

void loop() {
    static uint32_t frames = 0, last = 0, errors = 0;
    esp_cam_ctlr_trans_t t = {.buffer = frame_buffer, .buflen = frame_buffer_size};
    esp_err_t err = esp_cam_ctlr_receive(cam_handle, &t, 1000);
    if (err == ESP_OK) {
        frames++;
    } else {
        errors++;
        if (errors <= 3) {
            Serial.printf("[CAM] receive error: %s\n", esp_err_to_name(err));
        }
    }

    if (millis() - last >= 5000) {
        Serial.printf("[CAM] %.1f FPS | errors=%u | PSRAM: %u KB\n",
            frames * 1000.0 / (millis() - last), errors,
            heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
        frames = 0; errors = 0; last = millis();
    }
}
