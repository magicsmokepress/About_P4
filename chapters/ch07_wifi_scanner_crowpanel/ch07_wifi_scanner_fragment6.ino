static void lvgl_flush_cb(lv_display_t *disp,
                          const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_handle_t panel = g_lcd->getHandle();
    if (panel) {
        esp_err_t ret;
        do {
            ret = esp_lcd_panel_draw_bitmap(panel,
                area->x1, area->y1,
                area->x2 + 1, area->y2 + 1, px_map);
            if (ret == ESP_ERR_INVALID_STATE) delay(1);
        } while (ret == ESP_ERR_INVALID_STATE);
    }
    lv_display_flush_ready(disp);
}
