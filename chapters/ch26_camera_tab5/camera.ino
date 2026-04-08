#include <M5Unified.h>

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(1);  // landscape
    M5.Display.fillScreen(TFT_BLUE);
    M5.Display.setTextSize(4);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setCursor(100, 300);
    M5.Display.println("Tab5 ready!");
}

void loop() {
    M5.update();
}
