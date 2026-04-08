#include "driver/i2c.h"

#define BH1750_ADDR      0x23    // ADDR pin LOW or floating
#define BH1750_POWER_ON  0x01
#define BH1750_RESET     0x07
#define BH1750_CONT_HRES 0x10    // Continuous high-res (1 lx, 120ms)

static i2c_port_t bh_port = I2C_NUM_0;
static uint8_t bh_addr = BH1750_ADDR;

static bool bh1750_cmd(uint8_t cmd) {
    esp_err_t err = i2c_master_write_to_device(
        bh_port, bh_addr, &cmd, 1, pdMS_TO_TICKS(100));
    return (err == ESP_OK);
}

static bool bh1750_init() {
    // Try primary address first (0x23)
    if (bh1750_cmd(BH1750_POWER_ON)) {
        Serial.printf("[BH1750] Found at 0x%02X\n", bh_addr);
        bh1750_cmd(BH1750_RESET);
        bh1750_cmd(BH1750_CONT_HRES);
        return true;
    }

    // Try alternate address (0x5C) - ADDR pin HIGH
    bh_addr = 0x5C;
    if (bh1750_cmd(BH1750_POWER_ON)) {
        Serial.printf("[BH1750] Found at 0x%02X (alt)\n", bh_addr);
        bh1750_cmd(BH1750_RESET);
        bh1750_cmd(BH1750_CONT_HRES);
        return true;
    }

    return false;
}

static float bh1750_read_lux() {
    uint8_t buf[2] = {0};
    esp_err_t err = i2c_master_read_from_device(
        bh_port, bh_addr, buf, 2, pdMS_TO_TICKS(200));
    if (err != ESP_OK) return -1;

    uint16_t raw = (buf[0] << 8) | buf[1];
    return raw / 1.2;  // Convert to lux per datasheet
}
