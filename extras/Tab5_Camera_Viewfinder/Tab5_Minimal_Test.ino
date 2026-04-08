/**
 * Tab5 Minimal Boot Test
 *
 * Does the Tab5 even boot with this Arduino core config?
 * If this brownouts too, the issue is the board/cable/battery,
 * not our camera code.
 */

void setup() {
    Serial.begin(115200);
    delay(5000);  // Long delay — let everything settle

    Serial.println("\n=== Tab5 Boot Test ===");
    Serial.printf("CPU: %d MHz\n", getCpuFrequencyMhz());
    Serial.printf("PSRAM: %u bytes\n", ESP.getPsramSize());
    Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("SDK: %s\n", ESP.getSdkVersion());
    Serial.println("If you see this, the Tab5 boots fine.");
    Serial.println("The brownout is caused by the camera init.");
}

void loop() {
    Serial.println("alive");
    delay(5000);
}
