#include <SD_MMC.h>
#include <FS.h>

#define SD_CLK  43
#define SD_CMD  44
#define SD_D0   39

static bool init_sd() {
    SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);

    if (!SD_MMC.begin("/sdcard", true)) {  // true = 1-bit mode
        Serial.println("SD card mount failed");
        return false;
    }

    uint8_t type = SD_MMC.cardType();
    if (type == CARD_NONE) {
        Serial.println("No SD card inserted");
        return false;
    }

    const char *typeStr;
    switch (type) {
        case CARD_MMC:  typeStr = "MMC"; break;
        case CARD_SD:   typeStr = "SD"; break;
        case CARD_SDHC: typeStr = "SDHC"; break;
        default:        typeStr = "Unknown"; break;
    }

    Serial.printf("SD card: %s, %llu MB total, %llu MB used\n",
        typeStr,
        SD_MMC.totalBytes() / (1024 * 1024),
        SD_MMC.usedBytes() / (1024 * 1024));

    return true;
}
