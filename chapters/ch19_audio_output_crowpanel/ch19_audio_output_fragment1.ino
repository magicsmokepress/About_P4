#include "driver/i2s_std.h"

#define I2S_BCLK_PIN   22
#define I2S_LRCLK_PIN  21
#define I2S_SDATA_PIN  23
#define SAMPLE_RATE    16000

static i2s_chan_handle_t tx_handle = NULL;

static bool init_i2s() {
    // Channel config
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(
        I2S_NUM_0, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&chan_cfg, &tx_handle, NULL);
    if (err != ESP_OK) return false;

    // Standard mode config
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)I2S_BCLK_PIN,
            .ws = (gpio_num_t)I2S_LRCLK_PIN,
            .dout = (gpio_num_t)I2S_SDATA_PIN,
            .din = I2S_GPIO_UNUSED,
        },
    };

    err = i2s_channel_init_std_mode(tx_handle, &std_cfg);
    if (err != ESP_OK) return false;

    err = i2s_channel_enable(tx_handle);
    return (err == ESP_OK);
}
