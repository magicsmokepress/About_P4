#include "driver/i2c.h"

#define MPU6050_ADDR        0x68
#define MPU6050_REG_PWR     0x6B
#define MPU6050_REG_WHO     0x75
#define MPU6050_REG_ACCEL   0x3B
#define MPU6050_REG_CONFIG  0x1A
#define MPU6050_REG_GYRO_CFG  0x1B
#define MPU6050_REG_ACCEL_CFG 0x1C

static i2c_port_t mpu_port = I2C_NUM_0;
static uint8_t mpu_addr = MPU6050_ADDR;

static bool mpu_write_reg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    return i2c_master_write_to_device(
        mpu_port, mpu_addr, buf, 2,
        pdMS_TO_TICKS(100)) == ESP_OK;
}

static bool mpu6050_init() {
    // Check WHO_AM_I register (should return 0x68)
    uint8_t who;
    uint8_t reg = MPU6050_REG_WHO;
    esp_err_t err = i2c_master_write_read_device(
        mpu_port, mpu_addr, &reg, 1, &who, 1,
        pdMS_TO_TICKS(100));

    if (err != ESP_OK) {
        // Try alternate address
        mpu_addr = 0x69;
        err = i2c_master_write_read_device(
            mpu_port, mpu_addr, &reg, 1, &who, 1,
            pdMS_TO_TICKS(100));
        if (err != ESP_OK) return false;
    }

    Serial.printf("[MPU6050] Found at 0x%02X, WHO_AM_I=0x%02X\n",
                  mpu_addr, who);

    // Wake up (clear sleep bit)
    mpu_write_reg(MPU6050_REG_PWR, 0x00);
    delay(100);

    // Set DLPF to ~20 Hz bandwidth for smooth readings
    mpu_write_reg(MPU6050_REG_CONFIG, 0x04);

    // Accel range: ±2g (most sensitive)
    mpu_write_reg(MPU6050_REG_ACCEL_CFG, 0x00);

    // Gyro range: ±250°/s (most sensitive)
    mpu_write_reg(MPU6050_REG_GYRO_CFG, 0x00);

    return true;
}
