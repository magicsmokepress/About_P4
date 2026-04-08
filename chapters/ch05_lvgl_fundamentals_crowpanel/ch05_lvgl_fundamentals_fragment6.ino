lv_obj_t *label = lv_label_create(lv_screen_active());
lv_label_set_text(label, "Hello World!");
lv_obj_set_style_text_font(label, &lv_font_montserrat_48, 0);
lv_obj_set_style_text_color(label, lv_color_hex(0xFF5529), 0);
lv_obj_center(label);
