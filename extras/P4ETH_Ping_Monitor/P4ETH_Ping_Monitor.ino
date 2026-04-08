/*******************************************************************************
 * P4ETH_Ping_Monitor.ino
 *
 * ICMP Ping Monitor with RTT Display for Waveshare ESP32-P4-ETH
 *
 * Demonstrates:
 *   - Ethernet initialization with IP101 PHY on ESP32-P4
 *   - ICMP ping using the ESP32Ping library
 *   - Real-time RTT (Round Trip Time) visualization with color coding
 *   - Packet loss tracking and percentage display
 *   - Rolling bar graph of recent RTT measurements
 *   - LovyanGFX display driver for ILI9488 on SPI
 *
 * Hardware: Waveshare ESP32-P4-ETH
 *   - Ethernet PHY: IP101 (MDC=31, MDIO=52, POWER=51)
 *   - Display: ILI9488 320x480 SPI (SCLK=26, MOSI=23, MISO=27, DC=22, CS=20, RST=21)
 *
 * Required Libraries:
 *   - LovyanGFX (display driver)
 *   - ESP32Ping (ICMP ping)
 *   - ETH (built-in ESP32 Ethernet)
 *
 * Board: Waveshare ESP32-P4-ETH (ESP32-P4, arduino-esp32 core 3.x)
 ******************************************************************************/

#include <ETH.h>
#include <ESP32Ping.h>

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
      cfg.spi_host  = SPI2_HOST;
      cfg.spi_mode  = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read  = 16000000;
      cfg.pin_sclk  = 26;
      cfg.pin_mosi  = 23;
      cfg.pin_miso  = 27;
      cfg.pin_dc    = 22;
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
// Ping configuration
// ---------------------------------------------------------------------------
static const uint8_t NUM_TARGETS  = 3;
static const uint8_t HISTORY_LEN  = 20;   // bar graph depth
static const uint32_t PING_INTERVAL_MS = 5000;

struct PingTarget {
  const char* label;
  IPAddress   ip;
  float       rttHistory[HISTORY_LEN];
  uint32_t    sent;
  uint32_t    lost;
  uint8_t     histIdx;       // next write position (circular)
  uint8_t     histCount;     // how many entries are filled
};

static PingTarget targets[NUM_TARGETS];
static bool ethConnected = false;

