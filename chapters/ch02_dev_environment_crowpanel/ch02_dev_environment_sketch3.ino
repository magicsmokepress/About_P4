void setup() {
    Serial.begin(115200);
    delay(2000);  // Give USB CDC time to initialize

    Serial.println("=== ESP32-P4 Toolchain Test ===");
    Serial.printf("CPU Frequency: %d MHz\n", getCpuFrequencyMhz());
    Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
    Serial.printf("PSRAM size: %d bytes\n", ESP.getPsramSize());
    Serial.printf("Flash size: %d bytes\n", ESP.getFlashChipSize());
    Serial.printf("SDK version: %s\n", ESP.getSdkVersion());
}

void loop() {
    delay(1000);
}
