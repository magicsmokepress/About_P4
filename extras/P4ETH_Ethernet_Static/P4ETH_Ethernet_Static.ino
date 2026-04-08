/*******************************************************************************
 * P4ETH_Ethernet_Static.ino
 *
 * Demonstrates Ethernet initialization on the Waveshare ESP32-P4-ETH board
 * using the IP101 PHY. Supports both static IP configuration and DHCP,
 * with runtime toggling via Serial commands.
 *
 * Hardware: Waveshare ESP32-P4-ETH
 *   - Ethernet PHY: IP101
 *   - MDC:  GPIO 31
 *   - MDIO: GPIO 52
 *   - PWR:  GPIO 51
 *   - CLK:  External input (EMAC_CLK_EXT_IN)
 *   - PHY Address: 1
 *
 * Required libraries:
 *   - Arduino ESP32 core (3.x) — built-in ETH support
 *
 * Serial commands:
 *   "static" — switch to static IP mode
 *   "dhcp"   — switch to DHCP mode
 *   "status" — print current network info
 *
 * Author: Educational example for ESP32-P4
 ******************************************************************************/

#include <ETH.h>

// ---------------------------------------------------------------------------
// IP101 PHY pin definitions for Waveshare ESP32-P4-ETH
// ---------------------------------------------------------------------------
#define ETH_PHY_MDC   31
#define ETH_PHY_MDIO  52
#define ETH_PHY_POWER 51
#define ETH_PHY_ADDR  1
#define ETH_PHY_TYPE  ETH_PHY_IP101

// ---------------------------------------------------------------------------
// Static IP defaults — change these to match your network
// ---------------------------------------------------------------------------
static IPAddress staticIP(192, 168, 1, 200);
static IPAddress gateway(192, 168, 1, 1);
static IPAddress subnet(255, 255, 255, 0);
static IPAddress dns1(8, 8, 8, 8);
static IPAddress dns2(8, 8, 4, 4);

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static bool useStaticIP   = false;   // Start with DHCP by default
static bool ethConnected  = false;
static bool ethHasIP      = false;

// ---------------------------------------------------------------------------
// Apply network configuration (static or DHCP)
// ---------------------------------------------------------------------------
void applyNetworkConfig() {
    if (useStaticIP) {
        Serial.println("[ETH] Applying static IP configuration...");
        Serial.printf("      IP:      %s\n", staticIP.toString().c_str());
        Serial.printf("      Gateway: %s\n", gateway.toString().c_str());
        Serial.printf("      Subnet:  %s\n", subnet.toString().c_str());
        Serial.printf("      DNS1:    %s\n", dns1.toString().c_str());
        Serial.printf("      DNS2:    %s\n", dns2.toString().c_str());

        if (!ETH.config(staticIP, gateway, subnet, dns1, dns2)) {
            Serial.println("[ETH] Static IP config failed — falling back to DHCP");
            useStaticIP = false;
            ETH.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
        }
    } else {
        Serial.println("[ETH] Applying DHCP configuration...");
        // Passing all-zeros triggers DHCP
        ETH.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
    }
}

// ---------------------------------------------------------------------------
// Print current network status
// ---------------------------------------------------------------------------
void printNetworkStatus() {
    Serial.println("========== Network Status ==========");
    Serial.printf("  Connected: %s\n", ethConnected ? "YES" : "NO");
    Serial.printf("  Has IP:    %s\n", ethHasIP ? "YES" : "NO");
    Serial.printf("  Mode:      %s\n", useStaticIP ? "STATIC" : "DHCP");

    if (ethHasIP) {
        Serial.printf("  IP:        %s\n", ETH.localIP().toString().c_str());
        Serial.printf("  Gateway:   %s\n", ETH.gatewayIP().toString().c_str());
        Serial.printf("  Subnet:    %s\n", ETH.subnetMask().toString().c_str());
        Serial.printf("  DNS:       %s\n", ETH.dnsIP().toString().c_str());
        Serial.printf("  MAC:       %s\n", ETH.macAddress().c_str());
        Serial.printf("  Speed:     %d Mbps\n", ETH.linkSpeed());
        Serial.printf("  Full Dup:  %s\n", ETH.fullDuplex() ? "YES" : "NO");
    }
    Serial.println("====================================");
}

// ---------------------------------------------------------------------------
// WiFi/Ethernet event handler
// ---------------------------------------------------------------------------
void onEthEvent(arduino_event_id_t event) {
    switch (event) {
        case ARDUINO_EVENT_ETH_START:
            Serial.println("[ETH] Started — PHY initialized");
            // Optionally set hostname before DHCP request
            ETH.setHostname("esp32-p4-eth");
            break;

        case ARDUINO_EVENT_ETH_CONNECTED:
            Serial.println("[ETH] Link UP");
            ethConnected = true;
            break;

        case ARDUINO_EVENT_ETH_GOT_IP:
            ethHasIP = true;
            Serial.println("[ETH] Got IP address");
            printNetworkStatus();
            break;

        case ARDUINO_EVENT_ETH_LOST_IP:
            Serial.println("[ETH] Lost IP address");
            ethHasIP = false;
            break;

        case ARDUINO_EVENT_ETH_DISCONNECTED:
            Serial.println("[ETH] Link DOWN");
            ethConnected = false;
            ethHasIP     = false;
            break;

        case ARDUINO_EVENT_ETH_STOP:
            Serial.println("[ETH] Stopped");
            ethConnected = false;
            ethHasIP     = false;
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Process Serial commands
// ---------------------------------------------------------------------------
void processSerialCommand(const String &cmd) {
    String trimmed = cmd;
    trimmed.trim();
    trimmed.toLowerCase();

    if (trimmed == "static") {
        Serial.println("\n>> Switching to STATIC IP mode...");
        useStaticIP = true;
        applyNetworkConfig();
    } else if (trimmed == "dhcp") {
        Serial.println("\n>> Switching to DHCP mode...");
        useStaticIP = false;
        applyNetworkConfig();
    } else if (trimmed == "status") {
        printNetworkStatus();
    } else {
        Serial.println("Unknown command. Available: static, dhcp, status");
    }
}

// ---------------------------------------------------------------------------
// setup()
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("=============================================");
    Serial.println("  ESP32-P4-ETH  Ethernet Static/DHCP Demo");
    Serial.println("=============================================");
    Serial.println();
    Serial.println("Commands: 'static', 'dhcp', 'status'");
    Serial.println();

    // Register the event handler BEFORE starting Ethernet
    Network.onEvent(onEthEvent);

    // Initialize Ethernet with IP101 PHY
    // Parameters: PHY_ADDR, MDC, MDIO, PHY_TYPE, CLK_MODE, PHY_POWER
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
        Serial.println("      Check PHY wiring and pin definitions.");
        return;
    }

    Serial.println("[ETH] ETH.begin() succeeded — waiting for link...");

    // Apply initial network config (DHCP by default)
    applyNetworkConfig();
}

// ---------------------------------------------------------------------------
// loop()
// ---------------------------------------------------------------------------
void loop() {
    // Read Serial commands
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        processSerialCommand(cmd);
    }

    delay(10);
}
