/**
 * CrowPanel Advanced 7" ESP32-P4 — Sound / Audio Test
 *
 * Audio hardware: NS4168 I2S amplifier
 *   BCLK  = GPIO 22
 *   LRCLK = GPIO 21
 *   SDATA = GPIO 23
 *   AMP_EN = GPIO 30 (active HIGH to enable amplifier)
 *
 * Features:
 *   - Tone generator (sine wave) at selectable frequencies
 *   - Volume control
 *   - WAV file playback from SD card
 *   - Touch UI with buttons
 *   - Serial commands for testing
 *
 * Init order: SD → Display → Touch → LVGL → I2S → UI
 *
 * BOARD SETTINGS: Default partition, 16MB flash, OPI PSRAM
 */

#include <esp_display_panel.hpp>
#include <esp_lcd_panel_ops.h>
#include <lvgl.h>
#include <SD_MMC.h>
#include <FS.h>

// ESP-IDF I2S driver (new API — works on ESP32-P4)
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include <math.h>

using namespace esp_panel::drivers;

// ═══════════════════════════════════════════════════════════
//  Pin Definitions
// ═══════════════════════════════════════════════════════════
#define I2S_BCLK_PIN    22
#define I2S_LRCLK_PIN   21
#define I2S_SDATA_PIN   23
#define AMP_EN_PIN      30

// ═══════════════════════════════════════════════════════════
//  Display + Touch config (proven from SD Card sketch)
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
#define I2S_BUF_SIZE    1024    // samples per write
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

// I2S
static i2s_chan_handle_t tx_handle = NULL;
static bool i2s_ready = false;
static bool amp_enabled = false;

// Tone state
static float tone_freq = 440.0f;     // Hz
static float tone_volume = 0.5f;     // 0.0-1.0
static bool tone_playing = false;
static float phase = 0.0f;

// WAV playback state
static bool wav_playing = false;
static File wav_file;
static uint32_t wav_data_start = 0;
static uint32_t wav_data_size = 0;
static uint32_t wav_data_pos = 0;
static uint16_t wav_channels = 1;
static uint32_t wav_sample_rate = 44100;
static uint16_t wav_bits = 16;

// SD card
static bool sd_ready = false;

// UI labels
static lv_obj_t *title_lbl = NULL;
static lv_obj_t *status_lbl = NULL;
static lv_obj_t *freq_lbl = NULL;
static lv_obj_t *vol_lbl = NULL;
static lv_obj_t *info_lbl = NULL;
static lv_obj_t *sd_lbl = NULL;

// Preset frequencies
static const float FREQS[] = { 261.63, 329.63, 392.00, 440.00, 523.25, 659.25, 783.99, 880.00 };
static const char *NOTES[] = { "C4", "E4", "G4", "A4", "C5", "E5", "G5", "A5" };
static int freq_idx = 3;  // Start on A4

// ═══════════════════════════════════════════════════════════
//  Display + Touch init (proven from SD Card / NTP Clock)
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
//  I2S / Audio Init
// ═══════════════════════════════════════════════════════════

static bool init_i2s() {
  Serial.println("[I2S] Configuring I2S standard mode...");

  // Channel config
  i2s_chan_config_t chan_cfg = {
    .id = I2S_NUM_0,
    .role = I2S_ROLE_MASTER,
    .dma_desc_num = 8,
    .dma_frame_num = 256,
    .auto_clear_after_cb = true,
    .auto_clear_before_cb = false,
    .intr_priority = 0,
  };

  esp_err_t err = i2s_new_channel(&chan_cfg, &tx_handle, NULL);
  if (err != ESP_OK) {
    Serial.printf("[I2S] i2s_new_channel failed: 0x%x (%s)\n", err, esp_err_to_name(err));
    return false;
  }
  Serial.println("[I2S] Channel created OK");

  // Standard mode config for NS4168 — use Philips/MSB format
  i2s_std_config_t std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
                    I2S_DATA_BIT_WIDTH_16BIT,
                    I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)I2S_BCLK_PIN,
      .ws   = (gpio_num_t)I2S_LRCLK_PIN,
      .dout = (gpio_num_t)I2S_SDATA_PIN,
      .din  = I2S_GPIO_UNUSED,
      .invert_flags = {
        .mclk_inv = false,
        .bclk_inv = false,
        .ws_inv = false,
      },
    },
  };

  err = i2s_channel_init_std_mode(tx_handle, &std_cfg);
  if (err != ESP_OK) {
    Serial.printf("[I2S] i2s_channel_init_std_mode failed: 0x%x (%s)\n", err, esp_err_to_name(err));
    return false;
  }
  Serial.println("[I2S] Standard mode init OK");

  err = i2s_channel_enable(tx_handle);
  if (err != ESP_OK) {
    Serial.printf("[I2S] i2s_channel_enable failed: 0x%x (%s)\n", err, esp_err_to_name(err));
    return false;
  }
  Serial.println("[I2S] Channel enabled OK");

  return true;
}

