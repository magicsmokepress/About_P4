static void build_network_tab(lv_obj_t *tab) {
    // Wi-Fi status with icon
    wifi_status = lv_label_create(tab);
    lv_label_set_text(wifi_status,
        LV_SYMBOL_WIFI " Connected");
    lv_obj_set_style_text_color(wifi_status,
        lv_color_hex(COLOR_OK), 0);

    // IP address (large)
    ip_lbl = lv_label_create(tab);
    lv_obj_set_style_text_font(ip_lbl,
        &lv_font_montserrat_36, 0);

    // RSSI signal bar
    rssi_bar = lv_bar_create(tab);
    lv_bar_set_range(rssi_bar, -90, -30);
    lv_obj_set_size(rssi_bar, 300, 20);

    // Uptime counter
    uptime_lbl = lv_label_create(tab);
}

// Update every second
static void update_network() {
    if (WiFi.status() == WL_CONNECTED) {
        lv_label_set_text_fmt(ip_lbl, "%s",
            WiFi.localIP().toString().c_str());
        lv_bar_set_value(rssi_bar, WiFi.RSSI(), LV_ANIM_ON);

        uint32_t up = millis() / 1000;
        lv_label_set_text_fmt(uptime_lbl,
            "Uptime: %02d:%02d:%02d",
            up / 3600, (up / 60) % 60, up % 60);
    }
}
