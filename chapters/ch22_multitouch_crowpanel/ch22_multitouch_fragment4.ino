static void on_swipe_left() {
    uint32_t idx = lv_tabview_get_tab_active(tabview);
    if (idx < 2) lv_tabview_set_active(tabview, idx + 1, LV_ANIM_ON);
}

static void on_swipe_right() {
    uint32_t idx = lv_tabview_get_tab_active(tabview);
    if (idx > 0) lv_tabview_set_active(tabview, idx - 1, LV_ANIM_ON);
}
