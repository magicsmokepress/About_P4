/*******************************************************************************
 * P4ETH_FreeRTOS_DualCore.ino
 *
 * FreeRTOS Dual-Core Demo with Mutexes and Task Notification
 *
 * Demonstrates:
 *   - xTaskCreatePinnedToCore() to run tasks on specific cores
 *   - SemaphoreHandle_t mutex for shared data protection
 *   - xTaskNotifyGive() / ulTaskNotifyTake() for task synchronization
 *   - std::atomic for a lightweight cross-core flag
 *   - Stack high water mark monitoring with uxTaskGetStackHighWaterMark()
 *   - Real-time task statistics display on LovyanGFX
 *
 * Architecture:
 *   Core 0: sensorTask  — generates simulated sensor readings every 100ms,
 *                          writes to shared struct under mutex, then notifies
 *                          the display task.
 *   Core 1: displayTask — waits for notification, reads shared data under
 *                          mutex, and renders stats on the ILI9488 display.
 *   Main:   loop()      — prints periodic stats to Serial.
 *
 * Hardware: Waveshare ESP32-P4-ETH
 *   - Display: ILI9488 320x480 SPI (SCLK=26, MOSI=23, MISO=27, DC=22, CS=20, RST=21)
 *
 * Required Libraries:
 *   - LovyanGFX (display driver)
 *   - FreeRTOS (built-in with ESP32 Arduino core)
 *
 * Board: Waveshare ESP32-P4-ETH (ESP32-P4, arduino-esp32 core 3.x)
 ******************************************************************************/

#include <Arduino.h>
#include <atomic>

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
// Shared data protected by mutex
// ---------------------------------------------------------------------------
struct SensorData {
  float    temperature;     // simulated temperature (C)
  float    humidity;        // simulated humidity (%)
  float    pressure;        // simulated pressure (hPa)
  uint32_t readingCount;    // total readings taken
  uint32_t timestamp;       // millis() of last reading
};

static SensorData sharedData = {0, 0, 0, 0, 0};
static SemaphoreHandle_t dataMutex = nullptr;

// Mutex contention counter — incremented when a task has to wait
static volatile uint32_t contentionCount = 0;

// Cross-core flag using std::atomic (lock-free on ESP32-P4)
// Used to signal the display task that fresh data is "interesting"
// (e.g., temperature exceeded a threshold)
static std::atomic<bool> alertFlag(false);

// Task handles (needed for notification and stack monitoring)
static TaskHandle_t sensorTaskHandle  = nullptr;
static TaskHandle_t displayTaskHandle = nullptr;

// ---------------------------------------------------------------------------
// Simulated sensor reading (deterministic, no real hardware needed)
// ---------------------------------------------------------------------------
static float simTemperature(uint32_t tick) {
  // Sine wave centered at 25C, amplitude 10C, period ~30 seconds
  return 25.0f + 10.0f * sinf(tick * 0.0002f);
}

static float simHumidity(uint32_t tick) {
  // Slower sine wave centered at 55%, amplitude 20%
  return 55.0f + 20.0f * sinf(tick * 0.00008f);
}

static float simPressure(uint32_t tick) {
  // Very slow drift centered at 1013 hPa
  return 1013.0f + 5.0f * sinf(tick * 0.00003f);
}

