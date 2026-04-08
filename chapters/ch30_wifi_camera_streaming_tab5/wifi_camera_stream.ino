/**
 * ════════════════════════════════════════════════════════════
 *  M5Stack Tab5 - Wi-Fi Camera Stream + Person Detection v12
 *  SC202CS → MIPI-CSI RAW8 → SW Demosaic → Wi-Fi Stream
 *  + TFLite Micro Person Detection (96×96 int8)
 * ════════════════════════════════════════════════════════════
 *
 *  Tab5 AP: "Tab5-Cam" / <your-password> → http://192.168.4.1
 *  Display shows: IP, detection status, FPS, client count
 *  Screen border flashes GREEN on person detection
 *
 *  Boards: M5Stack 3.2.6
 */

#include <Wire.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduTFLite.h>
#include "person_detect_model.h"

extern TfLiteTensor *tflInputTensor;
extern TfLiteTensor *tflOutputTensor;

extern "C" {
#include "esp_heap_caps.h"
#include "esp_ldo_regulator.h"
#include "esp_cache.h"
#include "driver/isp.h"
#include "driver/ledc.h"
#include "esp_cam_ctlr_csi.h"
#include "esp_cam_ctlr.h"
}

// ═══════════════════════════════════════════════════════════
//  Config
// ═══════════════════════════════════════════════════════════
#define AP_SSID       "Tab5-Cam"
#define AP_PASS       "changeme!"   // ← CHANGE THIS to your own password

#define SDIO2_CLK     GPIO_NUM_12
#define SDIO2_CMD     GPIO_NUM_13
#define SDIO2_D0      GPIO_NUM_11
#define SDIO2_D1      GPIO_NUM_10
#define SDIO2_D2      GPIO_NUM_9
#define SDIO2_D3      GPIO_NUM_8
#define SDIO2_RST     GPIO_NUM_15

#define INT_I2C_SDA       31
#define INT_I2C_SCL       32
#define CAM_SCCB_ADDR     0x36
#define CAM_XCLK_PIN      36
#define CAM_XCLK_FREQ     24000000
#define CAM_H_RES         1280
#define CAM_V_RES         720
#define CSI_LANE_BITRATE  400
#define CSI_DATA_LANES    1

// Stream at 160×90 for max FPS over Wi-Fi AP (~28KB per frame)
#define STREAM_W          160
#define STREAM_H          90

#define MODEL_DIM         96

#define AE_TARGET         60
#define AE_TOLERANCE      15
#define AE_MIN_EXP        0x20
#define AE_MAX_EXP        0xF0
#define AE_INTERVAL       90
#define DETECT_INTERVAL   8

// ═══════════════════════════════════════════════════════════
//  Globals
// ═══════════════════════════════════════════════════════════
static esp_cam_ctlr_handle_t cam_handle = NULL;
static isp_proc_handle_t     isp_handle = NULL;

static uint8_t  *raw_buffer    = NULL;
static size_t    raw_buf_size  = 0;
static uint16_t *stream_buffer = NULL;

static volatile uint32_t frame_count = 0;
static volatile bool     new_frame   = false;

static uint8_t current_exposure = 0xC0;

// TFLite
constexpr int kTensorArenaSize = 136 * 1024;
static uint8_t *tensor_arena = NULL;
static bool tflite_ready = false;
static float person_score = 0;
static float no_person_score = 0;
static float avg_person_score = 0;
static bool person_detected = false;

#define AVG_WINDOW 3
static float score_history[AVG_WINDOW] = {0};
static int score_idx = 0;

WebServer server(80);
static float cam_fps = 0;
static uint32_t frame_served = 0;

// Display status
static bool last_detected = false;
static uint32_t last_display_ms = 0;