static void set_amp(bool on) {
  gpio_set_direction((gpio_num_t)AMP_EN_PIN, GPIO_MODE_OUTPUT);
  // NS4168 CTRL pin: LOW = enable/left channel, HIGH = right channel or mute
  // Try active LOW first (based on Elecrow BSP pattern)
  gpio_set_level((gpio_num_t)AMP_EN_PIN, on ? 0 : 1);
  amp_enabled = on;
  Serial.printf("[AMP] Amplifier %s (GPIO%d = %d)\n",
                on ? "ENABLED" : "DISABLED", AMP_EN_PIN, on ? 0 : 1);
}

// ═══════════════════════════════════════════════════════════
//  Tone Generation
// ═══════════════════════════════════════════════════════════

static int16_t tone_buf[I2S_BUF_SIZE * 2];  // stereo: L,R interleaved

static void generate_and_send_tone() {
  if (!i2s_ready || !tone_playing) return;

  float phase_inc = TWO_PI * tone_freq / SAMPLE_RATE;
  int16_t amplitude = (int16_t)(32767.0f * tone_volume);

  for (int i = 0; i < I2S_BUF_SIZE; i++) {
    int16_t sample = (int16_t)(sinf(phase) * amplitude);
    tone_buf[i * 2] = sample;      // Left
    tone_buf[i * 2 + 1] = sample;  // Right
    phase += phase_inc;
    if (phase >= TWO_PI) phase -= TWO_PI;
  }

  size_t bytes_written = 0;
  i2s_channel_write(tx_handle, tone_buf, sizeof(tone_buf), &bytes_written, 100);
}

// ═══════════════════════════════════════════════════════════
//  WAV Playback
// ═══════════════════════════════════════════════════════════

static bool open_wav(const char *path) {
  wav_file = SD_MMC.open(path, FILE_READ);
  if (!wav_file) {
    Serial.printf("[WAV] Cannot open: %s\n", path);
    return false;
  }

  // Read WAV header
  uint8_t hdr[44];
  if (wav_file.read(hdr, 44) != 44) {
    Serial.println("[WAV] Header too short");
    wav_file.close();
    return false;
  }

  // Verify RIFF header
  if (hdr[0] != 'R' || hdr[1] != 'I' || hdr[2] != 'F' || hdr[3] != 'F') {
    Serial.println("[WAV] Not a RIFF file");
    wav_file.close();
    return false;
  }
  if (hdr[8] != 'W' || hdr[9] != 'A' || hdr[10] != 'V' || hdr[11] != 'E') {
    Serial.println("[WAV] Not a WAV file");
    wav_file.close();
    return false;
  }

  // Parse fmt chunk
  uint16_t fmt = hdr[20] | (hdr[21] << 8);
  wav_channels = hdr[22] | (hdr[23] << 8);
  wav_sample_rate = hdr[24] | (hdr[25] << 8) | (hdr[26] << 16) | (hdr[27] << 24);
  wav_bits = hdr[34] | (hdr[35] << 8);

  Serial.printf("[WAV] Format:%d Ch:%d Rate:%lu Bits:%d\n",
                fmt, wav_channels, wav_sample_rate, wav_bits);

  if (fmt != 1) {  // PCM only
    Serial.println("[WAV] Not PCM format");
    wav_file.close();
    return false;
  }

  // Find data chunk (may not be at offset 36 if extra fmt bytes)
  wav_file.seek(12);  // After "RIFF" + size + "WAVE"
  while (wav_file.available()) {
    uint8_t ck[8];
    if (wav_file.read(ck, 8) != 8) break;
    uint32_t ck_size = ck[4] | (ck[5] << 8) | (ck[6] << 16) | (ck[7] << 24);
    if (ck[0] == 'd' && ck[1] == 'a' && ck[2] == 't' && ck[3] == 'a') {
      wav_data_start = wav_file.position();
      wav_data_size = ck_size;
      wav_data_pos = 0;
      Serial.printf("[WAV] Data chunk at %lu, size %lu bytes\n", wav_data_start, wav_data_size);
      return true;
    }
    wav_file.seek(wav_file.position() + ck_size);
  }

  Serial.println("[WAV] No data chunk found");
  wav_file.close();
  return false;
}

