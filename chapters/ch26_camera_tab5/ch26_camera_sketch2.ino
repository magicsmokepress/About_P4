#include <M5Unified.h>

extern "C" {
#include "esp_heap_caps.h"
#include "esp_ldo_regulator.h"
#include "esp_cache.h"
#include "driver/isp.h"
#include "driver/ledc.h"
#include "esp_cam_ctlr_csi.h"
#include "esp_cam_ctlr.h"
}

// ─── Camera config ────────────────────────────────────────
#define CAM_ADDR      0x36
#define CAM_H         1280
#define CAM_V         720
#define DISP_H        640
#define DISP_V        360

// ─── Globals ──────────────────────────────────────────────
static esp_cam_ctlr_handle_t cam_handle = NULL;
static uint8_t *raw_buf = NULL;     // RAW8 from sensor
static uint16_t *rgb_buf = NULL;    // RGB565 for display
static size_t raw_size = CAM_H * CAM_V;
static volatile bool frame_ready = false;

// ─── SCCB register access via Wire ───────────────────────
static void cam_write_reg(uint16_t reg, uint8_t val) {
    Wire.beginTransmission(CAM_ADDR);
    Wire.write((reg >> 8) & 0xFF);
    Wire.write(reg & 0xFF);
    Wire.write(val);
    Wire.endTransmission(true);
}

