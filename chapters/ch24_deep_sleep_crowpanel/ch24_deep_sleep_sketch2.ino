void setup() {
    boot_count++;

    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
        // Button press - user wants to see the display
        init_display();
        show_dashboard();
        wait_for_timeout_or_button();
    } else {
        // Timer wake - headless sensor cycle
        read_sensor();
        transmit_data();
    }

    esp_deep_sleep_start();
}
