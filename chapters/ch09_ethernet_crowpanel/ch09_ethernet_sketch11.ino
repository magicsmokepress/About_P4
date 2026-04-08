// ─── State flags (set by event handler) ─────────────────────
static bool linkUp = false;
static bool hasIP  = false;
static char currentIP[20] = "0.0.0.0";

// ─── Ethernet event handler ─────────────────────────────────
void onEthEvent(arduino_event_id_t event, arduino_event_info_t info) {
    switch (event) {
        case ARDUINO_EVENT_ETH_CONNECTED:
            linkUp = true;
            Serial.println("[ETH] Link UP");
            break;
        case ARDUINO_EVENT_ETH_DISCONNECTED:
            linkUp = false;
            hasIP  = false;
            strncpy(currentIP, "0.0.0.0", sizeof(currentIP));
            Serial.println("[ETH] Link DOWN");
            break;
        case ARDUINO_EVENT_ETH_GOT_IP:
            hasIP = true;
            strncpy(currentIP, ETH.localIP().toString().c_str(),
                    sizeof(currentIP) - 1);
            Serial.printf("[ETH] Got IP: %s\n", currentIP);
            // Good place to kick off NTP sync:
            configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
            break;
        default: break;
    }
}

// ─── Network configuration (DHCP or static) ─────────────────
void applyNetworkConfig() {
    if (useDHCP) {
        ETH.begin();   // DHCP handled automatically by lwIP
    } else {
        IPAddress ip, gw, sub, dns1, dns2;
        ip.fromString("10.0.10.50");
        gw.fromString("10.0.10.1");
        sub.fromString("255.255.255.0");
        dns1.fromString("10.0.10.1");
        dns2.fromString("8.8.8.8");
        ETH.config(ip, gw, sub, dns1, dns2);
        ETH.begin();
    }
}

void setup() {
    Serial.begin(115200);
    WiFi.onEvent(onEthEvent);  // Register handler before ETH.begin()
    applyNetworkConfig();
    // ... rest of setup ...
}
