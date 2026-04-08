static void kb_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        const char *pwd = lv_textarea_get_text(pwd_ta);
        WiFi.begin(selected_ssid.c_str(), pwd);
        lv_obj_add_flag(pwd_panel, LV_OBJ_FLAG_HIDDEN);
    } else if (code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(pwd_panel, LV_OBJ_FLAG_HIDDEN);
    }
}
