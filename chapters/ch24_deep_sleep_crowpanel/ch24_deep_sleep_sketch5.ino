RTC_DATA_ATTR int reading_count = 0;
#define BATCH_SIZE 12

void setup() {
    float temp = read_temperature();
    store_reading_in_nvs(reading_count, temp);
    reading_count++;

    if (reading_count >= BATCH_SIZE) {
        connect_wifi();
        upload_all_readings();
        reading_count = 0;
    }

    esp_sleep_enable_timer_wakeup(5 * 60 * 1000000ULL);  // 5 minutes
    esp_deep_sleep_start();
}
