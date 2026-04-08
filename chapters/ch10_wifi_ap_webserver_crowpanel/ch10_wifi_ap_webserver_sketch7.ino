#include <Preferences.h>
void setup() {
    Preferences prefs;
    prefs.begin("Wi-Fi", false);
    prefs.clear();
    prefs.end();
    Serial.println("Credentials cleared");
}
void loop() {}
