void setup() {
    boot_count++;

    // Headless sensor cycle
    read_sensors();
    store_in_nvs();  // or transmit via WiFi

    // Only show display on button wake
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
        init_display();
        show_last_10_readings();
        delay(10000);  // show for 10 seconds
    }

    configure_next_wake();
    esp_deep_sleep_start();
}
