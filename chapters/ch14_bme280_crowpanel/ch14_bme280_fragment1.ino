#include "driver/i2c.h"  // legacy API

static i2c_port_t bme_i2c_port = I2C_NUM_0;

static bool bme_write_reg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    esp_err_t err = i2c_master_write_to_device(
        bme_i2c_port, BME280_ADDR, buf, 2,
        pdMS_TO_TICKS(100));
    return (err == ESP_OK);
}

static bool bme_read_reg(uint8_t reg, uint8_t *data, uint8_t len) {
    esp_err_t err = i2c_master_write_read_device(
        bme_i2c_port, BME280_ADDR, &reg, 1, data, len,
        pdMS_TO_TICKS(100));
    return (err == ESP_OK);
}