static uint8_t wav_read_buf[4096];

static void stream_wav_chunk() {
  if (!wav_playing || !wav_file) return;

  uint32_t remaining = wav_data_size - wav_data_pos;
  if (remaining == 0) {
    wav_playing = false;
    wav_file.close();
    Serial.println("[WAV] Playback complete");
    return;
  }

  size_t to_read = sizeof(wav_read_buf);
  if (to_read > remaining) to_read = remaining;

  size_t got = wav_file.read(wav_read_buf, to_read);
  if (got == 0) {
    wav_playing = false;
    wav_file.close();
    return;
  }

  // If mono 16-bit, duplicate to stereo
  if (wav_channels == 1 && wav_bits == 16) {
    // Process in-place: read half buffer, expand to full
    size_t samples = got / 2;
    if (samples > I2S_BUF_SIZE) samples = I2S_BUF_SIZE;
    int16_t *src = (int16_t *)wav_read_buf;
    for (int i = samples - 1; i >= 0; i--) {
      tone_buf[i * 2] = src[i];
      tone_buf[i * 2 + 1] = src[i];
    }
    size_t bytes_written = 0;
    i2s_channel_write(tx_handle, tone_buf, samples * 4, &bytes_written, 100);
  } else {
    // Stereo 16-bit or other: send directly
    size_t bytes_written = 0;
    i2s_channel_write(tx_handle, wav_read_buf, got, &bytes_written, 100);
  }

  wav_data_pos += got;
}

// ═══════════════════════════════════════════════════════════
//  UI
// ═══════════════════════════════════════════════════════════

static lv_obj_t* make_btn(lv_obj_t *parent, const char *text,
                           lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                           lv_event_cb_t cb, uint32_t bg_color = 0x2A2A4A) {
  lv_obj_t *btn = lv_button_create(parent);
  lv_obj_set_size(btn, w, h);
  lv_obj_set_pos(btn, x, y);
  lv_obj_set_style_bg_color(btn, lv_color_hex(bg_color), 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x4A4A7A), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn, 8, 0);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xDDDDDD), 0);
  lv_obj_center(lbl);
  return btn;
}

// Button callbacks
static void cb_play_tone(lv_event_t *e) {
  if (wav_playing) { wav_playing = false; wav_file.close(); }
  tone_playing = !tone_playing;
  if (tone_playing) {
    set_amp(true);
    phase = 0.0f;
    Serial.printf("[TONE] Playing %.1f Hz (%s)\n", tone_freq, NOTES[freq_idx]);
  } else {
    Serial.println("[TONE] Stopped");
  }
}

static void cb_stop(lv_event_t *e) {
  tone_playing = false;
  wav_playing = false;
  if (wav_file) wav_file.close();
  set_amp(false);
  // Send silence to clear buffer
  if (i2s_ready) {
    memset(tone_buf, 0, sizeof(tone_buf));
    size_t bw;
    i2s_channel_write(tx_handle, tone_buf, sizeof(tone_buf), &bw, 50);
  }
  Serial.println("[AUDIO] Stopped all");
}

static void cb_freq_up(lv_event_t *e) {
  freq_idx = (freq_idx + 1) % 8;
  tone_freq = FREQS[freq_idx];
  Serial.printf("[TONE] Freq: %.1f Hz (%s)\n", tone_freq, NOTES[freq_idx]);
}

static void cb_freq_down(lv_event_t *e) {
  freq_idx = (freq_idx + 7) % 8;  // wrap around
  tone_freq = FREQS[freq_idx];
  Serial.printf("[TONE] Freq: %.1f Hz (%s)\n", tone_freq, NOTES[freq_idx]);
}

static void cb_vol_up(lv_event_t *e) {
  tone_volume += 0.1f;
  if (tone_volume > 1.0f) tone_volume = 1.0f;
  Serial.printf("[VOL] %.0f%%\n", tone_volume * 100);
}

static void cb_vol_down(lv_event_t *e) {
  tone_volume -= 0.1f;
  if (tone_volume < 0.0f) tone_volume = 0.0f;
  Serial.printf("[VOL] %.0f%%\n", tone_volume * 100);
}

