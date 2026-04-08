/**
 * CrowPanel Advanced 7" ESP32-P4 — Microphone Record & Playback
 *
 * Hardware (confirmed from Elecrow Lesson 11):
 *   Speaker (I2S_NUM_1, STD mode):
 *     BCLK=22, LRCLK=21, DOUT=23, AMP_EN=30 (active LOW)
 *   Microphone (I2S_NUM_0, PDM mode):
 *     CLK=24, DIN=26, 16kHz mono
 *
 * Features:
 *   - Record up to 5 seconds from mic
 *   - Playback through speaker
 *   - Touch buttons + serial commands
 *   - Live mic level meter
 *
 * Init order: SD → Display → Touch → LVGL → Amp OFF → I2S → UI
 *
 * BOARD SETTINGS: Default partition, 16MB flash, OPI PSRAM
 */

#include <esp_display_panel.hpp>
#include <esp_lcd_panel_ops.h>
#include <lvgl.h>

#include "driver/i2s_std.h"
#include "driver/i2s_pdm.h"
#include "driver/gpio.h"
#include <math.h>

using namespace esp_panel::drivers;

// ═══════════════════════════════════════════════════════════
//  Pin Definitions (from Elecrow BSP)
// ═══════════════════════════════════════════════════════════
// Speaker
#define AUDIO_GPIO_LRCLK  21
#define AUDIO_GPIO_BCLK   22
#define AUDIO_GPIO_SDATA  23
#define AUDIO_GPIO_CTRL   30

// Microphone
#define MIC_GPIO_CLK      24
#define MIC_GPIO_SDIN2    26

// ═══════════════════════════════════════════════════════════
//  Display + Touch config (proven)
// ═══════════════════════════════════════════════════════════
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

// ═══════════════════════════════════════════════════════════
//  Audio config
// ═══════════════════════════════════════════════════════════
#define SAMPLE_RATE     16000
#define REC_SECONDS     5
#define REC_SAMPLES     (SAMPLE_RATE * REC_SECONDS)
#define TWO_PI          6.283185307f

// ═══════════════════════════════════════════════════════════
//  Globals
// ═══════════════════════════════════════════════════════════

// Display
static LCD_EK79007 *g_lcd = NULL;
static TouchGT911 *g_touch = NULL;
static lv_display_t *lvgl_disp = NULL;
static lv_indev_t *lvgl_touch_dev = NULL;
static volatile bool touch_pressed = false;
static volatile int16_t touch_x = 0, touch_y = 0;

// I2S handles
static i2s_chan_handle_t tx_chan = NULL;   // Speaker — I2S_NUM_1 STD
static i2s_chan_handle_t rx_chan = NULL;   // Mic — I2S_NUM_0 PDM
static bool spk_ready = false;
static bool mic_ready = false;

// Recording buffer in PSRAM
static int16_t *rec_buf = NULL;
static int rec_samples = 0;
static bool recording = false;
static bool playing = false;
static bool has_recording = false;

// Live level
static float live_rms = 0;
static int16_t live_peak = 0;

// UI
static lv_obj_t *title_lbl = NULL;
static lv_obj_t *status_lbl = NULL;
static lv_obj_t *info_lbl = NULL;
static lv_obj_t *level_lbl = NULL;
static lv_obj_t *level_bar = NULL;
static lv_obj_t *rec_info_lbl = NULL;

// ═══════════════════════════════════════════════════════════
//  Display + Touch init (proven)
// ═══════════════════════════════════════════════════════════

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *a, uint8_t *px) {
  esp_lcd_panel_handle_t p = g_lcd->getHandle();
  if (p) {
    esp_err_t r;
    do {
      r = esp_lcd_panel_draw_bitmap(p, a->x1, a->y1, a->x2+1, a->y2+1, px);
      if (r == ESP_ERR_INVALID_STATE) delay(1);
    } while (r == ESP_ERR_INVALID_STATE);
  }
  lv_display_flush_ready(disp);
}

