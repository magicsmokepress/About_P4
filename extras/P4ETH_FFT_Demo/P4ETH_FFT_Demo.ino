/*******************************************************************************
 * P4ETH_FFT_Demo.ino
 *
 * ESP-DSP FFT Signal Analysis Demo for Waveshare ESP32-P4-ETH
 *
 * Demonstrates:
 *   - Generating synthetic signals (sum of sine waves + noise)
 *   - Applying a Hann (Hanning) window to reduce spectral leakage
 *   - Performing a real-valued FFT using Espressif's esp_dsp library
 *   - Computing the power spectrum in dB
 *   - Detecting peak frequencies
 *   - Rendering frequency-domain plots on LovyanGFX
 *   - Interactive serial commands to change signal parameters
 *
 * Signal Model:
 *   x(t) = A1*sin(2*pi*f1*t) + A2*sin(2*pi*f2*t) + noise
 *   where f1, f2, A1, A2 are user-configurable via Serial.
 *
 * Hardware: Waveshare ESP32-P4-ETH
 *   - Display: ILI9488 320x480 SPI (SCLK=26, MOSI=23, MISO=27, DC=22, CS=20, RST=21)
 *
 * Required Libraries:
 *   - LovyanGFX (display driver)
 *   - esp_dsp   (ESP-DSP, included with ESP-IDF / arduino-esp32 core 3.x)
 *                Add to platformio.ini: lib_deps = espressif/esp-dsp
 *
 * Board: Waveshare ESP32-P4-ETH (ESP32-P4, arduino-esp32 core 3.x)
 ******************************************************************************/

#include <Arduino.h>
#include <math.h>
#include <esp_dsp.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// ---------------------------------------------------------------------------
// LovyanGFX display configuration for Waveshare ESP32-P4-ETH (ILI9488 SPI)
// ---------------------------------------------------------------------------
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9488 _panel;
  lgfx::Bus_SPI       _bus;

public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host   = SPI2_HOST;
      cfg.spi_mode   = 0;
      cfg.freq_write  = 40000000;
      cfg.freq_read   = 16000000;
      cfg.pin_sclk   = 26;
      cfg.pin_mosi   = 23;
      cfg.pin_miso   = 27;
      cfg.pin_dc     = 22;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs   = 20;
      cfg.pin_rst  = 21;
      cfg.pin_busy = -1;
      cfg.panel_width  = 320;
      cfg.panel_height = 480;
      cfg.offset_x     = 0;
      cfg.offset_y     = 0;
      cfg.readable     = true;
      cfg.invert       = false;
      cfg.rgb_order    = false;
      cfg.dlen_16bit   = false;
      cfg.bus_shared   = true;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};

static LGFX tft;

// ---------------------------------------------------------------------------
// FFT configuration
// ---------------------------------------------------------------------------
static const int FFT_SIZE   = 1024;           // must be power of 2
static const float SAMPLE_RATE = 10000.0f;    // simulated sample rate (Hz)

// Signal buffers
// dsps_fft2r_fc32 operates in-place on an interleaved complex array.
// For a real FFT of N points, we need 2*N floats (real + imaginary parts).
static float fftBuf[FFT_SIZE * 2]  __attribute__((aligned(16)));
static float window[FFT_SIZE]      __attribute__((aligned(16)));
static float powerSpectrum[FFT_SIZE / 2];  // magnitude^2 in dB

// Signal parameters (user-adjustable)
static float sigFreq1 = 440.0f;   // Hz (A4 note)
static float sigFreq2 = 1200.0f;  // Hz
static float sigAmp1  = 1.0f;
static float sigAmp2  = 0.5f;
static float noiseAmp = 0.1f;

// Display layout (landscape 480x320)
static const int PLOT_X      = 40;
static const int PLOT_Y      = 50;
static const int PLOT_W      = 420;
static const int PLOT_H      = 200;
static const int PLOT_BOTTOM = PLOT_Y + PLOT_H;

// Power spectrum range for display (dB)
static const float DB_MIN = -60.0f;
static const float DB_MAX =  20.0f;

// ---------------------------------------------------------------------------
// Simple pseudo-random noise (xorshift32)
// ---------------------------------------------------------------------------
static uint32_t rngState = 12345;

static float randNoise() {
  rngState ^= rngState << 13;
  rngState ^= rngState >> 17;
  rngState ^= rngState << 5;
  // Map to roughly [-1, +1]
  return ((float)(int32_t)rngState / (float)INT32_MAX);
}

