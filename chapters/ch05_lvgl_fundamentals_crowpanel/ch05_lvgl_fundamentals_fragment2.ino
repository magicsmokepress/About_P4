lv_init();

size_t buf_size = LCD_WIDTH * LVGL_BUF_LINES * sizeof(lv_color_t);
uint8_t *buf1 = (uint8_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);

lvgl_disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
lv_display_set_flush_cb(lvgl_disp, lvgl_flush_cb);
lv_display_set_buffers(lvgl_disp, buf1, NULL, buf_size,
                       LV_DISPLAY_RENDER_MODE_PARTIAL);