// ═══════════════════════════════════════════════════════════
//  SC202CS registers (same as Ch26/Ch29)
// ═══════════════════════════════════════════════════════════
struct RegVal { uint16_t reg; uint8_t val; };
static const RegVal sc202cs_init_regs[] = {
    {0x0103, 0x01}, {0x0100, 0x00},
    {0x36e9, 0x80}, {0x36ea, 0x06}, {0x36eb, 0x0a},
    {0x36ec, 0x01}, {0x36ed, 0x18}, {0x36e9, 0x24},
    {0x301f, 0x18}, {0x3031, 0x08}, {0x3037, 0x00},
    {0x3200, 0x00}, {0x3201, 0xa0}, {0x3202, 0x00},
    {0x3203, 0xf0}, {0x3204, 0x05}, {0x3205, 0xa7},
    {0x3206, 0x03}, {0x3207, 0xc7}, {0x3208, 0x05},
    {0x3209, 0x00}, {0x320a, 0x02}, {0x320b, 0xd0},
    {0x3210, 0x00}, {0x3211, 0x04}, {0x3212, 0x00},
    {0x3213, 0x04},
    {0x3301, 0xff}, {0x3304, 0x68}, {0x3306, 0x40},
    {0x3308, 0x08}, {0x3309, 0xa8}, {0x330b, 0xd0},
    {0x330c, 0x18}, {0x330d, 0xff}, {0x330e, 0x20},
    {0x331e, 0x59}, {0x331f, 0x99}, {0x3333, 0x10},
    {0x335e, 0x06}, {0x335f, 0x08}, {0x3364, 0x1f},
    {0x337c, 0x02}, {0x337d, 0x0a}, {0x338f, 0xa0},
    {0x3390, 0x01}, {0x3391, 0x03}, {0x3392, 0x1f},
    {0x3393, 0xff}, {0x3394, 0xff}, {0x3395, 0xff},
    {0x33a2, 0x04}, {0x33ad, 0x0c}, {0x33b1, 0x20},
    {0x33b3, 0x38}, {0x33f9, 0x40}, {0x33fb, 0x48},
    {0x33fc, 0x0f}, {0x33fd, 0x1f},
    {0x349f, 0x03}, {0x34a6, 0x03}, {0x34a7, 0x1f},
    {0x34a8, 0x38}, {0x34a9, 0x30}, {0x34ab, 0xd0},
    {0x34ad, 0xd8}, {0x34f8, 0x1f}, {0x34f9, 0x20},
    {0x3630, 0xa0}, {0x3631, 0x92}, {0x3632, 0x64},
    {0x3633, 0x43}, {0x3637, 0x49}, {0x363a, 0x85},
    {0x363c, 0x0f}, {0x3650, 0x31}, {0x3670, 0x0d},
    {0x3674, 0xc0}, {0x3675, 0xa0}, {0x3676, 0xa0},
    {0x3677, 0x92}, {0x3678, 0x96}, {0x3679, 0x9a},
    {0x367c, 0x03}, {0x367d, 0x0f}, {0x367e, 0x01},
    {0x367f, 0x0f}, {0x3698, 0x83}, {0x3699, 0x86},
    {0x369a, 0x8c}, {0x369b, 0x94},
    {0x36a2, 0x01}, {0x36a3, 0x03}, {0x36a4, 0x07},
    {0x36ae, 0x0f}, {0x36af, 0x1f},
    {0x36bd, 0x22}, {0x36be, 0x22}, {0x36bf, 0x22},
    {0x36d0, 0x01}, {0x370f, 0x02},
    {0x3721, 0x6c}, {0x3722, 0x8d}, {0x3725, 0xc5},
    {0x3727, 0x14}, {0x3728, 0x04},
    {0x37b7, 0x04}, {0x37b8, 0x04}, {0x37b9, 0x06},
    {0x37bd, 0x07}, {0x37be, 0x0f},
    {0x3901, 0x02}, {0x3903, 0x40}, {0x3905, 0x8d},
    {0x3907, 0x00}, {0x3908, 0x41}, {0x391f, 0x41},
    {0x3933, 0x80}, {0x3934, 0x02}, {0x3937, 0x6f},
    {0x393a, 0x01}, {0x393d, 0x01}, {0x393e, 0xc0},
    {0x39dd, 0x41},
    {0x3e00, 0x00}, {0x3e01, 0x4d}, {0x3e02, 0xc0},
    {0x3e09, 0x00},
    {0x4509, 0x28}, {0x450d, 0x61},
    {0, 0}
};

