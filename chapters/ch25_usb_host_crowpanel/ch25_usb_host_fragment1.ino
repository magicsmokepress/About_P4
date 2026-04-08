#include "usb/usb_host.h"

static void usb_host_task(void *pvParameters) {
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    usb_host_install(&host_config);

    while (true) {
        usb_host_lib_handle_events(portMAX_DELAY, NULL);
    }
}
