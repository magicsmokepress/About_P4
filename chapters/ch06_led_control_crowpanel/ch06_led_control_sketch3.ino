void loop() {
    if (g_touch) {
        esp_lcd_touch_handle_t tp = g_touch->getHandle();
        if (tp) {
            uint16_t x[1], y[1], strength[1];
            uint8_t cnt = 0;
            esp_lcd_touch_read_data(tp);
            if (esp_lcd_touch_get_coordinates(tp, x, y, strength,
                                              &cnt, 1) && cnt > 0) {
                touch_pressed = true;
                touch_x = x[0];
                touch_y = y[0];
            } else {
                touch_pressed = false;
            }
        }
    }

    lv_timer_handler();
    delay(5);
}
