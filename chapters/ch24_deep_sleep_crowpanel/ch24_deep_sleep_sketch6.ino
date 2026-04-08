RTC_DATA_ATTR uint64_t rtc_epoch_ms = 0;

void setup() {
    // On first boot, sync with NTP and store the base time
    if (boot_count == 0) {
        connect_wifi();
        sync_ntp();
        rtc_epoch_ms = get_ntp_epoch_ms();
    } else {
        // Add sleep duration to our epoch tracker
        rtc_epoch_ms += SLEEP_DURATION_SEC * 1000ULL;
    }
    // rtc_epoch_ms now approximates the real time
}
