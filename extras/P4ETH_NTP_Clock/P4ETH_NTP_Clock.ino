/*******************************************************************************
 * P4ETH_NTP_Clock.ino
 *
 * Demonstrates NTP time synchronization over Ethernet with a large clock
 * display on the Waveshare ESP32-P4-ETH board. Combines IP101 Ethernet
 * initialization with LovyanGFX ILI9488 display output.
 *
 * Hardware: Waveshare ESP32-P4-ETH
 *   - Ethernet PHY: IP101
 *     - MDC: GPIO 31, MDIO: GPIO 52, PWR: GPIO 51
 *     - PHY Addr: 1, CLK: EMAC_CLK_EXT_IN
 *   - Display: ILI9488 320x480 SPI
 *     - SPI Host: SPI2_HOST
 *     - SCLK: GPIO 26, MOSI: GPIO 23, MISO: GPIO 27
 *     - DC: GPIO 22, CS: GPIO 20, RST: GPIO 21
 *
 * Required libraries:
 *   - LovyanGFX (install via Library Manager)
 *   - Arduino ESP32 core (3.x) — built-in ETH and time support
 *
 * Features:
 *   - NTP sync with three server pool (pool.ntp.org, Cloudflare, Google)
 *   - Epoch validation (>= 2025-01-01) before displaying time
 *   - Thread-safe time access via localtime_r()
 *   - 1 Hz refresh — display updates once per second
 *   - Shows IP address, sync status, and uptime
 *
 * Author: Educational example for ESP32-P4
 ******************************************************************************/

#include <ETH.h>
#include <time.h>
#include <sys/time.h>
#include <LovyanGFX.hpp>

// ---------------------------------------------------------------------------
// IP101 PHY pin definitions
// ---------------------------------------------------------------------------
#define ETH_PHY_MDC   31
#define ETH_PHY_MDIO  52
#define ETH_PHY_POWER 51
#define ETH_PHY_ADDR  1
#define ETH_PHY_TYPE  ETH_PHY_IP101

// ---------------------------------------------------------------------------
// NTP configuration
// ---------------------------------------------------------------------------
static const char *NTP_SERVER_1 = "pool.ntp.org";
static const char *NTP_SERVER_2 = "time.cloudflare.com";
static const char *NTP_SERVER_3 = "time.google.com";
static const long  GMT_OFFSET   = 0;       // UTC — adjust for your timezone
static const int   DST_OFFSET   = 0;       // Daylight saving offset in seconds

// Epoch threshold: 2025-01-01 00:00:00 UTC = 1735689600
static const time_t NTP_VALID_EPOCH = 1735689600;

// ---------------------------------------------------------------------------
// LovyanGFX display driver
// ---------------------------------------------------------------------------
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9488  _panel;
    lgfx::Bus_SPI        _bus;

public:
    LGFX() {
        auto busCfg        = _bus.config();
        busCfg.spi_host    = SPI2_HOST;
        busCfg.spi_mode    = 0;
        busCfg.freq_write  = 40000000;
        busCfg.freq_read   = 16000000;
        busCfg.pin_sclk    = 26;
        busCfg.pin_mosi    = 23;
        busCfg.pin_miso    = 27;
        busCfg.pin_dc      = 22;
        _bus.config(busCfg);
        _panel.setBus(&_bus);

        auto panelCfg           = _panel.config();
        panelCfg.pin_cs         = 20;
        panelCfg.pin_rst        = 21;
        panelCfg.pin_busy       = -1;
        panelCfg.panel_width    = 320;
        panelCfg.panel_height   = 480;
        panelCfg.offset_x       = 0;
        panelCfg.offset_y       = 0;
        panelCfg.readable       = true;
        panelCfg.invert         = false;
        panelCfg.rgb_order      = false;
        panelCfg.dlen_16bit     = false;
        panelCfg.bus_shared     = false;
        _panel.config(panelCfg);

        setPanel(&_panel);
    }
};

static LGFX display;

// Screen dimensions (landscape)
static const int SCREEN_W = 480;
static const int SCREEN_H = 320;