// ═══════════════════════════════════════════════════════════
//  Hardware helpers (compact)
// ═══════════════════════════════════════════════════════════
static void cam_write_reg(uint16_t reg, uint8_t val) {
    Wire.beginTransmission(CAM_SCCB_ADDR);
    Wire.write((reg >> 8) & 0xFF); Wire.write(reg & 0xFF); Wire.write(val);
    Wire.endTransmission(true); delayMicroseconds(100);
}
static uint8_t cam_read_reg(uint16_t reg) {
    Wire.beginTransmission(CAM_SCCB_ADDR);
    Wire.write((reg >> 8) & 0xFF); Wire.write(reg & 0xFF);
    Wire.endTransmission(true);
    Wire.requestFrom((uint8_t)CAM_SCCB_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0xFF;
}
static bool init_xclk() {
    ledc_timer_config_t t = {}; t.duty_resolution = LEDC_TIMER_1_BIT;
    t.freq_hz = CAM_XCLK_FREQ; t.speed_mode = LEDC_LOW_SPEED_MODE;
    t.timer_num = LEDC_TIMER_0; t.clk_cfg = LEDC_AUTO_CLK;
    if (ledc_timer_config(&t) != ESP_OK) return false;
    ledc_channel_config_t c = {}; c.gpio_num = CAM_XCLK_PIN;
    c.speed_mode = LEDC_LOW_SPEED_MODE; c.channel = LEDC_CHANNEL_0;
    c.timer_sel = LEDC_TIMER_0; c.duty = 1; c.hpoint = 0;
    return ledc_channel_config(&c) == ESP_OK;
}
static bool init_sensor() {
    Wire.beginTransmission(CAM_SCCB_ADDR);
    if (Wire.endTransmission() != 0) return false;
    uint16_t id = (cam_read_reg(0x3107) << 8) | cam_read_reg(0x3108);
    Serial.printf("[CAM] ID: 0x%04X\n", id);
    int n = 0;
    for (int i = 0; sc202cs_init_regs[i].reg != 0; i++) {
        cam_write_reg(sc202cs_init_regs[i].reg, sc202cs_init_regs[i].val);
        if (sc202cs_init_regs[i].reg == 0x0103) delay(50);
        if (sc202cs_init_regs[i].reg == 0x0100) delay(30);
        n++;
    }
    Serial.printf("[CAM] %d regs\n", n);
    return true;
}
static bool IRAM_ATTR on_new_trans(esp_cam_ctlr_handle_t h,
    esp_cam_ctlr_trans_t *t, void *ud) {
    t->buffer = raw_buffer; t->buflen = raw_buf_size; return false;
}
static bool IRAM_ATTR on_trans_done(esp_cam_ctlr_handle_t h,
    esp_cam_ctlr_trans_t *t, void *ud) {
    frame_count++; new_frame = true; return false;
}
static bool init_isp() {
    esp_isp_processor_cfg_t cfg = {}; cfg.clk_hz = 120000000;
    cfg.input_data_source = ISP_INPUT_DATA_SOURCE_CSI;
    cfg.input_data_color_type = ISP_COLOR_RAW8;
    cfg.output_data_color_type = ISP_COLOR_RAW8;
    cfg.has_line_start_packet = false; cfg.has_line_end_packet = false;
    cfg.h_res = CAM_H_RES; cfg.v_res = CAM_V_RES;
    if (esp_isp_new_processor(&cfg, &isp_handle) != ESP_OK) return false;
    return esp_isp_enable(isp_handle) == ESP_OK;
}
static bool init_csi() {
    esp_cam_ctlr_csi_config_t cfg = {}; cfg.ctlr_id = 0;
    cfg.h_res = CAM_H_RES; cfg.v_res = CAM_V_RES;
    cfg.lane_bit_rate_mbps = CSI_LANE_BITRATE;
    cfg.input_data_color_type = CAM_CTLR_COLOR_RAW8;
    cfg.output_data_color_type = CAM_CTLR_COLOR_RAW8;
    cfg.data_lane_num = CSI_DATA_LANES; cfg.byte_swap_en = false;
    cfg.queue_items = 1;
    if (esp_cam_new_csi_ctlr(&cfg, &cam_handle) != ESP_OK) return false;
    esp_cam_ctlr_evt_cbs_t cbs = {};
    cbs.on_get_new_trans = on_new_trans; cbs.on_trans_finished = on_trans_done;
    ESP_ERROR_CHECK(esp_cam_ctlr_register_event_callbacks(cam_handle,
        &cbs, NULL));
    ESP_ERROR_CHECK(esp_cam_ctlr_enable(cam_handle));
    return true;
}

// ═══════════════════════════════════════════════════════════
//  Bayer → RGB565 at 160×90 (8× downsample for speed)
// ═══════════════════════════════════════════════════════════
static void bayer_to_stream(const uint8_t *raw, uint16_t *rgb,
    int src_w, int src_h) {
    int step = src_w / STREAM_W;  // 1280/160 = 8
    for (int dy = 0; dy < STREAM_H; dy++) {
        int sy = dy * step;
        if (sy & 1) sy--;  // align to even for Bayer
        const uint8_t *row0 = &raw[sy * src_w];
        const uint8_t *row1 = &raw[(sy+1) * src_w];
        uint16_t *out = &rgb[dy * STREAM_W];
        for (int dx = 0; dx < STREAM_W; dx++) {
            int sx = dx * step;
            if (sx & 1) sx--;  // align to even for Bayer
            int r = min(255, (row1[sx+1] * 18) / 10);
            int g = (row0[sx+1] + row1[sx]) >> 1;
            int b = min(255, (row0[sx] * 16) / 10);
            out[dx] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        }
    }
}

// ═══════════════════════════════════════════════════════════
//  Auto-exposure
// ═══════════════════════════════════════════════════════════
static void auto_exposure(const uint8_t *raw, int w, int h) {
    uint32_t sum = 0; int count = 0;
    int cx = w/2, cy = h/2;
    for (int y = cy-160; y < cy+160; y += 10)
        for (int x = cx-160; x < cx+160; x += 10) {
            sum += raw[y*w+x]; count++;
        }
    uint8_t avg = count > 0 ? sum/count : 128;
    if (avg < AE_TARGET - AE_TOLERANCE && current_exposure < AE_MAX_EXP) {
        current_exposure += 4; cam_write_reg(0x3e01, current_exposure);
    } else if (avg > AE_TARGET + AE_TOLERANCE && current_exposure > AE_MIN_EXP) {
        current_exposure -= 4; cam_write_reg(0x3e01, current_exposure);
    }
}

// ═══════════════════════════════════════════════════════════
//  TFLite
// ═══════════════════════════════════════════════════════════
static bool init_tflite() {
    tensor_arena = (uint8_t *)heap_caps_malloc(kTensorArenaSize,
                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!tensor_arena) return false;
    if (!modelInit(person_detect_model, tensor_arena, kTensorArenaSize))
        return false;
    Serial.printf("[TFL] In:%d Out:%d\n",
        tflInputTensor->bytes, tflOutputTensor->bytes);
    return true;
}

static void run_person_detection(const uint8_t *raw, int w, int h) {
    if (!tflInputTensor || !tflOutputTensor) return;
    float x_step = (float)w / MODEL_DIM;
    float y_step = (float)h / MODEL_DIM;
    int8_t *in = tflInputTensor->data.int8;
    for (int dy = 0; dy < MODEL_DIM; dy++) {
        int sy = (int)(dy * y_step) & ~1;
        for (int dx = 0; dx < MODEL_DIM; dx++) {
            int sx = (int)(dx * x_step) & ~1;
            uint8_t val = (raw[sy*w+sx] + raw[sy*w+sx+1] +
                          raw[(sy+1)*w+sx] + raw[(sy+1)*w+sx+1]) >> 2;
            in[dy * MODEL_DIM + dx] = (int8_t)(val - 128);
        }
    }
    if (!modelRunInference()) return;
    int8_t *out = tflOutputTensor->data.int8;
    no_person_score = (out[0] + 128) / 255.0f;
    person_score    = (out[1] + 128) / 255.0f;
    score_history[score_idx] = person_score;
    score_idx = (score_idx + 1) % AVG_WINDOW;
    float sum = 0;
    for (int i = 0; i < AVG_WINDOW; i++) sum += score_history[i];
    avg_person_score = sum / AVG_WINDOW;
    person_detected = (avg_person_score > 0.70f);
}

// ═══════════════════════════════════════════════════════════
//  Display status panel on Tab5 screen
// ═══════════════════════════════════════════════════════════
static void update_display() {
    // Status text - update once per second to avoid wasting CPU
    M5.Display.setTextSize(3);
    M5.Display.setCursor(40, 250);
    if (person_detected) {
        M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
        M5.Display.printf("  PERSON %3d%%   ",
            (int)(avg_person_score * 100));
    } else {
        M5.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5.Display.printf("no person %2d%%   ",
            (int)(avg_person_score * 100));
    }

    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setCursor(40, 290);
    M5.Display.printf("FPS:%.0f  Clients:%d  Exp:0x%02X  ",
        cam_fps, WiFi.softAPgetStationNum(), current_exposure);
}

// ═══════════════════════════════════════════════════════════
//  Web server
// ═══════════════════════════════════════════════════════════
static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Tab5 Camera</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{background:#111;color:#eee;font-family:monospace;text-align:center}
h1{font-size:1em;padding:6px;color:#0f0}
canvas{border:4px solid #333;border-radius:8px;width:100%;
  max-width:360px;image-rendering:pixelated;
  transform:rotate(90deg);margin:40px auto}
#s{font-size:1.4em;margin:6px;padding:8px 16px;
  border-radius:8px;display:inline-block}
.y{background:#0a3;color:#fff}.n{background:#a00;color:#fff}
#i{color:#555;font-size:0.75em;padding:4px}
</style>
</head><body>
<h1>Tab5-Cam</h1>
<div><canvas id="c" width="160" height="90"></canvas></div>
<div id="s">...</div>
<div id="i">ESP32-P4 SC202CS MIPI-CSI | TFLite 96x96 |
  <span id="f">-</span> FPS</div>
<script>
const C=document.getElementById('c'),X=C.getContext('2d'),W=160,H=90;
const I=X.createImageData(W,H);
let fc=0,lt=Date.now();
async function F(){
  try{
    const r=await fetch('/frame');
    const b=new Uint16Array(await r.arrayBuffer());
    const d=I.data;
    for(let i=0;i<b.length;i++){
      const v=b[i];
      d[i*4]=((v>>11)&0x1F)*255/31|0;
      d[i*4+1]=((v>>5)&0x3F)*255/63|0;
      d[i*4+2]=(v&0x1F)*255/31|0;
      d[i*4+3]=255;
    }
    X.putImageData(I,0,0);
    fc++;
  }catch(e){}
  setTimeout(F,30);
}
async function D(){
  try{
    const r=await fetch('/detect');
    const d=await r.json();
    const s=document.getElementById('s');
    if(d.detected){
      s.className='y';s.textContent='PERSON '+d.avg+'%';
      C.style.borderColor='#0f0';
    }else{
      s.className='n';s.textContent='No person '+d.avg+'%';
      C.style.borderColor='#f00';
    }
  }catch(e){}
}
setInterval(()=>{
  const n=Date.now(),fps=Math.round(fc*1000/(n-lt));
  document.getElementById('f').textContent=fps;
  fc=0;lt=n;
},2000);
setInterval(D,400);
F();
</script>
</body></html>
)rawliteral";

static void handle_index() {
    server.send(200, "text/html", INDEX_HTML);
}

static void handle_frame() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Cache-Control", "no-store");
    server.setContentLength(STREAM_W * STREAM_H * 2);
    server.send(200, "application/octet-stream", "");
    server.sendContent((const char *)stream_buffer,
        STREAM_W * STREAM_H * 2);
    frame_served++;
}

static void handle_detect() {
    char j[128];
    snprintf(j, sizeof(j),
        "{\"person\":%d,\"no_person\":%d,\"detected\":%s,\"avg\":%d}",
        (int)(person_score*100), (int)(no_person_score*100),
        person_detected ? "true" : "false",
        (int)(avg_person_score*100));
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", j);
}

static void handle_status() {
    char j[256];
    snprintf(j, sizeof(j),
        "{\"cam_fps\":%.1f,\"exp\":\"0x%02X\",\"clients\":%d,"
        "\"person\":%d,\"detected\":%s,\"heap\":%u,\"served\":%lu}",
        cam_fps, current_exposure, WiFi.softAPgetStationNum(),
        (int)(avg_person_score*100),
        person_detected ? "true" : "false",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
        frame_served);
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", j);
}

// ═══════════════════════════════════════════════════════════
//  setup()
// ═══════════════════════════════════════════════════════════
void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(1);
    Serial.begin(115200);
    delay(2000);

    Serial.println("\n════════════════════════════════════════");
    Serial.println("  Tab5 Wi-Fi Camera v12");
    Serial.println("════════════════════════════════════════\n");

    // 1-4: Camera init
    esp_ldo_channel_handle_t ldo3 = NULL;
    esp_ldo_channel_config_t ldo3_cfg = {
        .chan_id = 3, .voltage_mv = 2500
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo3_cfg, &ldo3));
    if (!init_xclk()) {
        Serial.println("XCLK FAIL"); while(1) delay(1000);
    }
    delay(100);
    Wire.begin(INT_I2C_SDA, INT_I2C_SCL, 400000);
    delay(100);
    if (!init_sensor()) {
        Serial.println("SENSOR FAIL"); while(1) delay(1000);
    }
    Serial.println("[OK] Camera sensor");

    // 5: Buffers
    raw_buf_size = CAM_H_RES * CAM_V_RES;
    raw_buffer = (uint8_t *)heap_caps_aligned_alloc(64, raw_buf_size,
        MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    memset(raw_buffer, 0, raw_buf_size);
    stream_buffer = (uint16_t *)heap_caps_malloc(
        STREAM_W * STREAM_H * 2, MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    Serial.println("[OK] Buffers");

    // 6: ISP + CSI
    if (!init_isp() || !init_csi()) {
        Serial.println("ISP/CSI FAIL"); while(1) delay(1000);
    }
    ESP_ERROR_CHECK(esp_cam_ctlr_start(cam_handle));
    Serial.println("[OK] ISP+CSI");

    // 7-8: Exposure + stream
    cam_write_reg(0x3e00, 0x00);
    cam_write_reg(0x3e01, current_exposure);
    cam_write_reg(0x3e02, 0x00);
    cam_write_reg(0x3e09, 0x10);
    cam_write_reg(0x0100, 0x01);
    delay(30);
    Serial.println("[OK] Streaming");

    // 9: TFLite
    tflite_ready = init_tflite();
    Serial.printf("[%s] TFLite\n", tflite_ready ? "OK" : "FAIL");

    // 10: Wi-Fi AP
    WiFi.setPins(SDIO2_CLK, SDIO2_CMD, SDIO2_D0,
                 SDIO2_D1, SDIO2_D2, SDIO2_D3, SDIO2_RST);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    server.on("/", handle_index);
    server.on("/frame", handle_frame);
    server.on("/detect", handle_detect);
    server.on("/status", handle_status);
    server.begin();
    Serial.printf("[OK] AP: %s → http://%s\n",
        AP_SSID, WiFi.softAPIP().toString().c_str());

    // Display init screen
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(TFT_CYAN);
    M5.Display.setCursor(40, 100);
    M5.Display.printf("Wi-Fi: %s", AP_SSID);
    M5.Display.setCursor(40, 140);
    M5.Display.setTextColor(TFT_GREEN);
    M5.Display.printf("http://%s",
        WiFi.softAPIP().toString().c_str());
    M5.Display.setTextSize(2);
    M5.Display.setCursor(40, 180);
    M5.Display.setTextColor(TFT_YELLOW);
    M5.Display.printf("Pass: %s", AP_PASS);

    Serial.println("\n*** READY ***\n");
}

// ═══════════════════════════════════════════════════════════
//  loop()
// ═══════════════════════════════════════════════════════════
void loop() {
    static uint32_t last_count=0, last_ms=0;
    static uint32_t ae_cnt=0, det_cnt=0;

    server.handleClient();

    if (new_frame) {
        new_frame = false;

        if (++ae_cnt >= AE_INTERVAL) {
            ae_cnt = 0;
            auto_exposure(raw_buffer, CAM_H_RES, CAM_V_RES);
        }

        if (tflite_ready && ++det_cnt >= DETECT_INTERVAL) {
            det_cnt = 0;
            run_person_detection(raw_buffer, CAM_H_RES, CAM_V_RES);
        }

        bayer_to_stream(raw_buffer, stream_buffer,
            CAM_H_RES, CAM_V_RES);
    }

    // Update display every 1 second
    uint32_t now = millis();
    if (now - last_display_ms >= 1000) {
        last_display_ms = now;
        update_display();
    }

    // Serial status every 5s
    if (now - last_ms >= 5000) {
        uint32_t elapsed = now - last_ms;
        uint32_t delta = frame_count - last_count;
        cam_fps = delta * 1000.0f / elapsed;
        Serial.printf("[v12] cam=%.0f FPS | person=%d%% %s"
            " | clients=%d | served=%lu\n",
            cam_fps, (int)(avg_person_score*100),
            person_detected ? "YES" : "no",
            WiFi.softAPgetStationNum(), frame_served);
        last_count = frame_count; last_ms = now;
    }
}
