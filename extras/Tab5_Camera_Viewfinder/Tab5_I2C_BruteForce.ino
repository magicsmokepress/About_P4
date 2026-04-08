/**
 * Tab5 — Brute-force I2C scanner
 *
 * Scans every plausible I2C pin combination to find the SC2336 camera.
 * Also checks if 0x10 on internal bus might be the camera at an
 * alternate address (some sensors use 0x10 when SCCB address pin
 * is configured differently).
 *
 * Board: M5Stack Tab5 (M5Stack board manager)
 */

#include <Wire.h>

// All GPIO pins available on the Tab5 that might carry I2C
// (excluding known internal I2C 31/32 and reserved pins)
static const int candidate_pins[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    16, 17, 18, 19, 20,
    45, 46, 47, 48, 49, 50, 51, 52, 53, 54
};
static const int num_pins = sizeof(candidate_pins) / sizeof(int);

// Known camera sensor I2C addresses
static const uint8_t cam_addrs[] = {
    0x10, 0x20, 0x21, 0x30, 0x36, 0x37, 0x3C, 0x48
};
static const int num_addrs = sizeof(cam_addrs) / sizeof(uint8_t);

void setup() {
    Serial.begin(115200);
    delay(3000);

    Serial.println("\n========================================");
    Serial.println("  Tab5 I2C Brute-Force Scanner");
    Serial.println("========================================\n");

    // ── Step 1: Check internal bus devices in detail ──
    Serial.println("=== STEP 1: Internal bus (SDA=31, SCL=32) ===\n");
    Wire.begin(31, 32, 400000);
    delay(50);

    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  0x%02X ACK", addr);

            // Try reading chip ID for known camera addresses
            if (addr == 0x10 || addr == 0x30 || addr == 0x36) {
                // SC2336 ID regs: 0x3107/0x3108
                Wire.beginTransmission(addr);
                Wire.write(0x31);
                Wire.write(0x07);
                Wire.endTransmission(false);
                Wire.requestFrom(addr, (uint8_t)2);
                if (Wire.available() >= 2) {
                    uint8_t h = Wire.read(), l = Wire.read();
                    Serial.printf("  chipID(31/07)=0x%02X%02X", h, l);
                }

                // OV5647 ID regs: 0x300A/0x300B
                Wire.beginTransmission(addr);
                Wire.write(0x30);
                Wire.write(0x0A);
                Wire.endTransmission(false);
                Wire.requestFrom(addr, (uint8_t)2);
                if (Wire.available() >= 2) {
                    uint8_t h = Wire.read(), l = Wire.read();
                    Serial.printf("  chipID(30/0A)=0x%02X%02X", h, l);
                }
            }

            if (addr == 0x43 || addr == 0x44) {
                Serial.print("  (IO Expander)");
            }
            if (addr == 0x68) {
                // Could be BMI270 — read chip ID at 0x00
                Wire.beginTransmission(addr);
                Wire.write(0x00);
                Wire.endTransmission(false);
                Wire.requestFrom(addr, (uint8_t)1);
                if (Wire.available()) {
                    uint8_t id = Wire.read();
                    Serial.printf("  chipID(00)=0x%02X", id);
                    if (id == 0x24) Serial.print(" (BMI270)");
                    if (id == 0x68) Serial.print(" (MPU6050)");
                }
            }
            Serial.println();
        }
    }
    Wire.end();

    // ── Step 2: Scan camera-specific pins ──
    Serial.println("\n=== STEP 2: Camera I2C pin scan ===");
    Serial.println("Testing all pin pairs for camera addresses...\n");

    // Focus on likely camera pin pairs first
    int priority_pairs[][2] = {
        {7, 8},    // Espressif reference
        {6, 7},    // alternate
        {8, 9},    // alternate
        {47, 48},  // M-Bus
        {53, 54},  // External I2C from M5Unified
        {19, 20},  // USB/BSP pins
        {2, 3},    // Low GPIOs
        {4, 5},    // Low GPIOs
        {0, 1},    // Low GPIOs
        {16, 17},  // Mid GPIOs
        {45, 46},  // CrowPanel I2C
        {49, 50},  // ExtPort
        {51, 52},  // M-Bus
    };
    int num_pairs = sizeof(priority_pairs) / sizeof(priority_pairs[0]);

    for (int p = 0; p < num_pairs; p++) {
        int sda = priority_pairs[p][0];
        int scl = priority_pairs[p][1];

        Wire.end();
        delay(20);
        Wire.begin(sda, scl, 100000);
        delay(50);

        bool found_anything = false;
        for (int a = 0; a < num_addrs; a++) {
            Wire.beginTransmission(cam_addrs[a]);
            if (Wire.endTransmission() == 0) {
                Serial.printf("  *** HIT: SDA=%d SCL=%d addr=0x%02X",
                              sda, scl, cam_addrs[a]);

                // Read chip ID
                Wire.beginTransmission(cam_addrs[a]);
                Wire.write(0x31);
                Wire.write(0x07);
                Wire.endTransmission(false);
                Wire.requestFrom(cam_addrs[a], (uint8_t)2);
                if (Wire.available() >= 2) {
                    uint8_t h = Wire.read(), l = Wire.read();
                    Serial.printf("  ID=0x%02X%02X", h, l);
                    if (h == 0xCB && l == 0x3A)
                        Serial.print(" SC2336!");
                }
                Serial.println();
                found_anything = true;
            }
        }

        // Also do a full scan on this pin pair
        if (!found_anything) {
            int count = 0;
            for (uint8_t addr = 1; addr < 127; addr++) {
                Wire.beginTransmission(addr);
                if (Wire.endTransmission() == 0) {
                    if (count == 0)
                        Serial.printf("  SDA=%d SCL=%d:", sda, scl);
                    Serial.printf(" 0x%02X", addr);
                    count++;
                }
            }
            if (count > 0) Serial.println();
        }
    }
    Wire.end();

    // ── Step 3: Check if camera needs MCLK ──
    Serial.println("\n=== STEP 3: Info ===");
    Serial.println("If no camera found, the SC2336 likely needs");
    Serial.println("an MCLK signal before it powers up its I2C.");
    Serial.println("The Tab5 BSP calls bsp_cam_osc_init() first,");
    Serial.println("which may configure a clock output on a GPIO.");
    Serial.println("Check M5Stack BSP for XCLK/MCLK pin.");
    Serial.printf("\nFree heap: %u\n", ESP.getFreeHeap());
    Serial.printf("Free PSRAM: %u\n", ESP.getFreePsram());
    Serial.println("\nDone. Paste this output back.");
}

void loop() {
    delay(10000);
}