// ---------------------------------------------------------------------------
// Generate the synthetic signal into fftBuf (real part only)
// ---------------------------------------------------------------------------
static void generateSignal() {
  float dt = 1.0f / SAMPLE_RATE;
  for (int i = 0; i < FFT_SIZE; i++) {
    float t = i * dt;
    float sample = sigAmp1 * sinf(2.0f * M_PI * sigFreq1 * t)
                 + sigAmp2 * sinf(2.0f * M_PI * sigFreq2 * t)
                 + noiseAmp * randNoise();
    fftBuf[i * 2]     = sample;   // real part
    fftBuf[i * 2 + 1] = 0.0f;    // imaginary part
  }
}

// ---------------------------------------------------------------------------
// Apply Hann window to the real parts of fftBuf
// ---------------------------------------------------------------------------
static void applyWindow() {
  // Generate Hann window coefficients
  dsps_wind_hann_f32(window, FFT_SIZE);

  // Multiply real parts by window (imaginary parts stay 0)
  for (int i = 0; i < FFT_SIZE; i++) {
    fftBuf[i * 2] *= window[i];
  }
}

// ---------------------------------------------------------------------------
// Perform FFT and compute power spectrum in dB
// ---------------------------------------------------------------------------
static void computeFFT() {
  // Initialize FFT tables (safe to call repeatedly; internally cached)
  esp_err_t err = dsps_fft2r_init_fc32(NULL, FFT_SIZE);
  if (err != ESP_OK) {
    Serial.printf("FFT init error: %d\n", err);
    return;
  }

  // In-place complex FFT
  dsps_fft2r_fc32(fftBuf, FFT_SIZE);

  // Bit-reversal reordering
  dsps_bit_rev_fc32(fftBuf, FFT_SIZE);

  // Compute power spectrum (first half only — Nyquist symmetry)
  // P(k) = 10 * log10( re^2 + im^2 )
  int halfN = FFT_SIZE / 2;
  for (int k = 0; k < halfN; k++) {
    float re = fftBuf[k * 2];
    float im = fftBuf[k * 2 + 1];
    float mag2 = re * re + im * im;
    // Normalize by FFT_SIZE^2 and avoid log(0)
    float norm = mag2 / ((float)FFT_SIZE * (float)FFT_SIZE);
    if (norm < 1e-12f) norm = 1e-12f;
    powerSpectrum[k] = 10.0f * log10f(norm);
  }
}

// ---------------------------------------------------------------------------
// Find peak frequency in power spectrum
// ---------------------------------------------------------------------------
struct PeakInfo {
  float freq;
  float db;
  int   bin;
};

static PeakInfo findPeak(int startBin = 1, int endBin = -1) {
  int halfN = FFT_SIZE / 2;
  if (endBin < 0 || endBin > halfN) endBin = halfN;

  PeakInfo peak = {0, -999.0f, 0};
  float binWidth = SAMPLE_RATE / FFT_SIZE;

  for (int k = startBin; k < endBin; k++) {
    if (powerSpectrum[k] > peak.db) {
      peak.db   = powerSpectrum[k];
      peak.bin  = k;
      peak.freq = k * binWidth;
    }
  }
  return peak;
}

// Find second peak (avoiding the first peak's neighborhood)
static PeakInfo findSecondPeak(const PeakInfo& first, int guardBins = 10) {
  int halfN = FFT_SIZE / 2;
  PeakInfo peak = {0, -999.0f, 0};
  float binWidth = SAMPLE_RATE / FFT_SIZE;

  for (int k = 1; k < halfN; k++) {
    if (abs(k - first.bin) < guardBins) continue;  // skip near first peak
    if (powerSpectrum[k] > peak.db) {
      peak.db   = powerSpectrum[k];
      peak.bin  = k;
      peak.freq = k * binWidth;
    }
  }
  return peak;
}

