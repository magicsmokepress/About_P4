static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area,
                          uint8_t *px_map)
{
    int w = lv_area_get_width(area);
    int h = lv_area_get_height(area);
    g_lcd->drawBitmap(area->x1, area->y1, w, h,
                      (const uint8_t *)px_map);
    lv_display_flush_ready(disp);
}
