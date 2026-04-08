static void show_connected_ui() {
    lv_obj_add_flag(net_list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(scan_btn, LV_OBJ_FLAG_HIDDEN);

    char buf[128];
    snprintf(buf, sizeof(buf), "Connected to %s",
        WiFi.SSID().c_str());
    update_status(buf);

    snprintf(buf, sizeof(buf), "http://%s",
        WiFi.localIP().toString().c_str());
    lv_label_set_text(ip_lbl, buf);
}
