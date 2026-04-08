#include <Wire.h>

#define I2C1_SDA  45
#define I2C1_SCL  46

// Known device table for friendly names
struct KnownDevice {
    uint8_t addr;
    const char* name;
};

static const KnownDevice known[] = {
    { 0x14, "GT911 touch (alt addr)" },
    { 0x23, "BH1750 light sensor" },
    { 0x38, "AHT10/AHT20 temp/humidity" },
    { 0x3C, "SSD1306 OLED 128x64" },
    { 0x3D, "SSD1306 OLED (alt addr)" },
    { 0x50, "AT24C EEPROM" },
    { 0x57, "DS3231 RTC EEPROM" },
    { 0x5D, "GT911 touch controller" },
    { 0x68, "MPU6050 / DS3231 RTC" },
    { 0x69, "MPU6050 (AD0=HIGH)" },
    { 0x76, "BME280 / BMP280 (SDO=GND)" },
    { 0x77, "BME280 / BMP280 (SDO=VCC)" },
};
static const int knownCount = sizeof(known) / sizeof(known[0]);

static const char* lookupName(uint8_t addr) {
    for (int i = 0; i < knownCount; i++) {
        if (known[i].addr == addr) return known[i].name;
    }
    return NULL;
}

void setup() {
    Serial.begin(115200);
    delay(2000);  // let serial monitor connect

    Serial.println();
    Serial.println("========================================");
    Serial.println("  CrowPanel P4 - I2C Bus Scanner");
    Serial.println("  Bus: I2C1 header (SDA=45, SCL=46)");
    Serial.println("========================================");
    Serial.println();

    Wire.begin(I2C1_SDA, I2C1_SCL, 100000);  // 100 kHz
    delay(100);

    Serial.println("Scanning addresses 0x01 - 0x7F ...");
    Serial.println();

    int found = 0;

    for (uint8_t addr = 1; addr < 128; addr++) {
        Wire.beginTransmission(addr);
        uint8_t err = Wire.endTransmission();

        if (err == 0) {
            found++;
            const char* name = lookupName(addr);
            Serial.printf("  0x%02X  - ACK  ", addr);
            if (name) {
                Serial.printf(" [%s]", name);
            }
            Serial.println();
        } else if (err == 4) {
            Serial.printf("  0x%02X  - ERROR (unknown)\n", addr);
        }
        // err == 2 (NACK on address) = no device, skip silently
    }

    Serial.println();
    if (found == 0) {
        Serial.println("*** No devices found! ***");
        Serial.println();
        Serial.println("Troubleshooting:");
        Serial.println("  - Check SDA is connected to SDA on I2C1 header");
        Serial.println("  - Check SCL is connected to SCL on I2C1 header");
        Serial.println("  - Check VCC is 3.3V (not 5V)");
        Serial.println("  - Check GND is connected");
        Serial.println("  - Some sensors need time to boot - press RESET");
    } else {
        Serial.printf("Found %d device(s) on the bus.\n", found);
    }

    Serial.println();
    Serial.println("Scan complete. Press RESET to scan again.");
}

void loop() {
    delay(10000);
}