// ---------------------------------------------------------------------------
// Draw the frequency-domain plot on display
// ---------------------------------------------------------------------------
static void drawPlot() {
  int halfN = FFT_SIZE / 2;
  float binWidth = SAMPLE_RATE / FFT_SIZE;
  float maxFreq  = SAMPLE_RATE / 2.0f;

  // Clear plot area
  tft.fillRect(0, 0, 480, 320, TFT_BLACK);

  // Title
  tft.setFont(&fonts::Font2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 4);
  tft.print("ESP-DSP FFT Signal Analyzer");

  // Signal info line
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(10, 24);
  tft.printf("f1=%.0fHz(%.1f)  f2=%.0fHz(%.1f)  noise=%.2f  N=%d  Fs=%.0f",
             sigFreq1, sigAmp1, sigFreq2, sigAmp2, noiseAmp,
             FFT_SIZE, SAMPLE_RATE);

  // Plot border
  tft.drawRect(PLOT_X - 1, PLOT_Y - 1, PLOT_W + 2, PLOT_H + 2, TFT_DARKGREY);

  // Y-axis labels (dB)
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  for (float db = DB_MIN; db <= DB_MAX; db += 20.0f) {
    int py = PLOT_BOTTOM - (int)((db - DB_MIN) / (DB_MAX - DB_MIN) * PLOT_H);
    if (py >= PLOT_Y && py <= PLOT_BOTTOM) {
      tft.setCursor(2, py - 6);
      tft.printf("%+.0f", db);
      tft.drawFastHLine(PLOT_X, py, PLOT_W, 0x202020U);  // grid line
    }
  }

  // X-axis labels (frequency)
  for (float f = 0; f <= maxFreq; f += 1000.0f) {
    int px = PLOT_X + (int)(f / maxFreq * PLOT_W);
    tft.setCursor(px - 8, PLOT_BOTTOM + 6);
    tft.printf("%.0fk", f / 1000.0f);
    tft.drawFastVLine(px, PLOT_Y, PLOT_H, 0x202020U);  // grid line
  }

  // Plot the power spectrum as filled bars
  // Map each bin to a screen X position
  for (int k = 1; k < halfN; k++) {
    float freq = k * binWidth;
    int px = PLOT_X + (int)(freq / maxFreq * PLOT_W);
    if (px < PLOT_X || px >= PLOT_X + PLOT_W) continue;

    float db = powerSpectrum[k];
    db = constrain(db, DB_MIN, DB_MAX);
    int barH = (int)((db - DB_MIN) / (DB_MAX - DB_MIN) * PLOT_H);
    if (barH < 1) continue;

    int py = PLOT_BOTTOM - barH;

    // Color: gradient from blue (low) to cyan (mid) to green (high)
    uint16_t color;
    if (db < -30) {
      color = tft.color565(0, 0, 100 + (int)((db - DB_MIN) / (-30 - DB_MIN) * 155));
    } else if (db < 0) {
      int g = (int)((db + 30) / 30.0f * 255);
      color = tft.color565(0, g, 255 - g);
    } else {
      int r = (int)(db / DB_MAX * 255);
      color = tft.color565(r, 255, 0);
    }

    tft.drawFastVLine(px, py, barH, color);
  }

  // Mark detected peaks
  PeakInfo peak1 = findPeak();
  PeakInfo peak2 = findSecondPeak(peak1);

  auto drawPeakMarker = [&](const PeakInfo& p, uint16_t color, int yOff) {
    if (p.freq <= 0) return;
    int px = PLOT_X + (int)(p.freq / maxFreq * PLOT_W);
    float dbC = constrain(p.db, DB_MIN, DB_MAX);
    int py = PLOT_BOTTOM - (int)((dbC - DB_MIN) / (DB_MAX - DB_MIN) * PLOT_H);

    // Draw triangle marker above the peak
    tft.fillTriangle(px - 4, py - 8, px + 4, py - 8, px, py - 2, color);

    // Label
    tft.setTextColor(color, TFT_BLACK);
    tft.setCursor(px - 30, py - 22 + yOff);
    tft.printf("%.0f Hz", p.freq);
  };

  drawPeakMarker(peak1, TFT_YELLOW, 0);
  drawPeakMarker(peak2, TFT_MAGENTA, -14);

  // Peak info text at bottom
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(10, 296);
  tft.printf("Peak 1: %.1f Hz (%.1f dB)", peak1.freq, peak1.db);

  tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
  tft.setCursor(250, 296);
  tft.printf("Peak 2: %.1f Hz (%.1f dB)", peak2.freq, peak2.db);
}