static uint8_t cam_read_reg(uint16_t reg) {
    Wire.beginTransmission(CAM_ADDR);
    Wire.write((reg >> 8) & 0xFF);
    Wire.write(reg & 0xFF);
    Wire.endTransmission(true);
    Wire.requestFrom((uint8_t)CAM_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0xFF;
}

// ─── SC202CS register table (720p RAW8 30fps, 1-lane) ────
// From esp_cam_sensor/sensors/sc202cs (Apache 2.0)
struct RegVal { uint16_t reg; uint8_t val; };
static const RegVal sc202cs_regs[] = {
    {0x0103,0x01},{0x0100,0x00},
    {0x36e9,0x80},{0x36ea,0x06},{0x36eb,0x0a},{0x36ec,0x01},
    {0x36ed,0x18},{0x36e9,0x24},{0x301f,0x18},{0x3031,0x08},
    {0x3037,0x00},{0x3200,0x00},{0x3201,0xa0},{0x3202,0x00},
    {0x3203,0xf0},{0x3204,0x05},{0x3205,0xa7},{0x3206,0x03},
    {0x3207,0xc7},{0x3208,0x05},{0x3209,0x00},{0x320a,0x02},
    {0x320b,0xd0},{0x3210,0x00},{0x3211,0x04},{0x3212,0x00},
    {0x3213,0x04},{0x3301,0xff},{0x3304,0x68},{0x3306,0x40},
    {0x3308,0x08},{0x3309,0xa8},{0x330b,0xd0},{0x330c,0x18},
    {0x330d,0xff},{0x330e,0x20},{0x331e,0x59},{0x331f,0x99},
    {0x3333,0x10},{0x335e,0x06},{0x335f,0x08},{0x3364,0x1f},
    {0x337c,0x02},{0x337d,0x0a},{0x338f,0xa0},{0x3390,0x01},
    {0x3391,0x03},{0x3392,0x1f},{0x3393,0xff},{0x3394,0xff},
    {0x3395,0xff},{0x33a2,0x04},{0x33ad,0x0c},{0x33b1,0x20},
    {0x33b3,0x38},{0x33f9,0x40},{0x33fb,0x48},{0x33fc,0x0f},
    {0x33fd,0x1f},{0x349f,0x03},{0x34a6,0x03},{0x34a7,0x1f},
    {0x34a8,0x38},{0x34a9,0x30},{0x34ab,0xd0},{0x34ad,0xd8},
    {0x34f8,0x1f},{0x34f9,0x20},{0x3630,0xa0},{0x3631,0x92},
    {0x3632,0x64},{0x3633,0x43},{0x3637,0x49},{0x363a,0x85},
    {0x363c,0x0f},{0x3650,0x31},{0x3670,0x0d},{0x3674,0xc0},
    {0x3675,0xa0},{0x3676,0xa0},{0x3677,0x92},{0x3678,0x96},
    {0x3679,0x9a},{0x367c,0x03},{0x367d,0x0f},{0x367e,0x01},
    {0x367f,0x0f},{0x3698,0x83},{0x3699,0x86},{0x369a,0x8c},
    {0x369b,0x94},{0x36a2,0x01},{0x36a3,0x03},{0x36a4,0x07},
    {0x36ae,0x0f},{0x36af,0x1f},{0x36bd,0x22},{0x36be,0x22},
    {0x36bf,0x22},{0x36d0,0x01},{0x370f,0x02},{0x3721,0x6c},
    {0x3722,0x8d},{0x3725,0xc5},{0x3727,0x14},{0x3728,0x04},
    {0x37b7,0x04},{0x37b8,0x04},{0x37b9,0x06},{0x37bd,0x07},
    {0x37be,0x0f},{0x3901,0x02},{0x3903,0x40},{0x3905,0x8d},
    {0x3907,0x00},{0x3908,0x41},{0x391f,0x41},{0x3933,0x80},
    {0x3934,0x02},{0x3937,0x6f},{0x393a,0x01},{0x393d,0x01},
    {0x393e,0xc0},{0x39dd,0x41},{0x3e00,0x00},{0x3e01,0xC0},
    {0x3e02,0x00},{0x3e09,0x10},{0x4509,0x28},{0x450d,0x61},
    {0, 0}
};

// ─── Software Bayer demosaic (BGGR → RGB565) ─────────────
static void bayer_to_rgb565(const uint8_t *raw, uint16_t *rgb,
                             int src_w, int src_h) {
    int dst_w = src_w / 2;
    int dst_h = src_h / 2;
    for (int dy = 0; dy < dst_h; dy++) {
        int sy = dy * 2;
        const uint8_t *row0 = &raw[sy * src_w];
        const uint8_t *row1 = &raw[(sy + 1) * src_w];
        uint16_t *out = &rgb[dy * dst_w];
        for (int dx = 0; dx < dst_w; dx++) {
            int sx = dx * 2;
            uint8_t b_raw = row0[sx];
            uint8_t g_raw = (row0[sx + 1] + row1[sx]) >> 1;
            uint8_t r_raw = row1[sx + 1];
            // White balance: R×1.8, G×1.0, B×1.6
            int r = min(255, (r_raw * 18) / 10);
            int g = g_raw;
            int b = min(255, (b_raw * 16) / 10);
            out[dx] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        }
    }
}

// ─── CSI callbacks ───────────────────────────────────────
static bool IRAM_ATTR on_new_trans(
    esp_cam_ctlr_handle_t h, esp_cam_ctlr_trans_t *t, void *ud) {
    t->buffer = raw_buf;
    t->buflen = raw_size;
    return false;
}

static bool IRAM_ATTR on_trans_done(
    esp_cam_ctlr_handle_t h, esp_cam_ctlr_trans_t *t, void *ud) {
    frame_ready = true;
    return false;
}

// ─── Setup ───────────────────────────────────────────────
void setup() {
    // 1. M5.begin() handles: display (ST7123), touch, IO expanders, Wire
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(1);       // Landscape
    M5.Display.setSwapBytes(true);   // Correct RGB565 byte order
    M5.Display.fillScreen(TFT_BLACK);
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n=== Tab5 Camera Viewfinder ===");
    Serial.println("SC202CS 1280x720 RAW8 → Software Demosaic → 640x360 Display\n");

    // 2. LDO3 for MIPI PHY
    esp_ldo_channel_handle_t ldo = NULL;
    esp_ldo_channel_config_t ldo_cfg = {.chan_id = 3, .voltage_mv = 2500};
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_cfg, &ldo));

    // 3. XCLK 24 MHz on GPIO 36 (must be before I2C probe)
    ledc_timer_config_t t_cfg = {};
    t_cfg.duty_resolution = LEDC_TIMER_1_BIT;
    t_cfg.freq_hz = 24000000;
    t_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    t_cfg.clk_cfg = LEDC_AUTO_CLK;
    t_cfg.timer_num = LEDC_TIMER_0;
    ledc_timer_config(&t_cfg);
    ledc_channel_config_t c_cfg = {};
    c_cfg.gpio_num = 36;
    c_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    c_cfg.channel = LEDC_CHANNEL_0;
    c_cfg.timer_sel = LEDC_TIMER_0;
    c_cfg.duty = 1;
    c_cfg.sleep_mode = LEDC_SLEEP_MODE_KEEP_ALIVE;
    ledc_channel_config(&c_cfg);
    delay(100);

    // 4. Sensor init via Wire (DO NOT call Wire.end()!)
    Wire.begin(31, 32, 400000);
    delay(50);

    uint8_t id_h = cam_read_reg(0x3107);
    uint8_t id_l = cam_read_reg(0x3108);
    Serial.printf("[CAM] Chip ID: 0x%02X%02X (expect 0xEB52)\n", id_h, id_l);

    // Write register table (exposure included, stream-on is LAST)
    for (int i = 0; sc202cs_regs[i].reg != 0; i++) {
        cam_write_reg(sc202cs_regs[i].reg, sc202cs_regs[i].val);
        if (sc202cs_regs[i].reg == 0x0103) delay(50);
    }
    Serial.println("[CAM] Registers written");

    // 5. Allocate buffers (64-byte aligned for DMA)
    raw_buf = (uint8_t *)heap_caps_aligned_alloc(64, raw_size,
        MALLOC_CAP_SPIRAM);
    rgb_buf = (uint16_t *)heap_caps_malloc(DISP_H * DISP_V * 2,
        MALLOC_CAP_SPIRAM);

    // 6. ISP - RAW8 passthrough (hardware demosaic doesn't work in Arduino)
    isp_proc_handle_t isp = NULL;
    esp_isp_processor_cfg_t isp_cfg = {};
    isp_cfg.clk_hz = 120000000;
    isp_cfg.input_data_source = ISP_INPUT_DATA_SOURCE_CSI;
    isp_cfg.input_data_color_type = ISP_COLOR_RAW8;
    isp_cfg.output_data_color_type = ISP_COLOR_RAW8;  // Passthrough!
    isp_cfg.h_res = CAM_H;
    isp_cfg.v_res = CAM_V;
    ESP_ERROR_CHECK(esp_isp_new_processor(&isp_cfg, &isp));
    ESP_ERROR_CHECK(esp_isp_enable(isp));

    // 7. CSI - 1 lane, 400 Mbps, RAW8 passthrough
    esp_cam_ctlr_csi_config_t csi_cfg = {};
    csi_cfg.ctlr_id = 0;
    csi_cfg.h_res = CAM_H;
    csi_cfg.v_res = CAM_V;
    csi_cfg.lane_bit_rate_mbps = 400;
    csi_cfg.input_data_color_type = CAM_CTLR_COLOR_RAW8;
    csi_cfg.output_data_color_type = CAM_CTLR_COLOR_RAW8;
    csi_cfg.data_lane_num = 1;
    csi_cfg.byte_swap_en = false;
    csi_cfg.queue_items = 1;
    ESP_ERROR_CHECK(esp_cam_new_csi_ctlr(&csi_cfg, &cam_handle));

    esp_cam_ctlr_evt_cbs_t cbs = {};
    cbs.on_get_new_trans = on_new_trans;
    cbs.on_trans_finished = on_trans_done;
    esp_cam_ctlr_trans_t trans = {.buffer = raw_buf, .buflen = raw_size};
    ESP_ERROR_CHECK(esp_cam_ctlr_register_event_callbacks(
        cam_handle, &cbs, &trans));
    ESP_ERROR_CHECK(esp_cam_ctlr_enable(cam_handle));
    ESP_ERROR_CHECK(esp_cam_ctlr_start(cam_handle));

    // 8. Start streaming (MUST be last register write)
    cam_write_reg(0x0100, 0x01);
    Serial.println("[CAM] Streaming!\n");
}

// ─── Loop ────────────────────────────────────────────────
void loop() {
    static uint32_t frames = 0, last_fps = 0;

    esp_cam_ctlr_trans_t t = {.buffer = raw_buf, .buflen = raw_size};
    if (esp_cam_ctlr_receive(cam_handle, &t, 100) == ESP_OK) {
        // Demosaic RAW8 → RGB565 at half resolution
        bayer_to_rgb565(raw_buf, rgb_buf, CAM_H, CAM_V);
        // Display
        M5.Display.pushImage(0, 0, DISP_H, DISP_V, rgb_buf);
        frames++;
    }

    if (millis() - last_fps >= 5000) {
        float fps = frames * 1000.0 / (millis() - last_fps);
        Serial.printf("%.1f FPS | PSRAM: %u KB free\n",
            fps, heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
        frames = 0;
        last_fps = millis();
    }
}