static void cb_play_wav(lv_event_t *e) {
  if (!sd_ready) {
    Serial.println("[WAV] No SD card");
    return;
  }
  // Look for first .wav file on SD
  File root = SD_MMC.open("/");
  if (!root) return;
  File f;
  char wav_path[128] = "";
  while ((f = root.openNextFile())) {
    String name = f.name();
    name.toLowerCase();
    if (name.endsWith(".wav")) {
      snprintf(wav_path, sizeof(wav_path), "/%s", f.name());
      f.close();
      break;
    }
    f.close();
  }
  root.close();

  if (wav_path[0] == 0) {
    Serial.println("[WAV] No .wav files found on SD");
    return;
  }

  tone_playing = false;
  if (open_wav(wav_path)) {
    set_amp(true);
    wav_playing = true;
    Serial.printf("[WAV] Playing: %s\n", wav_path);
  }
}

static void cb_scale(lv_event_t *e) {
  // Play a quick scale: C4 E4 G4 C5
  if (wav_playing) { wav_playing = false; wav_file.close(); }
  set_amp(true);
  tone_playing = false;

  Serial.println("[SCALE] Playing C-E-G-C scale...");

  float scale[] = { 261.63, 329.63, 392.00, 523.25 };
  for (int n = 0; n < 4; n++) {
    float freq = scale[n];
    float ph = 0.0f;
    float phase_inc = TWO_PI * freq / SAMPLE_RATE;
    int16_t amp = (int16_t)(32767.0f * tone_volume);

    // Generate 200ms of tone
    int total_samples = SAMPLE_RATE / 5;  // 200ms
    int16_t buf[512 * 2];
    int remaining = total_samples;

    while (remaining > 0) {
      int chunk = (remaining > 512) ? 512 : remaining;
      for (int i = 0; i < chunk; i++) {
        // Apply envelope (fade in/out)
        float env = 1.0f;
        int pos = total_samples - remaining + i;
        if (pos < 200) env = pos / 200.0f;
        if (remaining - chunk + i > total_samples - 200)
          env = (total_samples - pos) / 200.0f;

        int16_t s = (int16_t)(sinf(ph) * amp * env);
        buf[i * 2] = s;
        buf[i * 2 + 1] = s;
        ph += phase_inc;
        if (ph >= TWO_PI) ph -= TWO_PI;
      }
      size_t bw;
      i2s_channel_write(tx_handle, buf, chunk * 4, &bw, 200);
      remaining -= chunk;
    }

    // 50ms silence between notes
    memset(buf, 0, sizeof(buf));
    size_t bw;
    int silence_samples = SAMPLE_RATE / 20;
    while (silence_samples > 0) {
      int chunk = (silence_samples > 512) ? 512 : silence_samples;
      i2s_channel_write(tx_handle, buf, chunk * 4, &bw, 100);
      silence_samples -= chunk;
    }
  }
  Serial.println("[SCALE] Done");
}

static void update_ui() {
  if (!status_lbl) return;

  // Status
  if (wav_playing) {
    lv_label_set_text(status_lbl, "WAV Playing");
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x00FF88), 0);
  } else if (tone_playing) {
    lv_label_set_text(status_lbl, "Tone Playing");
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x00CCFF), 0);
  } else {
    lv_label_set_text(status_lbl, "Stopped");
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xFF4444), 0);
  }

  // Frequency
  char buf[64];
  snprintf(buf, sizeof(buf), "%s  %.0f Hz", NOTES[freq_idx], tone_freq);
  lv_label_set_text(freq_lbl, buf);

  // Volume
  snprintf(buf, sizeof(buf), "Vol: %.0f%%", tone_volume * 100);
  lv_label_set_text(vol_lbl, buf);

  // Info
  snprintf(buf, sizeof(buf), "I2S: %s | Amp: %s | SD: %s",
           i2s_ready ? "OK" : "FAIL",
           amp_enabled ? "ON" : "OFF",
           sd_ready ? "OK" : "N/A");
  lv_label_set_text(info_lbl, buf);
}

