// board_config.h - Elecrow CrowPanel Advanced 7"
#define DISPLAY_WIDTH      1024
#define DISPLAY_HEIGHT     600
#define BACKLIGHT_PIN      31
#define I2C_SDA            45
#define I2C_SCL            46
#define TOUCH_RST          40
#define TOUCH_INT          42
#define LED_PIN            48
#define SD_CLK             43
#define SD_CMD             44
#define SD_D0              39
// ... more pin definitions
// Your sketch
#include "board_config.h"

void setup() {
    pinMode(LED_PIN, OUTPUT);       // Works on any board
    digitalWrite(LED_PIN, HIGH);
}