// ---------------------------------------------------------------------------
// State variables
// ---------------------------------------------------------------------------
static bool     ethConnected  = false;
static bool     ethHasIP      = false;
static bool     ntpSynced     = false;
static String   ipAddress     = "---";
static uint32_t lastDisplaySec = 0xFFFFFFFF;  // Force first update

// ---------------------------------------------------------------------------
// Ethernet event handler
// ---------------------------------------------------------------------------
void onEthEvent(arduino_event_id_t event) {
    switch (event) {
        case ARDUINO_EVENT_ETH_START:
            Serial.println("[ETH] Started");
            ETH.setHostname("esp32-p4-clock");
            break;

        case ARDUINO_EVENT_ETH_CONNECTED:
            Serial.println("[ETH] Link UP");
            ethConnected = true;
            break;

        case ARDUINO_EVENT_ETH_GOT_IP:
            ethHasIP  = true;
            ipAddress = ETH.localIP().toString();
            Serial.printf("[ETH] IP: %s\n", ipAddress.c_str());

            // Start NTP sync once we have an IP
            Serial.println("[NTP] Configuring time servers...");
            configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
            break;

        case ARDUINO_EVENT_ETH_LOST_IP:
            ethHasIP  = false;
            ipAddress = "---";
            break;

        case ARDUINO_EVENT_ETH_DISCONNECTED:
            Serial.println("[ETH] Link DOWN");
            ethConnected = false;
            ethHasIP     = false;
            ipAddress    = "---";
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Check if NTP time is valid (epoch >= 2025-01-01)
// Thread-safe: uses gettimeofday + localtime_r
// ---------------------------------------------------------------------------
bool isTimeValid() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec >= NTP_VALID_EPOCH;
}

// ---------------------------------------------------------------------------
// Format uptime as "Xd Xh Xm Xs"
// ---------------------------------------------------------------------------
String formatUptime(unsigned long ms) {
    unsigned long totalSec = ms / 1000;
    int days  = totalSec / 86400;
    int hours = (totalSec % 86400) / 3600;
    int mins  = (totalSec % 3600)  / 60;
    int secs  = totalSec % 60;

    char buf[32];
    if (days > 0) {
        snprintf(buf, sizeof(buf), "%dd %02dh %02dm %02ds", days, hours, mins, secs);
    } else if (hours > 0) {
        snprintf(buf, sizeof(buf), "%dh %02dm %02ds", hours, mins, secs);
    } else {
        snprintf(buf, sizeof(buf), "%dm %02ds", mins, secs);
    }
    return String(buf);
}

// ---------------------------------------------------------------------------
// Draw the clock face (called once per second)
// ---------------------------------------------------------------------------
void drawClockDisplay() {
    struct timeval tv;
    gettimeofday(&tv, NULL);

    // Only update once per second
    uint32_t currentSec = (uint32_t)(tv.tv_sec);
    if (currentSec == lastDisplaySec && ntpSynced) {
        return;  // No change
    }
    lastDisplaySec = currentSec;

    // --- Header ---
    display.fillRect(0, 0, SCREEN_W, 40, TFT_NAVY);
    display.setTextColor(TFT_WHITE, TFT_NAVY);
    display.setTextSize(2);
    display.setCursor(10, 12);
    display.print("ESP32-P4  NTP Clock");

    // --- Connection indicator ---
    uint16_t statusColor = TFT_RED;
    const char *statusText = "No Link";
    if (ethHasIP && ntpSynced) {
        statusColor = TFT_GREEN;
        statusText  = "Synced";
    } else if (ethHasIP) {
        statusColor = TFT_YELLOW;
        statusText  = "Waiting NTP";
    } else if (ethConnected) {
        statusColor = TFT_ORANGE;
        statusText  = "No IP";
    }

    display.fillCircle(SCREEN_W - 80, 20, 8, statusColor);
    display.setTextSize(1);
    display.setCursor(SCREEN_W - 68, 16);
    display.printf("%-10s", statusText);

    // --- Main clock area ---
    display.fillRect(0, 42, SCREEN_W, 160, TFT_BLACK);

    if (ntpSynced) {
        // Get local time — thread-safe
        struct tm timeinfo;
        time_t nowEpoch = tv.tv_sec;
        localtime_r(&nowEpoch, &timeinfo);

        // Large time display: HH:MM:SS
        char timeBuf[16];
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d",
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

        display.setTextColor(TFT_CYAN, TFT_BLACK);
        display.setTextSize(7);
        // Center the time string (7 * 6px * 8chars = ~336px wide)
        int timeX = (SCREEN_W - 336) / 2;
        display.setCursor(timeX, 60);
        display.print(timeBuf);

        // Date below the time
        char dateBuf[32];
        strftime(dateBuf, sizeof(dateBuf), "%A, %B %d, %Y", &timeinfo);

        display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        display.setTextSize(2);
        int dateW = strlen(dateBuf) * 12;
        display.setCursor((SCREEN_W - dateW) / 2, 135);
        display.print(dateBuf);

        // Timezone info
        display.setTextSize(1);
        display.setTextColor(TFT_DARKGREY, TFT_BLACK);
        display.setCursor((SCREEN_W - 40) / 2, 165);
        display.print("UTC+0");

    } else {
        // Not yet synced
        display.setTextColor(TFT_YELLOW, TFT_BLACK);
        display.setTextSize(3);
        display.setCursor(80, 90);
        display.print("Waiting for NTP...");
    }

    // --- Info panel at bottom ---
    int infoY = 210;
    display.fillRect(0, infoY, SCREEN_W, SCREEN_H - infoY, 0x1082);  // Dark grey

    display.setTextColor(TFT_WHITE, 0x1082);
    display.setTextSize(2);

    // IP Address
    display.setCursor(20, infoY + 15);
    display.printf("IP:     %s", ipAddress.c_str());

    // Link speed
    display.setCursor(20, infoY + 40);
    if (ethHasIP) {
        display.printf("Link:   %d Mbps %s",
                       ETH.linkSpeed(),
                       ETH.fullDuplex() ? "Full" : "Half");
    } else {
        display.print("Link:   ---");
    }

    // Uptime
    display.setCursor(20, infoY + 65);
    display.printf("Uptime: %s", formatUptime(millis()).c_str());

    // NTP servers
    display.setTextSize(1);
    display.setTextColor(TFT_DARKGREY, 0x1082);
    display.setCursor(20, infoY + 92);
    display.printf("NTP: %s | %s | %s", NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
}

// ---------------------------------------------------------------------------
// setup()
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println();
    Serial.println("P4ETH NTP Clock — starting...");

    // Initialize display
    display.init();
    display.setRotation(1);  // Landscape 480x320
    display.fillScreen(TFT_BLACK);
    display.setTextWrap(false);

    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextSize(2);
    display.setCursor(100, 140);
    display.print("Initializing Ethernet...");

    // Register event handler and start Ethernet
    Network.onEvent(onEthEvent);

    bool ok = ETH.begin(
        ETH_PHY_TYPE,
        ETH_PHY_ADDR,
        ETH_PHY_MDC,
        ETH_PHY_MDIO,
        ETH_PHY_POWER,
        ETH_CLK_EXT_IN
    );

    if (!ok) {
        Serial.println("[ETH] ERROR: ETH.begin() failed!");
        display.fillScreen(TFT_BLACK);
        display.setTextColor(TFT_RED, TFT_BLACK);
        display.setTextSize(2);
        display.setCursor(60, 140);
        display.print("Ethernet init failed!");
        while (1) delay(1000);
    }

    Serial.println("[ETH] Waiting for link and IP...");
}

// ---------------------------------------------------------------------------
// loop() — 1 Hz refresh
// ---------------------------------------------------------------------------
void loop() {
    // Check NTP sync status
    if (!ntpSynced && isTimeValid()) {
        ntpSynced = true;
        Serial.println("[NTP] Time synchronized!");

        struct tm timeinfo;
        time_t now = time(NULL);
        localtime_r(&now, &timeinfo);

        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", &timeinfo);
        Serial.printf("[NTP] Current time: %s\n", buf);
    }

    drawClockDisplay();

    delay(100);  // Check ~10 times per second, but display only updates 1/sec
}