static void lvgl_touch_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  if (touch_pressed) {
    data->point.x = touch_x;
    data->point.y = touch_y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

static void init_hardware() {
  BusDSI *bus = new BusDSI(
    LCD_DSI_LANE_NUM, LCD_DSI_LANE_RATE,
    LCD_DPI_CLK_MHZ, LCD_COLOR_BITS,
    LCD_WIDTH, LCD_HEIGHT,
    LCD_DPI_HPW, LCD_DPI_HBP, LCD_DPI_HFP,
    LCD_DPI_VPW, LCD_DPI_VBP, LCD_DPI_VFP,
    LCD_DSI_PHY_LDO_ID);
  bus->configDpiFrameBufferNumber(1);
  assert(bus->begin());

  g_lcd = new LCD_EK79007(bus, LCD_WIDTH, LCD_HEIGHT, LCD_COLOR_BITS, LCD_RST_IO);
  assert(g_lcd->begin());

  BacklightPWM_LEDC *bl = new BacklightPWM_LEDC(LCD_BL_IO, LCD_BL_ON_LEVEL);
  assert(bl->begin());
  assert(bl->on());

  BusI2C *tb = new BusI2C(
    TOUCH_I2C_SCL, TOUCH_I2C_SDA,
    (BusI2C::ControlPanelFullConfig)ESP_PANEL_TOUCH_I2C_CONTROL_PANEL_CONFIG(GT911));
  tb->configI2C_FreqHz(TOUCH_I2C_FREQ);
  tb->configI2C_PullupEnable(true, true);
  g_touch = new TouchGT911(tb, LCD_WIDTH, LCD_HEIGHT, TOUCH_RST_IO, TOUCH_INT_IO);
  g_touch->begin();

  lv_init();
  lv_tick_set_cb((lv_tick_get_cb_t)millis);

  size_t bsz = LCD_WIDTH * LVGL_BUF_LINES * sizeof(lv_color_t);
  uint8_t *buf1 = (uint8_t *)heap_caps_malloc(bsz, MALLOC_CAP_SPIRAM);
  assert(buf1);

  lvgl_disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  lv_display_set_flush_cb(lvgl_disp, lvgl_flush_cb);
  lv_display_set_buffers(lvgl_disp, buf1, NULL, bsz, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lvgl_touch_dev = lv_indev_create();
  lv_indev_set_type(lvgl_touch_dev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(lvgl_touch_dev, lvgl_touch_cb);
  lv_indev_set_display(lvgl_touch_dev, lvgl_disp);
}

// ═══════════════════════════════════════════════════════════
//  Amplifier Control (active LOW per BSP)
// ═══════════════════════════════════════════════════════════

static void amp_init() {
  const gpio_config_t cfg = {
    .pin_bit_mask = 1ULL << AUDIO_GPIO_CTRL,
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&cfg);
  gpio_set_level((gpio_num_t)AUDIO_GPIO_CTRL, 1);  // OFF (active LOW)
  Serial.println("[AMP] Init OK (OFF)");
}

static void set_amp(bool on) {
  // BSP: set_Audio_ctrl(state) → gpio_set_level(CTRL, !state)
  gpio_set_level((gpio_num_t)AUDIO_GPIO_CTRL, on ? 0 : 1);
  Serial.printf("[AMP] %s\n", on ? "ON" : "OFF");
}

// ═══════════════════════════════════════════════════════════
//  Speaker Init — I2S_NUM_1, Standard mode
// ═══════════════════════════════════════════════════════════

static bool init_speaker() {
  Serial.println("[SPK] Init I2S_NUM_1 STD TX...");

  i2s_chan_config_t chan_cfg = {
    .id = I2S_NUM_1,              // Speaker on port 1
    .role = I2S_ROLE_MASTER,
    .dma_desc_num = 6,
    .dma_frame_num = 256,
    .auto_clear_after_cb = true,
    .auto_clear_before_cb = false,
    .intr_priority = 0,
  };

  esp_err_t err = i2s_new_channel(&chan_cfg, &tx_chan, NULL);
  if (err != ESP_OK) {
    Serial.printf("[SPK] new_channel fail: %s\n", esp_err_to_name(err));
    return false;
  }

  i2s_std_config_t std_cfg = {
    .clk_cfg = {
      .sample_rate_hz = SAMPLE_RATE,
      .clk_src = I2S_CLK_SRC_DEFAULT,
      .ext_clk_freq_hz = 0,
      .mclk_multiple = I2S_MCLK_MULTIPLE_256,
    },
    .slot_cfg = {
      .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
      .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
      .slot_mode = I2S_SLOT_MODE_STEREO,
      .slot_mask = I2S_STD_SLOT_BOTH,
      .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
      .ws_pol = false,
      .bit_shift = true,
      .left_align = true,
      .big_endian = false,
      .bit_order_lsb = false,
    },
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)AUDIO_GPIO_BCLK,
      .ws   = (gpio_num_t)AUDIO_GPIO_LRCLK,
      .dout = (gpio_num_t)AUDIO_GPIO_SDATA,
      .din  = I2S_GPIO_UNUSED,
      .invert_flags = { false, false, false },
    },
  };

  err = i2s_channel_init_std_mode(tx_chan, &std_cfg);
  if (err != ESP_OK) {
    Serial.printf("[SPK] init_std fail: %s\n", esp_err_to_name(err));
    return false;
  }

  err = i2s_channel_enable(tx_chan);
  if (err != ESP_OK) {
    Serial.printf("[SPK] enable fail: %s\n", esp_err_to_name(err));
    return false;
  }

  Serial.println("[SPK] Ready");
  return true;
}

// ═══════════════════════════════════════════════════════════
//  Microphone Init — I2S_NUM_0, PDM RX mode
// ═══════════════════════════════════════════════════════════

static bool init_mic() {
  Serial.println("[MIC] Init I2S_NUM_0 PDM RX...");
  Serial.printf("[MIC] CLK=%d, DIN=%d\n", MIC_GPIO_CLK, MIC_GPIO_SDIN2);

  i2s_chan_config_t rx_chan_cfg = {
    .id = I2S_NUM_0,              // Mic on port 0
    .role = I2S_ROLE_MASTER,
    .dma_desc_num = 6,
    .dma_frame_num = 256,
    .auto_clear_after_cb = true,
    .auto_clear_before_cb = true,
    .intr_priority = 0,
  };

  esp_err_t err = i2s_new_channel(&rx_chan_cfg, NULL, &rx_chan);
  if (err != ESP_OK) {
    Serial.printf("[MIC] new_channel fail: %s\n", esp_err_to_name(err));
    return false;
  }

  i2s_pdm_rx_config_t pdm_rx_cfg = {
    .clk_cfg = {
      .sample_rate_hz = SAMPLE_RATE,
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
      .clk = (gpio_num_t)MIC_GPIO_CLK,
      .din = (gpio_num_t)MIC_GPIO_SDIN2,
      .invert_flags = { .clk_inv = false },
    },
  };

  err = i2s_channel_init_pdm_rx_mode(rx_chan, &pdm_rx_cfg);
  if (err != ESP_OK) {
    Serial.printf("[MIC] pdm_rx init fail: %s\n", esp_err_to_name(err));
    return false;
  }

  err = i2s_channel_enable(rx_chan);
  if (err != ESP_OK) {
    Serial.printf("[MIC] enable fail: %s\n", esp_err_to_name(err));
    return false;
  }

  Serial.println("[MIC] Ready (PDM RX)");
  return true;
}

// ═══════════════════════════════════════════════════════════
//  Record & Playback
// ═══════════════════════════════════════════════════════════

static void do_record() {
  if (!mic_ready) {
    Serial.println("[REC] Mic not ready!");
    return;
  }
  if (playing) return;

  Serial.printf("[REC] Recording %d seconds...\n", REC_SECONDS);
  recording = true;
  rec_samples = 0;

  int16_t chunk[256];
  uint32_t start_ms = millis();

  while (rec_samples < REC_SAMPLES) {
    size_t bytes_read = 0;
    esp_err_t err = i2s_channel_read(rx_chan, chunk, sizeof(chunk), &bytes_read, 500);
    if (err != ESP_OK) {
      Serial.printf("[REC] Read error: %s\n", esp_err_to_name(err));
      break;
    }
    if (bytes_read == 0) continue;

    int samples_got = bytes_read / 2;  // 16-bit mono
    for (int i = 0; i < samples_got && rec_samples < REC_SAMPLES; i++) {
      rec_buf[rec_samples++] = chunk[i];
    }

    // Update live level every ~100ms
    if (rec_samples % (SAMPLE_RATE / 10) < samples_got) {
      int64_t sq = 0;
      int16_t pk = 0;
      int start = (rec_samples > 1600) ? rec_samples - 1600 : 0;
      for (int i = start; i < rec_samples; i++) {
        sq += (int32_t)rec_buf[i] * rec_buf[i];
        if (abs(rec_buf[i]) > pk) pk = abs(rec_buf[i]);
      }
      live_rms = sqrtf((float)sq / (rec_samples - start));
      live_peak = pk;
    }
  }

  uint32_t elapsed = millis() - start_ms;
  recording = false;
  has_recording = true;

  // Stats
  int64_t sq = 0;
  int16_t pk = 0;
  for (int i = 0; i < rec_samples; i++) {
    sq += (int32_t)rec_buf[i] * rec_buf[i];
    if (abs(rec_buf[i]) > pk) pk = abs(rec_buf[i]);
  }
  float rms = sqrtf((float)sq / rec_samples);

  Serial.printf("[REC] Done: %d samples in %lums\n", rec_samples, elapsed);
  Serial.printf("[REC] RMS=%.0f Peak=%d\n", rms, pk);
}

static void do_playback() {
  if (!has_recording) {
    Serial.println("[PLAY] No recording!");
    return;
  }
  if (!spk_ready) {
    Serial.println("[PLAY] Speaker not ready!");
    return;
  }
  if (recording) return;

  Serial.printf("[PLAY] Playing %d samples...\n", rec_samples);
  playing = true;
  set_amp(true);
  delay(20);

  // Play mono recording as stereo, with volume boost (x10 per BSP)
  int16_t stereo[512];
  int pos = 0;

  while (pos < rec_samples) {
    int chunk = (rec_samples - pos > 256) ? 256 : (rec_samples - pos);
    for (int i = 0; i < chunk; i++) {
      int32_t s = (int32_t)rec_buf[pos + i] * 10;  // Volume amplify
      if (s > 32767) s = 32767;
      if (s < -32767) s = -32767;
      stereo[i * 2] = (int16_t)s;       // L
      stereo[i * 2 + 1] = (int16_t)s;   // R
    }
    size_t bw = 0;
    i2s_channel_write(tx_chan, stereo, chunk * 4, &bw, 200);
    pos += chunk;
  }

  set_amp(false);
  playing = false;
  Serial.println("[PLAY] Done");
}

static void do_beep() {
  if (!spk_ready) return;
  set_amp(true);
  delay(20);

  float ph = 0.0f;
  float inc = TWO_PI * 440.0f / SAMPLE_RATE;
  int16_t buf[256 * 2];
  int total = SAMPLE_RATE / 2;  // 0.5s

  while (total > 0) {
    int chunk = (total > 256) ? 256 : total;
    for (int i = 0; i < chunk; i++) {
      int16_t s = (int16_t)(sinf(ph) * 16000.0f);
      buf[i * 2] = s;
      buf[i * 2 + 1] = s;
      ph += inc;
      if (ph >= TWO_PI) ph -= TWO_PI;
    }
    size_t bw;
    i2s_channel_write(tx_chan, buf, chunk * 4, &bw, 200);
    total -= chunk;
  }

  set_amp(false);
  Serial.println("[BEEP] Done");
}

// Read mic level without recording
static void update_mic_level() {
  if (!mic_ready || recording) return;

  int16_t chunk[256];
  size_t br = 0;
  esp_err_t err = i2s_channel_read(rx_chan, chunk, sizeof(chunk), &br, 10);
  if (err != ESP_OK || br == 0) return;

  int ns = br / 2;
  int64_t sq = 0;
  int16_t pk = 0;
  for (int i = 0; i < ns; i++) {
    sq += (int32_t)chunk[i] * chunk[i];
    if (abs(chunk[i]) > pk) pk = abs(chunk[i]);
  }
  live_rms = sqrtf((float)sq / ns);
  live_peak = pk;
}

// ═══════════════════════════════════════════════════════════
//  UI
// ═══════════════════════════════════════════════════════════

static lv_obj_t* make_btn(lv_obj_t *parent, const char *text,
                           lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                           lv_event_cb_t cb, uint32_t bg = 0x2A2A4A) {
  lv_obj_t *btn = lv_button_create(parent);
  lv_obj_set_size(btn, w, h);
  lv_obj_set_pos(btn, x, y);
  lv_obj_set_style_bg_color(btn, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x5A5A8A), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn, 8, 0);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_22, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xDDDDDD), 0);
  lv_obj_center(lbl);
  return btn;
}

