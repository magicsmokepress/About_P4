void loop() {
    // Always read mic for level meter
    int16_t level_buf[256];
    size_t bytes_read;
    i2s_channel_read(rx_handle, level_buf, sizeof(level_buf),
                     &bytes_read, pdMS_TO_TICKS(50));
    update_level_meter(level_buf, bytes_read / sizeof(int16_t));

    // If recording, also store to buffer
    if (recording) record_chunk();

    lv_timer_handler();
    delay(10);
}
