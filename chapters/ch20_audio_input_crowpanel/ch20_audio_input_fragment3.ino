static void build_audio_ui() {
    // ... standard screen setup ...

    // Level meter bar
    level_bar = lv_bar_create(scr);
    lv_obj_set_size(level_bar, 600, 30);
    lv_bar_set_range(level_bar, 0, 100);
    lv_obj_align(level_bar, LV_ALIGN_CENTER, 0, -60);

    // Duration label (updates during recording)
    dur_lbl = lv_label_create(scr);
    lv_label_set_text(dur_lbl, "0.0 / 5.0 sec");
    lv_obj_align(dur_lbl, LV_ALIGN_CENTER, 0, -20);

    // Record button
    lv_obj_t *btn_rec = lv_button_create(scr);
    lv_obj_set_size(btn_rec, 200, 80);
    lv_obj_align(btn_rec, LV_ALIGN_BOTTOM_LEFT, 80, -40);
    lv_obj_set_style_bg_color(btn_rec, lv_color_hex(0xD32F2F), 0);
    lv_obj_add_event_cb(btn_rec, cb_record, LV_EVENT_CLICKED, NULL);
    // ... label, play button, stop button ...
}