static void cb_record(lv_event_t *e) { do_record(); }
static void cb_play(lv_event_t *e) { do_playback(); }
static void cb_beep(lv_event_t *e) { do_beep(); }

static void build_ui() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A0A1A), 0);

  // Title
  title_lbl = lv_label_create(scr);
  lv_label_set_text(title_lbl, "CrowPanel Mic + Speaker");
  lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(title_lbl, lv_color_hex(0xFF6644), 0);
  lv_obj_set_pos(title_lbl, 20, 15);

  // Status
  status_lbl = lv_label_create(scr);
  lv_label_set_text(status_lbl, "Ready");
  lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x00FF88), 0);
  lv_obj_set_pos(status_lbl, 20, 55);

  // Info
  info_lbl = lv_label_create(scr);
  lv_label_set_text(info_lbl, "...");
  lv_obj_set_style_text_font(info_lbl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(info_lbl, lv_color_hex(0x888888), 0);
  lv_obj_set_pos(info_lbl, 20, 90);

  // Mic level label
  level_lbl = lv_label_create(scr);
  lv_label_set_text(level_lbl, "Mic Level: ---");
  lv_obj_set_style_text_font(level_lbl, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(level_lbl, lv_color_hex(0x00CCFF), 0);
  lv_obj_set_pos(level_lbl, 20, 125);

  // Mic level bar
  level_bar = lv_bar_create(scr);
  lv_obj_set_size(level_bar, 600, 25);
  lv_obj_set_pos(level_bar, 20, 160);
  lv_bar_set_range(level_bar, 0, 100);
  lv_bar_set_value(level_bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(level_bar, lv_color_hex(0x222244), 0);
  lv_obj_set_style_bg_color(level_bar, lv_color_hex(0x00CC66), LV_PART_INDICATOR);

  // Recording info
  rec_info_lbl = lv_label_create(scr);
  lv_label_set_text(rec_info_lbl, "No recording yet");
  lv_obj_set_style_text_font(rec_info_lbl, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(rec_info_lbl, lv_color_hex(0xCCCCCC), 0);
  lv_obj_set_pos(rec_info_lbl, 20, 205);

  // Buttons
  int by = 260, bw = 200, bh = 70, gap = 20;
  make_btn(scr, LV_SYMBOL_AUDIO " Record", 20, by, bw, bh, cb_record, 0x4A1A1A);
  make_btn(scr, LV_SYMBOL_PLAY " Play", 20 + bw + gap, by, bw, bh, cb_play, 0x1A4A2A);
  make_btn(scr, LV_SYMBOL_BELL " Beep", 20 + (bw + gap) * 2, by, bw, bh, cb_beep, 0x2A2A5A);

  // Serial help
  lv_obj_t *help = lv_label_create(scr);
  lv_label_set_text(help, "Serial: r=record  p=play  b=beep  ?=help");
  lv_obj_set_style_text_font(help, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(help, lv_color_hex(0x555555), 0);
  lv_obj_set_pos(help, 20, 560);
}

static void update_ui() {
  // Status
  if (recording) {
    lv_label_set_text(status_lbl, "RECORDING...");
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xFF4444), 0);
  } else if (playing) {
    lv_label_set_text(status_lbl, "PLAYING...");
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x4488FF), 0);
  } else {
    lv_label_set_text(status_lbl, "Ready");
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x00FF88), 0);
  }

  // Info
  char buf[128];
  snprintf(buf, sizeof(buf), "SPK: %s (I2S1 STD) | MIC: %s (I2S0 PDM) | CLK=%d DIN=%d",
           spk_ready ? "OK" : "FAIL", mic_ready ? "OK" : "FAIL",
           MIC_GPIO_CLK, MIC_GPIO_SDIN2);
  lv_label_set_text(info_lbl, buf);

  // Mic level
  snprintf(buf, sizeof(buf), "Mic Level: RMS=%.0f  Peak=%d", live_rms, live_peak);
  lv_label_set_text(level_lbl, buf);

  // Level bar (log scale, 0-100)
  int bar_val = 0;
  if (live_rms > 1) {
    bar_val = (int)(20.0f * log10f(live_rms));
    if (bar_val < 0) bar_val = 0;
    if (bar_val > 100) bar_val = 100;
  }
  lv_bar_set_value(level_bar, bar_val, LV_ANIM_OFF);

  // Color the bar based on level
  if (bar_val > 80)
    lv_obj_set_style_bg_color(level_bar, lv_color_hex(0xFF4444), LV_PART_INDICATOR);
  else if (bar_val > 50)
    lv_obj_set_style_bg_color(level_bar, lv_color_hex(0xFFAA00), LV_PART_INDICATOR);
  else
    lv_obj_set_style_bg_color(level_bar, lv_color_hex(0x00CC66), LV_PART_INDICATOR);

  // Recording info
  if (has_recording) {
    snprintf(buf, sizeof(buf), "Recording: %.1f sec (%d samples)",
             rec_samples / (float)SAMPLE_RATE, rec_samples);
    lv_label_set_text(rec_info_lbl, buf);
  }
}

