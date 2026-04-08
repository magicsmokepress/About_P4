static void update_spirit_level(float pitch, float roll) {
    // Scale tilt to pixels (3 px per degree)
    int bubble_x = (int)(-roll * 3.0);
    int bubble_y = (int)(-pitch * 3.0);

    // Clamp to circle boundary
    float dist = sqrt(bubble_x * bubble_x + bubble_y * bubble_y);
    int max_r = 120;  // circle radius in pixels
    if (dist > max_r) {
        bubble_x = (int)(bubble_x * max_r / dist);
        bubble_y = (int)(bubble_y * max_r / dist);
    }

    // Move bubble
    lv_obj_align(bubble, LV_ALIGN_CENTER, bubble_x, bubble_y);

    // Color: green = level, orange = tilted, red = very tilted
    uint32_t color;
    if (dist < 10) color = 0x00E676;        // level
    else if (dist < 50) color = 0xFFCA28;   // tilted
    else color = 0xFF5252;                    // very tilted

    lv_obj_set_style_bg_color(bubble,
        lv_color_hex(color), 0);
}
