SemaphoreHandle_t lvgl_mutex = xSemaphoreCreateMutex();

// Core 0 - updating LVGL (emergency only)
if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50))) {
    lv_label_set_text(status, "Network error");
    xSemaphoreGive(lvgl_mutex);
}

// Core 1 - normal LVGL tick
if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(10))) {
    lv_timer_handler();
    xSemaphoreGive(lvgl_mutex);
}
