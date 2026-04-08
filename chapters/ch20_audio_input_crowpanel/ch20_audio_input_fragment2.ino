static void update_level_meter(const int16_t *samples, int count) {
    // Calculate RMS (Root Mean Square) amplitude
    float sum_sq = 0;
    for (int i = 0; i < count; i++) {
        float s = samples[i] / 32768.0f;
        sum_sq += s * s;
    }
    float rms = sqrtf(sum_sq / count);

    // Convert to dB relative to full scale
    float db = 20.0f * log10f(rms + 0.0001f);
    // -60 dB = silence, 0 dB = full scale

    // Map to LVGL bar (0-100%)
    int level = constrain((int)((db + 60) * 100 / 60), 0, 100);
    lv_bar_set_value(level_bar, level, LV_ANIM_ON);

    // Color: green for normal, yellow for loud, red for clipping
    uint32_t color;
    if (level < 60)       color = 0x00E676;
    else if (level < 85)  color = 0xFFCA28;
    else                  color = 0xFF5252;
    lv_obj_set_style_bg_color(level_bar,
        lv_color_hex(color), LV_PART_INDICATOR);
}