static void build_ui() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A0A1A), 0);

  // Title
  title_lbl = lv_label_create(scr);
  lv_label_set_text(title_lbl, "CrowPanel Sound Test");
  lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(title_lbl, lv_color_hex(0x00CCFF), 0);
  lv_obj_set_pos(title_lbl, 20, 10);

  // Status (large)
  status_lbl = lv_label_create(scr);
  lv_label_set_text(status_lbl, "Initializing...");
  lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xFFAA00), 0);
  lv_obj_set_pos(status_lbl, 20, 55);

  // Frequency display
  freq_lbl = lv_label_create(scr);
  lv_label_set_text(freq_lbl, "A4  440 Hz");
  lv_obj_set_style_text_font(freq_lbl, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(freq_lbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_pos(freq_lbl, 20, 100);

  // Volume display
  vol_lbl = lv_label_create(scr);
  lv_label_set_text(vol_lbl, "Vol: 50%");
  lv_obj_set_style_text_font(vol_lbl, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(vol_lbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_pos(vol_lbl, 20, 135);

  // Info line
  info_lbl = lv_label_create(scr);
  lv_label_set_text(info_lbl, "Initializing...");
  lv_obj_set_style_text_font(info_lbl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(info_lbl, lv_color_hex(0x888888), 0);
  lv_obj_set_pos(info_lbl, 20, 175);

  // SD card info
  sd_lbl = lv_label_create(scr);
  lv_label_set_text(sd_lbl, "");
  lv_obj_set_style_text_font(sd_lbl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(sd_lbl, lv_color_hex(0x888888), 0);
  lv_obj_set_pos(sd_lbl, 20, 200);

  // ── Tone buttons ──────────────────
  int bx = 20, by = 240;
  int bw = 140, bh = 60, gap = 15;

  make_btn(scr, LV_SYMBOL_PLAY " Tone", bx, by, bw, bh, cb_play_tone, 0x1A4A2A);
  make_btn(scr, LV_SYMBOL_STOP " Stop", bx + bw + gap, by, bw, bh, cb_stop, 0x4A1A1A);

  // Frequency buttons
  by += bh + gap;
  make_btn(scr, LV_SYMBOL_DOWN " Note", bx, by, bw, bh, cb_freq_down);
  make_btn(scr, LV_SYMBOL_UP " Note", bx + bw + gap, by, bw, bh, cb_freq_up);

  // Volume buttons
  by += bh + gap;
  make_btn(scr, LV_SYMBOL_MINUS " Vol", bx, by, bw, bh, cb_vol_down);
  make_btn(scr, LV_SYMBOL_PLUS " Vol", bx + bw + gap, by, bw, bh, cb_vol_up);

  // Scale + WAV buttons
  by += bh + gap;
  make_btn(scr, LV_SYMBOL_REFRESH " Scale", bx, by, bw, bh, cb_scale, 0x2A2A5A);
  make_btn(scr, LV_SYMBOL_AUDIO " WAV", bx + bw + gap, by, bw, bh, cb_play_wav, 0x3A2A4A);

  // Right side: big visual indicator
  lv_obj_t *note_lbl = lv_label_create(scr);
  lv_label_set_text(note_lbl, LV_SYMBOL_AUDIO);
  lv_obj_set_style_text_font(note_lbl, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_color(note_lbl, lv_color_hex(0x333355), 0);
  lv_obj_set_pos(note_lbl, 800, 250);

  // Serial command help
  lv_obj_t *help_lbl = lv_label_create(scr);
  lv_label_set_text(help_lbl,
    "Serial: p=play  s=stop  +=vol up  -=vol down\n"
    "        u=freq up  d=freq down  c=scale  w=wav");
  lv_obj_set_style_text_font(help_lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(help_lbl, lv_color_hex(0x555555), 0);
  lv_obj_set_pos(help_lbl, 20, 550);
}

// ═══════════════════════════════════════════════════════════
//  Serial Commands
// ═══════════════════════════════════════════════════════════

static void handle_serial() {
  while (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 'p': cb_play_tone(NULL); break;
      case 's': cb_stop(NULL); break;
      case '+': cb_vol_up(NULL); break;
      case '-': cb_vol_down(NULL); break;
      case 'u': cb_freq_up(NULL); break;
      case 'd': cb_freq_down(NULL); break;
      case 'c': cb_scale(NULL); break;
      case 'w': cb_play_wav(NULL); break;
      case '?':
        Serial.println("Commands: p=play s=stop +=vol+ -=vol- u=freq+ d=freq- c=scale w=wav");
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
  Serial.println("║  CrowPanel P4 — Sound Test           ║");
  Serial.println("╚══════════════════════════════════════╝\n");

  // 1. Init SD FIRST (before display — LDO4 kills DSI if done after)
  Serial.println("[SD] Initializing SD_MMC (1-bit mode)...");
  SD_MMC.setPins(43, 44, 39);
  if (SD_MMC.begin("/sdcard", true)) {
    sd_ready = true;
    Serial.printf("[SD] OK — %lluMB %s\n",
                  SD_MMC.cardSize() / (1024 * 1024),
                  SD_MMC.cardType() == CARD_SDHC ? "SDHC" : "SD");
  } else {
    Serial.println("[SD] Not available (continuing without SD)");
  }
  delay(100);

  // 2. Init display + touch
  Serial.println("[DISP] Initializing display + touch...");
  init_hardware();
  Serial.println("[DISP] OK");

  // 3. Build UI
  build_ui();
  lv_timer_handler();

  // 4. Init amplifier (OFF first — avoid pop/distortion per cookbook)
  Serial.println("[AMP] Configuring amplifier enable pin...");
  set_amp(false);
  delay(50);

  // 5. Init I2S
  Serial.println("[I2S] Initializing I2S for NS4168...");
  Serial.printf("[I2S] Pins: BCLK=%d, LRCLK=%d, SDATA=%d\n",
                I2S_BCLK_PIN, I2S_LRCLK_PIN, I2S_SDATA_PIN);
  i2s_ready = init_i2s();

  if (i2s_ready) {
    Serial.println("[I2S] *** I2S READY ***");

    // 6. Auto-test: play a 1-second 440Hz beep at startup
    Serial.println("\n[TEST] Playing startup beep (440Hz, 1 sec)...");
    set_amp(true);
    delay(50);  // Let amp settle

    float ph = 0.0f;
    float phase_inc = TWO_PI * 440.0f / SAMPLE_RATE;
    int total = SAMPLE_RATE;  // 1 second
    int16_t buf[256 * 2];

    while (total > 0) {
      int chunk = (total > 256) ? 256 : total;
      for (int i = 0; i < chunk; i++) {
        int16_t s = (int16_t)(sinf(ph) * 16000.0f);  // ~50% volume
        buf[i * 2] = s;       // L
        buf[i * 2 + 1] = s;   // R
        ph += phase_inc;
        if (ph >= TWO_PI) ph -= TWO_PI;
      }
      size_t bw = 0;
      esp_err_t wr = i2s_channel_write(tx_handle, buf, chunk * 4, &bw, 200);
      if (wr != ESP_OK) {
        Serial.printf("[TEST] Write error: 0x%x (%s)\n", wr, esp_err_to_name(wr));
        break;
      }
      total -= chunk;
    }
    Serial.println("[TEST] Beep done. Did you hear it?");

    // Try flipping amp polarity if no sound
    Serial.println("[TEST] Now trying OPPOSITE amp polarity...");
    gpio_set_level((gpio_num_t)AMP_EN_PIN, 1);  // Try HIGH
    Serial.printf("[AMP] GPIO%d = HIGH\n", AMP_EN_PIN);
    delay(50);

    ph = 0.0f;
    phase_inc = TWO_PI * 880.0f / SAMPLE_RATE;  // 880Hz to differentiate
    total = SAMPLE_RATE;  // 1 second
    while (total > 0) {
      int chunk = (total > 256) ? 256 : total;
      for (int i = 0; i < chunk; i++) {
        int16_t s = (int16_t)(sinf(ph) * 16000.0f);
        buf[i * 2] = s;
        buf[i * 2 + 1] = s;
        ph += phase_inc;
        if (ph >= TWO_PI) ph -= TWO_PI;
      }
      size_t bw = 0;
      i2s_channel_write(tx_handle, buf, chunk * 4, &bw, 200);
      total -= chunk;
    }
    Serial.println("[TEST] Second beep done (opposite polarity).");
    Serial.println("[TEST] If you heard the 2nd but not 1st, amp is active HIGH.");
    Serial.println("[TEST] If you heard the 1st but not 2nd, amp is active LOW.\n");

    set_amp(false);  // Turn off after test
  } else {
    Serial.println("[I2S] *** I2S FAILED — check serial output ***");
  }

  // 7. Update UI with status
  update_ui();
  lv_timer_handler();

  Serial.println("\nReady! Press '?' for commands.");
  Serial.println("Touch the screen buttons or use serial commands.\n");
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

  // Audio streaming
  if (tone_playing) {
    generate_and_send_tone();
  } else if (wav_playing) {
    stream_wav_chunk();
  }

  // UI updates
  static uint32_t last_ui = 0;
  if (millis() - last_ui > 200) {
    last_ui = millis();
    update_ui();
  }

  // LVGL
  lv_timer_handler();

  // Serial
  handle_serial();

  delay(5);
}
