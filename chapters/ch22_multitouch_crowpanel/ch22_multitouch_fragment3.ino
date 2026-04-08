static void check_two_finger_scroll(uint16_t *x, uint16_t *y) {
    int16_t mid_x = (x[0] + x[1]) / 2;
    int16_t mid_y = (y[0] + y[1]) / 2;

    if (gesture.prev_mid_x != 0) {
        int dx = mid_x - gesture.prev_mid_x;
        int dy = mid_y - gesture.prev_mid_y;

        // Apply scroll (minimum 3px to filter noise)
        if (abs(dx) > 3 || abs(dy) > 3) {
            on_scroll(dx, dy);
        }
    }

    gesture.prev_mid_x = mid_x;
    gesture.prev_mid_y = mid_y;
}

static int pan_x = 0, pan_y = 0;

static void on_scroll(int dx, int dy) {
    pan_x += dx;
    pan_y += dy;
    lv_obj_set_pos(scroll_target, pan_x, pan_y);
}
