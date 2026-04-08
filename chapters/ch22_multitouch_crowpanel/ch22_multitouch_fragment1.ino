static void check_pinch(uint16_t *x, uint16_t *y) {
    float dx = x[1] - x[0];
    float dy = y[1] - y[0];
    float dist = sqrtf(dx * dx + dy * dy);

    if (gesture.prev_dist > 0) {
        float scale = dist / gesture.prev_dist;

        // Threshold: ignore < 5% change (noise)
        if (scale > 1.05f) {
            // Pinch OUT - zoom in
            on_zoom(scale);
        } else if (scale < 0.95f) {
            // Pinch IN - zoom out
            on_zoom(scale);
        }
    }

    gesture.prev_dist = dist;
}

static float zoom_level = 1.0f;

static void on_zoom(float scale) {
    zoom_level *= scale;
    zoom_level = constrain(zoom_level, 0.5f, 5.0f);

    Serial.printf("Zoom: %.2fx\n", zoom_level);

    // Apply zoom to a visual element
    int new_size = (int)(200 * zoom_level);
    lv_obj_set_size(zoom_target, new_size, new_size);
}