// ---------------------------------------------------------------------------
// Ethernet event handler
// ---------------------------------------------------------------------------
static void onEthEvent(arduino_event_id_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("[ETH] Started");
      ETH.setHostname("esp32p4-ping");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("[ETH] Link up");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.printf("[ETH] IP: %s  GW: %s\n",
                     ETH.localIP().toString().c_str(),
                     ETH.gatewayIP().toString().c_str());
      ethConnected = true;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("[ETH] Link down");
      ethConnected = false;
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Color helpers
// ---------------------------------------------------------------------------
static uint16_t rttColor(float rtt) {
  if (rtt < 0)    return TFT_DARKGREY;   // timeout / no data
  if (rtt < 50)   return TFT_GREEN;
  if (rtt < 150)  return TFT_YELLOW;
  return TFT_RED;
}

// ---------------------------------------------------------------------------
// Draw the header (IP info, titles)
// ---------------------------------------------------------------------------
static void drawHeader() {
  tft.fillRect(0, 0, 480, 36, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(4, 4);
  tft.setFont(&fonts::Font2);

  if (ethConnected) {
    tft.printf("IP: %s   GW: %s",
               ETH.localIP().toString().c_str(),
               ETH.gatewayIP().toString().c_str());
  } else {
    tft.print("Ethernet: waiting for link...");
  }

  tft.setCursor(4, 22);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.print("ICMP Ping Monitor");
}

// ---------------------------------------------------------------------------
// Draw one target panel (RTT text + bar graph)
// Panel layout (landscape 480x320): 3 panels stacked vertically below header
// ---------------------------------------------------------------------------
static void drawTarget(uint8_t idx) {
  const int panelH   = 88;
  const int panelY   = 40 + idx * panelH;
  const int barAreaX = 200;
  const int barW     = 12;
  const int barGap   = 2;
  const int maxBarH  = 70;

  PingTarget& t = targets[idx];

  // Background
  tft.fillRect(0, panelY, 480, panelH, TFT_BLACK);

  // Label
  tft.setFont(&fonts::Font2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(4, panelY + 4);
  tft.printf("%s", t.label);

  // IP address
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(4, panelY + 20);
  tft.printf("%s", t.ip.toString().c_str());

  // Latest RTT
  float latestRtt = -1;
  if (t.histCount > 0) {
    uint8_t lastIdx = (t.histIdx + HISTORY_LEN - 1) % HISTORY_LEN;
    latestRtt = t.rttHistory[lastIdx];
  }

  tft.setCursor(4, panelY + 38);
  if (latestRtt >= 0) {
    tft.setTextColor(rttColor(latestRtt), TFT_BLACK);
    tft.printf("RTT: %.0f ms", latestRtt);
  } else {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.print("RTT: ---");
  }

  // Packet loss
  tft.setCursor(4, panelY + 56);
  float loss = (t.sent > 0) ? (100.0f * t.lost / t.sent) : 0;
  uint16_t lossColor = (loss < 1) ? TFT_GREEN : (loss < 10) ? TFT_YELLOW : TFT_RED;
  tft.setTextColor(lossColor, TFT_BLACK);
  tft.printf("Loss: %.1f%% (%u/%u)", loss, t.lost, t.sent);

  // Bar graph of last HISTORY_LEN RTT values
  // Draw from oldest to newest, left to right
  for (int i = 0; i < HISTORY_LEN; i++) {
    int dataIdx;
    if (t.histCount < HISTORY_LEN) {
      // Buffer not full yet
      if (i >= t.histCount) break;
      dataIdx = i;
    } else {
      // Circular: oldest is at histIdx
      dataIdx = (t.histIdx + i) % HISTORY_LEN;
    }
    float rtt = t.rttHistory[dataIdx];
    int x = barAreaX + i * (barW + barGap);
    if (rtt < 0) {
      // Timeout: draw a small red X marker
      tft.drawLine(x, panelY + maxBarH - 2, x + barW, panelY + maxBarH + 4, TFT_RED);
      tft.drawLine(x + barW, panelY + maxBarH - 2, x, panelY + maxBarH + 4, TFT_RED);
    } else {
      // Scale: 0..300ms maps to 0..maxBarH pixels
      int barH = constrain((int)(rtt * maxBarH / 300.0f), 1, maxBarH);
      int barY = panelY + (maxBarH - barH) + 4;
      tft.fillRect(x, barY, barW, barH, rttColor(rtt));
    }
  }

  // Separator line
  tft.drawFastHLine(0, panelY + panelH - 1, 480, TFT_DARKGREY);
}

// ---------------------------------------------------------------------------
// Perform one round of pings
// ---------------------------------------------------------------------------
static void pingAllTargets() {
  for (uint8_t i = 0; i < NUM_TARGETS; i++) {
    PingTarget& t = targets[i];
    t.sent++;

    bool ok = Ping.ping(t.ip, 1);   // send 1 ICMP echo
    if (ok) {
      float rtt = Ping.averageTime();
      t.rttHistory[t.histIdx] = rtt;
      Serial.printf("[PING] %s (%s): %.0f ms\n", t.label, t.ip.toString().c_str(), rtt);
    } else {
      t.rttHistory[t.histIdx] = -1;  // timeout sentinel
      t.lost++;
      Serial.printf("[PING] %s (%s): TIMEOUT\n", t.label, t.ip.toString().c_str());
    }
    t.histIdx = (t.histIdx + 1) % HISTORY_LEN;
    if (t.histCount < HISTORY_LEN) t.histCount++;

    drawTarget(i);
  }
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== P4ETH Ping Monitor ===");

  // Initialize display (landscape)
  tft.init();
  tft.setRotation(1);   // landscape 480x320
  tft.fillScreen(TFT_BLACK);
  tft.setFont(&fonts::Font2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 140);
  tft.print("Initializing Ethernet...");

  // Initialize Ethernet — IP101 PHY on Waveshare ESP32-P4-ETH
  Network.onEvent(onEthEvent);
  ETH.begin(ETH_PHY_IP101,     // PHY type
            1,                  // PHY address
            31,                 // MDC pin
            52,                 // MDIO pin
            51,                 // Power pin
            ETH_CLOCK_GPIO0_IN  // External clock input
  );

  // Configure ping targets
  // Target 0: gateway (will be updated once we get an IP)
  targets[0].label = "Gateway";
  targets[0].ip    = IPAddress(0, 0, 0, 0);
  targets[0].sent  = 0;  targets[0].lost = 0;
  targets[0].histIdx = 0; targets[0].histCount = 0;

  targets[1].label = "Google DNS";
  targets[1].ip    = IPAddress(8, 8, 8, 8);
  targets[1].sent  = 0;  targets[1].lost = 0;
  targets[1].histIdx = 0; targets[1].histCount = 0;

  targets[2].label = "Cloudflare";
  targets[2].ip    = IPAddress(1, 1, 1, 1);
  targets[2].sent  = 0;  targets[2].lost = 0;
  targets[2].histIdx = 0; targets[2].histCount = 0;

  // Initialize RTT history to -1 (no data)
  for (int t = 0; t < NUM_TARGETS; t++)
    for (int h = 0; h < HISTORY_LEN; h++)
      targets[t].rttHistory[h] = -1;

  // Wait for Ethernet to get an IP (up to 15 seconds)
  Serial.println("Waiting for Ethernet IP...");
  uint32_t t0 = millis();
  while (!ethConnected && (millis() - t0 < 15000)) {
    delay(100);
  }

  if (ethConnected) {
    // Update gateway target to actual gateway
    targets[0].ip = ETH.gatewayIP();
    Serial.printf("Gateway target set to %s\n", targets[0].ip.toString().c_str());
  } else {
    Serial.println("WARNING: No Ethernet IP yet; pings may fail.");
    targets[0].ip = IPAddress(192, 168, 1, 1);  // fallback guess
  }

  tft.fillScreen(TFT_BLACK);
  drawHeader();
  for (int i = 0; i < NUM_TARGETS; i++) drawTarget(i);
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------
void loop() {
  static uint32_t lastPing = 0;

  if (millis() - lastPing >= PING_INTERVAL_MS) {
    lastPing = millis();

    if (ethConnected) {
      // Refresh gateway IP in case it changed (e.g., DHCP renewal)
      targets[0].ip = ETH.gatewayIP();
      drawHeader();
      pingAllTargets();
    } else {
      drawHeader();
      Serial.println("[PING] Skipped — no Ethernet link");
    }
  }

  delay(10);  // yield
}
