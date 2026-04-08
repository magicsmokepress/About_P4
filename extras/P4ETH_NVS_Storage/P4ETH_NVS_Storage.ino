/*******************************************************************************
 * P4ETH_NVS_Storage.ino
 *
 * NVS Persistent Storage with CRC32 Integrity Checking
 *
 * Demonstrates:
 *   - Storing and retrieving structured data in NVS (Non-Volatile Storage)
 *   - Software CRC32 implementation (IEEE 802.3 polynomial)
 *   - Data integrity validation on read-back
 *   - Detecting corruption via CRC mismatch
 *   - Using the Preferences library for typed NVS access
 *
 * Why software CRC32?
 *   The ESP32-P4 uses a RISC-V core (HP & LP), unlike the Xtensa-based
 *   ESP32/S2/S3 which provide a ROM function crc32_le() backed by hardware.
 *   On RISC-V targets, that ROM function does not exist, so we implement
 *   a portable bit-by-bit CRC32 using the standard IEEE 802.3 polynomial
 *   (0xEDB88320 reflected). This runs entirely in software and works on
 *   every ESP32 variant without architecture dependencies.
 *
 * Hardware: Waveshare ESP32-P4-ETH (or any ESP32-P4 board)
 *   - No display or network needed; uses Serial only
 *
 * Required Libraries:
 *   - Preferences (built-in ESP32 Arduino core)
 *
 * Board: Waveshare ESP32-P4-ETH (ESP32-P4, arduino-esp32 core 3.x)
 ******************************************************************************/

#include <Preferences.h>

// ---------------------------------------------------------------------------
// Software CRC32 (IEEE 802.3 polynomial, reflected / LSB-first)
//
// This is the same algorithm used in Ethernet FCS, PNG, ZIP, etc.
// We need a software implementation because the ESP32-P4 RISC-V core
// does not include the ROM-based crc32_le() found on Xtensa ESP32 chips.
// ---------------------------------------------------------------------------
static uint32_t sw_crc32(uint32_t crc, const uint8_t* data, size_t len) {
  crc = ~crc;
  while (len--) {
    crc ^= *data++;
    for (int i = 0; i < 8; i++) {
      crc = (crc >> 1) ^ (0xEDB88320UL & -(crc & 1));
    }
  }
  return ~crc;
}

// ---------------------------------------------------------------------------
// Configuration structure stored in NVS
// ---------------------------------------------------------------------------
struct SensorConfig {
  float    tempThresholdHigh;    // degrees C
  float    tempThresholdLow;     // degrees C
  float    humidityCalOffset;    // calibration offset for humidity sensor
  float    pressureCalScale;     // calibration scale factor
  uint32_t sampleIntervalMs;     // how often to read sensors
  uint32_t bootCount;            // number of times config was loaded after boot
  uint8_t  sensorEnabled;        // bitmask: bit0=temp, bit1=humidity, bit2=pressure
  char     deviceName[32];       // user-assigned device name
};

// Active in-memory configuration
static SensorConfig config;
static Preferences  prefs;

static const char* NVS_NAMESPACE  = "sensor_cfg";
static const char* KEY_DATA       = "cfg_data";
static const char* KEY_CRC        = "cfg_crc";

// ---------------------------------------------------------------------------
// Set factory defaults
// ---------------------------------------------------------------------------
static void setDefaults(SensorConfig& cfg) {
  cfg.tempThresholdHigh  = 45.0f;
  cfg.tempThresholdLow   = -10.0f;
  cfg.humidityCalOffset  = 0.0f;
  cfg.pressureCalScale   = 1.0f;
  cfg.sampleIntervalMs   = 1000;
  cfg.bootCount          = 0;
  cfg.sensorEnabled      = 0x07;  // all three sensors enabled
  strncpy(cfg.deviceName, "ESP32-P4-Sensor", sizeof(cfg.deviceName));
  cfg.deviceName[sizeof(cfg.deviceName) - 1] = '\0';
}

// ---------------------------------------------------------------------------
// Compute CRC32 over the config struct
// ---------------------------------------------------------------------------
static uint32_t configCRC(const SensorConfig& cfg) {
  return sw_crc32(0, reinterpret_cast<const uint8_t*>(&cfg), sizeof(cfg));
}

