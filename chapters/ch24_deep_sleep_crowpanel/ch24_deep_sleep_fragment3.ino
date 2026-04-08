static uint32_t last_tick = 0;
uint32_t now = millis();
if (now - last_tick >= COUNTDOWN_TICK_MS) {
    last_tick = now;
    countdown_sec--;

    if (countdown_sec <= 0) {
        enter_deep_sleep();
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "Sleeping in:  %d seconds...", countdown_sec);
    lv_label_set_text(lbl_countdown, buf);
}
