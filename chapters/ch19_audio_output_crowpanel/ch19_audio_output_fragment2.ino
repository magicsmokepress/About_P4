// Frequency buttons
static void cb_freq(lv_event_t *e) {
    float *freq = (float *)lv_event_get_user_data(e);
    tone_freq = *freq;
    lv_label_set_text_fmt(freq_lbl, "%.0f Hz", tone_freq);
}

// Tone play/stop
static void cb_tone_toggle(lv_event_t *e) {
    tone_playing = !tone_playing;
    if (!tone_playing) {
        // Write silence to flush I2S buffer
        int16_t silence[256] = {0};
        size_t written;
        i2s_channel_write(tx_handle, silence, sizeof(silence),
                          &written, pdMS_TO_TICKS(100));
    }
}
