static lv_obj_t *create_gauge(lv_obj_t *parent,
    const char *title, int min_val, int max_val,
    lv_color_t color, int x_offset) {

    // Background arc (full sweep, dim)
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, 200, 200);
    lv_arc_set_range(arc, min_val, max_val);
    lv_arc_set_bg_angles(arc, 135, 45);   // 270° sweep
    lv_arc_set_value(arc, min_val);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(arc, LV_ALIGN_TOP_MID, x_offset, 30);

    // Style the indicator (colored part)
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 12, LV_PART_INDICATOR);

    // Style the background track (dim version of same color)
    lv_obj_set_style_arc_color(arc,
        lv_color_hex(0x333344), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 8, LV_PART_MAIN);

    // Value label in center
    lv_obj_t *val_lbl = lv_label_create(arc);
    lv_label_set_text(val_lbl, "--");
    lv_obj_set_style_text_font(val_lbl, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(val_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(val_lbl);

    // Title label below
    lv_obj_t *title_lbl = lv_label_create(parent);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_font(title_lbl,
        &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title_lbl, color, 0);
    lv_obj_align_to(title_lbl, arc, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);

    return arc;
}

// Create gauges for temperature and humidity
lv_obj_t *temp_gauge = create_gauge(tab_env, "Temperature",
    0, 50, lv_color_hex(0xFF5252), -150);
lv_obj_t *hum_gauge = create_gauge(tab_env, "Humidity",
    0, 100, lv_color_hex(0x4488FF), 150);