// ---------------------------------------------------------------------------
// sensorTask — runs on Core 0
// Reads simulated sensor data every 100ms, writes to shared struct.
// ---------------------------------------------------------------------------
static void sensorTask(void* param) {
  (void)param;
  Serial.printf("[sensorTask] Started on core %d\n", xPortGetCoreID());

  for (;;) {
    uint32_t now = millis();

    // Generate simulated readings
    float temp  = simTemperature(now);
    float hum   = simHumidity(now);
    float press = simPressure(now);

    // Try to acquire mutex (with a short timeout to track contention)
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      sharedData.temperature  = temp;
      sharedData.humidity     = hum;
      sharedData.pressure     = press;
      sharedData.readingCount++;
      sharedData.timestamp    = now;
      xSemaphoreGive(dataMutex);
    } else {
      // Mutex was held by the display task — contention!
      contentionCount++;
    }

    // Set alert flag if temperature exceeds threshold
    if (temp > 33.0f) {
      alertFlag.store(true, std::memory_order_relaxed);
    } else {
      alertFlag.store(false, std::memory_order_relaxed);
    }

    // Notify the display task that new data is available
    xTaskNotifyGive(displayTaskHandle);

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ---------------------------------------------------------------------------
// displayTask — runs on Core 1
// Waits for notification, reads shared data under mutex, updates display.
// ---------------------------------------------------------------------------
static void displayTask(void* param) {
  (void)param;
  Serial.printf("[displayTask] Started on core %d\n", xPortGetCoreID());

  // Local copy of sensor data (filled under mutex, drawn outside it)
  SensorData localData;
  uint32_t   displayUpdates = 0;

  for (;;) {
    // Block until sensorTask notifies us (with a 1-second timeout)
    uint32_t notifyCount = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));

    if (notifyCount == 0) {
      // Timeout — no notification received (sensor task may have stalled)
      continue;
    }

    // Read shared data under mutex
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      localData = sharedData;  // struct copy
      xSemaphoreGive(dataMutex);
    } else {
      contentionCount++;
      continue;  // skip this frame
    }

    displayUpdates++;
    bool alert = alertFlag.load(std::memory_order_relaxed);

    // --- Render to display ---
    // We only redraw changed regions to minimize flicker.

    int y = 0;
    const int lineH = 28;
    const int col1  = 10;
    const int col2  = 250;

    // Title bar
    tft.fillRect(0, y, 480, 34, alert ? TFT_RED : 0x000040U);
    tft.setFont(&fonts::Font4);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(col1, y + 4);
    tft.print("FreeRTOS Dual-Core Demo");
    y += 40;

    tft.setFont(&fonts::Font2);

    // Sensor data section
    tft.fillRect(0, y, 480, lineH * 4 + 10, TFT_BLACK);

    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(col1, y);
    tft.print("--- Sensor Data ---");
    y += lineH;

    tft.setTextColor(alert ? TFT_RED : TFT_GREEN, TFT_BLACK);
    tft.setCursor(col1, y);
    tft.printf("Temperature: %.1f C", localData.temperature);
    if (alert) tft.print("  [ALERT]");
    y += lineH;

    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(col1, y);
    tft.printf("Humidity:    %.1f %%", localData.humidity);
    y += lineH;

    tft.setCursor(col1, y);
    tft.printf("Pressure:    %.1f hPa", localData.pressure);
    y += lineH + 8;

    // Task statistics section
    tft.fillRect(0, y, 480, lineH * 7 + 10, TFT_BLACK);

    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(col1, y);
    tft.print("--- Task Statistics ---");
    y += lineH;

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(col1, y);
    tft.printf("Sensor readings:  %u", localData.readingCount);
    y += lineH;

    tft.setCursor(col1, y);
    tft.printf("Display updates:  %u", displayUpdates);
    y += lineH;

    tft.setCursor(col1, y);
    tft.printf("Mutex contentions: %u", contentionCount);
    y += lineH;

    tft.setCursor(col1, y);
    tft.printf("Uptime: %u s", localData.timestamp / 1000);
    y += lineH + 8;

    // Core assignments and stack info
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(col1, y);
    tft.print("--- Core & Stack ---");
    y += lineH;

    UBaseType_t sensorHWM  = (sensorTaskHandle)  ? uxTaskGetStackHighWaterMark(sensorTaskHandle)  : 0;
    UBaseType_t displayHWM = (displayTaskHandle) ? uxTaskGetStackHighWaterMark(displayTaskHandle) : 0;

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(col1, y);
    tft.printf("sensorTask  -> Core 0  Stack HWM: %u", (unsigned)sensorHWM);
    y += lineH;

    tft.setCursor(col1, y);
    tft.printf("displayTask -> Core 1  Stack HWM: %u", (unsigned)displayHWM);
    y += lineH;

    tft.setCursor(col1, y);
    tft.printf("loop()      -> Core %d", xPortGetCoreID());
    y += lineH;

    // Alert flag indicator
    y += 8;
    tft.fillRect(0, y, 480, lineH, TFT_BLACK);
    tft.setCursor(col1, y);
    tft.setTextColor(alert ? TFT_RED : TFT_DARKGREY, TFT_BLACK);
    tft.printf("std::atomic alertFlag: %s", alert ? "TRUE (temp > 33C)" : "false");
  }
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== P4ETH FreeRTOS Dual-Core Demo ===");
  Serial.printf("setup() running on core %d\n", xPortGetCoreID());

  // Initialize display
  tft.init();
  tft.setRotation(1);  // landscape 480x320
  tft.fillScreen(TFT_BLACK);
  tft.setFont(&fonts::Font2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 150);
  tft.print("Starting FreeRTOS tasks...");

  // Create mutex
  dataMutex = xSemaphoreCreateMutex();
  if (dataMutex == nullptr) {
    Serial.println("FATAL: Failed to create mutex!");
    while (true) delay(1000);
  }

  // Create display task first (so it's ready to receive notifications)
  BaseType_t ret;
  ret = xTaskCreatePinnedToCore(
    displayTask,          // function
    "displayTask",        // name
    8192,                 // stack size (bytes)
    nullptr,              // parameter
    1,                    // priority (1 = normal)
    &displayTaskHandle,   // handle
    1                     // core 1
  );
  if (ret != pdPASS) {
    Serial.println("FATAL: Failed to create displayTask!");
    while (true) delay(1000);
  }

  // Create sensor task
  ret = xTaskCreatePinnedToCore(
    sensorTask,           // function
    "sensorTask",         // name
    4096,                 // stack size (bytes)
    nullptr,              // parameter
    2,                    // priority (higher than display)
    &sensorTaskHandle,    // handle
    0                     // core 0
  );
  if (ret != pdPASS) {
    Serial.println("FATAL: Failed to create sensorTask!");
    while (true) delay(1000);
  }

  Serial.println("Tasks created. sensorTask on core 0, displayTask on core 1.");
}

// ---------------------------------------------------------------------------
// Main loop — periodic Serial stats (runs on whichever core Arduino assigns)
// ---------------------------------------------------------------------------
void loop() {
  static uint32_t lastPrint = 0;

  if (millis() - lastPrint >= 5000) {
    lastPrint = millis();

    Serial.println("--- Periodic Stats ---");

    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      Serial.printf("  Readings: %u  Temp: %.1f C  Hum: %.1f %%  Press: %.1f hPa\n",
                     sharedData.readingCount,
                     sharedData.temperature,
                     sharedData.humidity,
                     sharedData.pressure);
      xSemaphoreGive(dataMutex);
    }

    Serial.printf("  Mutex contentions: %u\n", contentionCount);
    Serial.printf("  Alert flag: %s\n", alertFlag.load() ? "TRUE" : "false");

    if (sensorTaskHandle) {
      Serial.printf("  sensorTask  stack HWM: %u words\n",
                     (unsigned)uxTaskGetStackHighWaterMark(sensorTaskHandle));
    }
    if (displayTaskHandle) {
      Serial.printf("  displayTask stack HWM: %u words\n",
                     (unsigned)uxTaskGetStackHighWaterMark(displayTaskHandle));
    }
  }

  delay(100);
}