// ---------------------------------------------------------------------------
// Save config to NVS with CRC
// ---------------------------------------------------------------------------
static bool saveConfig() {
  uint32_t crc = configCRC(config);

  prefs.begin(NVS_NAMESPACE, false);  // read-write

  size_t written = prefs.putBytes(KEY_DATA, &config, sizeof(config));
  if (written != sizeof(config)) {
    Serial.println("ERROR: Failed to write config data to NVS");
    prefs.end();
    return false;
  }

  prefs.putUInt(KEY_CRC, crc);
  prefs.end();

  Serial.printf("Config saved to NVS (%u bytes, CRC32=0x%08X)\n",
                (unsigned)sizeof(config), crc);
  return true;
}

// ---------------------------------------------------------------------------
// Load config from NVS with CRC validation
// ---------------------------------------------------------------------------
static bool loadConfig() {
  prefs.begin(NVS_NAMESPACE, true);  // read-only

  size_t readLen = prefs.getBytesLength(KEY_DATA);
  if (readLen == 0) {
    Serial.println("No config found in NVS (first boot?)");
    prefs.end();
    return false;
  }

  if (readLen != sizeof(SensorConfig)) {
    Serial.printf("WARNING: Stored size (%u) != expected (%u) — struct may have changed\n",
                  (unsigned)readLen, (unsigned)sizeof(SensorConfig));
    prefs.end();
    return false;
  }

  SensorConfig loaded;
  prefs.getBytes(KEY_DATA, &loaded, sizeof(loaded));
  uint32_t storedCRC = prefs.getUInt(KEY_CRC, 0);
  prefs.end();

  // Validate CRC
  uint32_t computedCRC = configCRC(loaded);
  if (computedCRC != storedCRC) {
    Serial.println("!!! CRC MISMATCH — stored data is CORRUPT !!!");
    Serial.printf("    Stored  CRC32: 0x%08X\n", storedCRC);
    Serial.printf("    Computed CRC32: 0x%08X\n", computedCRC);
    Serial.println("    Config NOT loaded. Use (R)eset to restore defaults.");
    return false;
  }

  config = loaded;
  config.bootCount++;
  Serial.printf("Config loaded from NVS (CRC32=0x%08X, boot #%u)\n",
                storedCRC, config.bootCount);
  return true;
}

// ---------------------------------------------------------------------------
// Deliberately corrupt stored data (for demonstration)
// ---------------------------------------------------------------------------
static void corruptStoredData() {
  prefs.begin(NVS_NAMESPACE, false);
  size_t len = prefs.getBytesLength(KEY_DATA);
  if (len == 0) {
    Serial.println("No data to corrupt.");
    prefs.end();
    return;
  }

  // Read the raw bytes, flip a bit, write back (without updating CRC)
  uint8_t buf[sizeof(SensorConfig)];
  prefs.getBytes(KEY_DATA, buf, sizeof(buf));
  buf[0] ^= 0xFF;  // flip all bits in the first byte
  prefs.putBytes(KEY_DATA, buf, sizeof(buf));
  prefs.end();

  Serial.println("Stored config data has been deliberately CORRUPTED.");
  Serial.println("Try (L)oad to see CRC mismatch detection in action.");
}

// ---------------------------------------------------------------------------
// Print current config values
// ---------------------------------------------------------------------------
static void printConfig() {
  Serial.println("\n--- Current Configuration ---");
  Serial.printf("  Device Name       : %s\n", config.deviceName);
  Serial.printf("  Temp High Thresh  : %.1f C\n", config.tempThresholdHigh);
  Serial.printf("  Temp Low Thresh   : %.1f C\n", config.tempThresholdLow);
  Serial.printf("  Humidity Cal Ofs  : %.2f\n", config.humidityCalOffset);
  Serial.printf("  Pressure Cal Scl  : %.4f\n", config.pressureCalScale);
  Serial.printf("  Sample Interval   : %u ms\n", config.sampleIntervalMs);
  Serial.printf("  Boot Count        : %u\n", config.bootCount);
  Serial.printf("  Sensors Enabled   : 0x%02X", config.sensorEnabled);
  if (config.sensorEnabled & 0x01) Serial.print(" [Temp]");
  if (config.sensorEnabled & 0x02) Serial.print(" [Humidity]");
  if (config.sensorEnabled & 0x04) Serial.print(" [Pressure]");
  Serial.println();
  Serial.printf("  Struct size       : %u bytes\n", (unsigned)sizeof(SensorConfig));
  Serial.printf("  CRC32             : 0x%08X\n", configCRC(config));
  Serial.println("-----------------------------\n");
}