// ═══════════════════════════════════════════════════════════
//  Serial Commands
// ═══════════════════════════════════════════════════════════

static void handle_serial() {
  while (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 'r': do_record(); break;
      case 'p': do_playback(); break;
      case 'b': do_beep(); break;
      case '?':
        Serial.println("r=record p=play b=beep");
        break;
    }
  }
}

// ═══════════════════════════════════════════════════════════
//  Setup & Loop
// ═══════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n╔══════════════════════════════════════╗");
  Serial.println("║  CrowPanel P4 — Mic + Speaker         ║");
  Serial.println("╚══════════════════════════════════════╝\n");

  // Allocate recording buffer in PSRAM (5 sec * 16kHz * 2 bytes = 160KB)
  rec_buf = (int16_t *)heap_caps_malloc(REC_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
  if (!rec_buf) {
    Serial.println("FATAL: Cannot allocate recording buffer!");
    while (1) delay(1000);
  }
  Serial.printf("[MEM] Rec buffer: %d bytes in PSRAM\n", REC_SAMPLES * 2);

  // 1. Display + touch
  Serial.println("[DISP] Init...");
  init_hardware();
  Serial.println("[DISP] OK");

  // 2. UI
  build_ui();
  lv_timer_handler();

  // 3. Amp OFF first (avoid pop)
  amp_init();
  delay(50);

  // 4. Speaker TX — I2S_NUM_1
  spk_ready = init_speaker();

  // 5. Mic RX — I2S_NUM_0 PDM
  mic_ready = init_mic();

  // 6. Quick beep to confirm speaker
  if (spk_ready) {
    Serial.println("[TEST] Speaker beep...");
    do_beep();
  }

  // 7. Quick mic test
  if (mic_ready) {
    Serial.println("[TEST] Reading mic...");
    int16_t chunk[256];
    size_t br = 0;
    esp_err_t err = i2s_channel_read(rx_chan, chunk, sizeof(chunk), &br, 500);
    if (err == ESP_OK && br > 0) {
      int ns = br / 2;
      int64_t sq = 0;
      int16_t pk = 0;
      for (int i = 0; i < ns; i++) {
        sq += (int32_t)chunk[i] * chunk[i];
        if (abs(chunk[i]) > pk) pk = abs(chunk[i]);
      }
      float rms = sqrtf((float)sq / ns);
      Serial.printf("[TEST] Mic read OK: %d samples, RMS=%.0f Peak=%d\n", ns, rms, pk);
    } else {
      Serial.printf("[TEST] Mic read fail: %s\n", esp_err_to_name(err));
    }
  }

  update_ui();
  lv_timer_handler();

  Serial.println("\nReady! r=record p=play b=beep\n");
}

void loop() {
  // Touch polling
  static uint32_t last_touch = 0;
  if (millis() - last_touch > 30) {
    last_touch = millis();
    if (g_touch) {
      esp_panel::drivers::TouchPoint tp[1];
      int pts = g_touch->readPoints(tp, 1);
      if (pts > 0) {
        touch_pressed = true;
        touch_x = tp[0].x;
        touch_y = tp[0].y;
      } else {
        touch_pressed = false;
      }
    }
  }

  // Live mic level (when not recording/playing)
  if (!recording && !playing) {
    update_mic_level();
  }

  // UI refresh
  static uint32_t last_ui = 0;
  if (millis() - last_ui > 100) {
    last_ui = millis();
    update_ui();
  }

  lv_timer_handler();
  handle_serial();
  delay(5);
}
