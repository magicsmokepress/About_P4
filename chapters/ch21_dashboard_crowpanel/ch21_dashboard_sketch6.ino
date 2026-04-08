void loop() {
    // Touch polling
    poll_touch();

    // Sensor reads (every second)
    static uint32_t last_read = 0;
    if (millis() - last_read >= 1000) {
        last_read = millis();

        // Read sensors
        bme280_read();

        // Update Environment tab
        lv_arc_set_value(temp_gauge, (int)bme_temp);
        lv_arc_set_value(hum_gauge, (int)bme_hum);

        // Update Charts tab
        lv_chart_set_next_value(chart, temp_series, (int)bme_temp);
        lv_chart_set_next_value(chart, hum_series, (int)bme_hum);
        lv_chart_refresh(chart);

        // Update Network tab
        update_network();
    }

    lv_timer_handler();
    delay(5);
}