// ---------------------------------------------------------------------------
// Process Serial commands
// ---------------------------------------------------------------------------
static void processSerialCommand() {
  if (!Serial.available()) return;

  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  // Parse command: "f1 440", "f2 1200", "a1 1.0", "a2 0.5", "noise 0.1", "help"
  int spaceIdx = line.indexOf(' ');
  String cmd, val;
  if (spaceIdx > 0) {
    cmd = line.substring(0, spaceIdx);
    val = line.substring(spaceIdx + 1);
  } else {
    cmd = line;
  }
  cmd.toLowerCase();

  if (cmd == "f1") {
    sigFreq1 = constrain(val.toFloat(), 1.0f, SAMPLE_RATE / 2.0f);
    Serial.printf("Frequency 1 set to %.1f Hz\n", sigFreq1);
  } else if (cmd == "f2") {
    sigFreq2 = constrain(val.toFloat(), 1.0f, SAMPLE_RATE / 2.0f);
    Serial.printf("Frequency 2 set to %.1f Hz\n", sigFreq2);
  } else if (cmd == "a1") {
    sigAmp1 = constrain(val.toFloat(), 0.0f, 10.0f);
    Serial.printf("Amplitude 1 set to %.2f\n", sigAmp1);
  } else if (cmd == "a2") {
    sigAmp2 = constrain(val.toFloat(), 0.0f, 10.0f);
    Serial.printf("Amplitude 2 set to %.2f\n", sigAmp2);
  } else if (cmd == "noise") {
    noiseAmp = constrain(val.toFloat(), 0.0f, 5.0f);
    Serial.printf("Noise amplitude set to %.3f\n", noiseAmp);
  } else if (cmd == "help" || cmd == "?") {
    Serial.println("\n=== FFT Demo Serial Commands ===");
    Serial.println("  f1 <Hz>      Set frequency 1 (e.g., f1 440)");
    Serial.println("  f2 <Hz>      Set frequency 2 (e.g., f2 1200)");
    Serial.println("  a1 <amp>     Set amplitude 1 (e.g., a1 1.0)");
    Serial.println("  a2 <amp>     Set amplitude 2 (e.g., a2 0.5)");
    Serial.println("  noise <amp>  Set noise level (e.g., noise 0.1)");
    Serial.println("  help         Show this menu");
    Serial.printf("\nSample rate: %.0f Hz, FFT size: %d, Bin width: %.2f Hz\n",
                  SAMPLE_RATE, FFT_SIZE, SAMPLE_RATE / FFT_SIZE);
    Serial.printf("Nyquist freq: %.0f Hz\n", SAMPLE_RATE / 2.0f);
  } else {
    Serial.printf("Unknown command: '%s' (type 'help' for options)\n", cmd.c_str());
    return;  // don't trigger re-analysis
  }

  // Re-generate and re-display after parameter change
  generateSignal();
  applyWindow();
  computeFFT();
  drawPlot();

  PeakInfo p1 = findPeak();
  PeakInfo p2 = findSecondPeak(p1);
  Serial.printf("Detected peaks: %.1f Hz (%.1f dB), %.1f Hz (%.1f dB)\n",
                p1.freq, p1.db, p2.freq, p2.db);
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== P4ETH FFT Signal Analysis Demo ===");
  Serial.printf("FFT size: %d, Sample rate: %.0f Hz\n", FFT_SIZE, SAMPLE_RATE);
  Serial.printf("Frequency resolution: %.2f Hz/bin\n", SAMPLE_RATE / FFT_SIZE);
  Serial.println("Type 'help' for serial commands.\n");

  // Initialize display (landscape)
  tft.init();
  tft.setRotation(1);   // landscape 480x320
  tft.fillScreen(TFT_BLACK);

  tft.setFont(&fonts::Font2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 150);
  tft.print("Computing FFT...");

  // Generate initial signal, apply window, compute FFT
  generateSignal();
  applyWindow();
  computeFFT();

  // Draw the frequency-domain plot
  drawPlot();

  PeakInfo p1 = findPeak();
  PeakInfo p2 = findSecondPeak(p1);
  Serial.printf("Initial peaks: %.1f Hz (%.1f dB), %.1f Hz (%.1f dB)\n",
                p1.freq, p1.db, p2.freq, p2.db);
  Serial.println("Ready. Change frequencies with 'f1 <Hz>' / 'f2 <Hz>'.");
}

// ---------------------------------------------------------------------------
// Main loop — re-compute periodically and check serial commands
// ---------------------------------------------------------------------------
void loop() {
  // Check for serial commands (non-blocking)
  processSerialCommand();

  // Auto-refresh every 2 seconds (noise will vary each time)
  static uint32_t lastUpdate = 0;
  if (millis() - lastUpdate >= 2000) {
    lastUpdate = millis();
    generateSignal();
    applyWindow();
    computeFFT();
    drawPlot();
  }

  delay(10);
}
