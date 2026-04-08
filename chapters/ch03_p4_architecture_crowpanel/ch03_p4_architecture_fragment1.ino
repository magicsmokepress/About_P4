#include "esp_ldo_regulator.h"

esp_ldo_channel_handle_t ldo3 = NULL;
esp_ldo_channel_config_t ldo3_cfg = {
    .chan_id = 3,
    .voltage_mv = 2500,
};
esp_ldo_acquire_channel(&ldo3_cfg, &ldo3);
