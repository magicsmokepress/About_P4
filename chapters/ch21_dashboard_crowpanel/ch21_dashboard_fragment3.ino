lv_arc_set_value(temp_gauge, (int)temperature);
// The label inside the arc
lv_obj_t *val = lv_obj_get_child(temp_gauge, 0);
lv_label_set_text_fmt(val, "%.1f°", temperature);

// Dynamic color based on value
lv_color_t tc;
if (temperature < 18) tc = lv_color_hex(0x4488FF);       // cold
else if (temperature < 26) tc = lv_color_hex(0x00E676);   // comfort
else tc = lv_color_hex(0xFF5252);                           // hot
lv_obj_set_style_arc_color(temp_gauge, tc, LV_PART_INDICATOR);