// ---------------------------------------------------------------------------
// Print menu
// ---------------------------------------------------------------------------
static void printMenu() {
  Serial.println("=== NVS Storage Demo ===");
  Serial.println("  (S) Save config to NVS");
  Serial.println("  (L) Load config from NVS");
  Serial.println("  (R) Reset to factory defaults");
  Serial.println("  (P) Print current config");
  Serial.println("  (C) Corrupt stored data (demo CRC check)");
  Serial.println("  (M) Modify a value (interactive)");
  Serial.println("========================\n");
}

// ---------------------------------------------------------------------------
// Simple interactive value modification
// ---------------------------------------------------------------------------
static void modifyValue() {
  Serial.println("Which value to modify?");
  Serial.println("  1) Temp High Threshold");
  Serial.println("  2) Temp Low Threshold");
  Serial.println("  3) Sample Interval (ms)");
  Serial.println("  4) Device Name");

  // Wait for input
  while (!Serial.available()) delay(10);
  char choice = Serial.read();
  // Flush remaining
  while (Serial.available()) Serial.read();

  Serial.print("Enter new value: ");
  while (!Serial.available()) delay(10);
  String val = Serial.readStringUntil('\n');
  val.trim();

  switch (choice) {
    case '1':
      config.tempThresholdHigh = val.toFloat();
      Serial.printf("Temp High Threshold set to %.1f\n", config.tempThresholdHigh);
      break;
    case '2':
      config.tempThresholdLow = val.toFloat();
      Serial.printf("Temp Low Threshold set to %.1f\n", config.tempThresholdLow);
      break;
    case '3':
      config.sampleIntervalMs = val.toInt();
      Serial.printf("Sample Interval set to %u ms\n", config.sampleIntervalMs);
      break;
    case '4':
      strncpy(config.deviceName, val.c_str(), sizeof(config.deviceName) - 1);
      config.deviceName[sizeof(config.deviceName) - 1] = '\0';
      Serial.printf("Device Name set to '%s'\n", config.deviceName);
      break;
    default:
      Serial.println("Invalid choice.");
      break;
  }
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n=============================================");
  Serial.println("  ESP32-P4 NVS Storage with CRC32 Integrity");
  Serial.println("=============================================\n");

  // Start with factory defaults
  setDefaults(config);

  // Attempt to load saved config
  if (loadConfig()) {
    Serial.println("Successfully restored saved configuration.");
  } else {
    Serial.println("Using factory defaults.");
  }

  printConfig();
  printMenu();
}

// ---------------------------------------------------------------------------
// Main loop — process serial commands
// ---------------------------------------------------------------------------
void loop() {
  if (Serial.available()) {
    char cmd = toupper(Serial.read());
    // Flush any trailing newline/CR
    while (Serial.available()) {
      char c = Serial.peek();
      if (c == '\n' || c == '\r') Serial.read();
      else break;
    }

    switch (cmd) {
      case 'S':
        saveConfig();
        break;
      case 'L':
        if (!loadConfig()) {
          Serial.println("Load failed. Current in-memory config unchanged.");
        } else {
          printConfig();
        }
        break;
      case 'R':
        setDefaults(config);
        Serial.println("Config reset to factory defaults.");
        printConfig();
        break;
      case 'P':
        printConfig();
        break;
      case 'C':
        corruptStoredData();
        break;
      case 'M':
        modifyValue();
        break;
      default:
        if (cmd >= ' ') printMenu();  // ignore control chars
        break;
    }
  }
  delay(10);
}
