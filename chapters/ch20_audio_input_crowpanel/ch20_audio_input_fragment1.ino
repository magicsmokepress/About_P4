#include "driver/i2s_pdm.h"

#define MIC_CLK  24
#define MIC_DIN  26
#define MIC_SAMPLE_RATE 16000

static i2s_chan_handle_t rx_handle = NULL;

static bool init_mic() {
    i2s_chan_config_t rx_cfg = I2S_CHANNEL_DEFAULT_CONFIG(
        I2S_NUM_1, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&rx_cfg, NULL, &rx_handle);
    if (err != ESP_OK) return false;

    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(MIC_SAMPLE_RATE),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = (gpio_num_t)MIC_CLK,
            .din = (gpio_num_t)MIC_DIN,
        },
    };

    err = i2s_channel_init_pdm_rx_mode(rx_handle, &pdm_cfg);
    if (err != ESP_OK) return false;

    return i2s_channel_enable(rx_handle) == ESP_OK;
}
