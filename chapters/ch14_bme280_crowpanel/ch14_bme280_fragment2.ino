if (has_humidity) {
    // BME280 - show humidity
    snprintf(buf, sizeof(buf), "%.1f %%", bme_hum);
    lv_label_set_text(hum_lbl, buf);
} else {
    // BMP280 - grey out humidity section
    lv_label_set_text(hum_lbl, "N/A");
    lv_obj_set_style_text_color(hum_lbl,
        lv_color_hex(0x333344), 0);
}
