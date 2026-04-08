static void lvgl_touch_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp =
        (esp_lcd_touch_handle_t)lv_indev_get_user_data(indev);
    uint16_t x[1], y[1], strength[1];
    uint8_t cnt = 0;

    esp_lcd_touch_read_data(tp);
    if (esp_lcd_touch_get_coordinates(tp, x, y, strength, &cnt, 1)
        && cnt > 0) {
        data->point.x = x[0];
        data->point.y = y[0];
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state   = LV_INDEV_STATE_RELEASED;
    }
}
