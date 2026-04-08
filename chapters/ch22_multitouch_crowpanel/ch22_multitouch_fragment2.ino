static void on_zoom(float scale) {
    zoom_level *= scale;
    zoom_level = constrain(zoom_level, 0.5f, 4.0f);

    // Adjust Y-axis range
    int range = (int)(50 / zoom_level);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y,
        (int)(25 - range / 2), (int)(25 + range / 2));
    lv_chart_refresh(chart);
}
