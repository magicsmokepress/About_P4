#define CHART_POINTS 60  // 60 seconds of history

lv_obj_t *chart = lv_chart_create(tab_charts);
lv_obj_set_size(chart, 900, 300);
lv_obj_align(chart, LV_ALIGN_TOP_MID, 0, 10);
lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
lv_chart_set_point_count(chart, CHART_POINTS);
lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 50);

// Style
lv_obj_set_style_bg_color(chart, lv_color_hex(0x0F0F23), 0);
lv_obj_set_style_border_color(chart, lv_color_hex(0x333355), 0);
lv_obj_set_style_line_color(chart,
    lv_color_hex(0x222244), LV_PART_MAIN);  // grid lines

// Add temperature series (red line)
lv_chart_series_t *temp_series = lv_chart_add_series(chart,
    lv_color_hex(0xFF5252), LV_CHART_AXIS_PRIMARY_Y);

// Add humidity series (blue line)
lv_chart_series_t *hum_series = lv_chart_add_series(chart,
    lv_color_hex(0x4488FF), LV_CHART_AXIS_PRIMARY_Y);
