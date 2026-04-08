static void set_led(bool on) {
    led_state = on;
    digitalWrite(LED_GPIO, on ? HIGH : LOW);

    if (status_led) {
        lv_obj_set_style_bg_color(status_led,
            on ? lv_color_hex(0x00E676) : lv_color_hex(0x616161), 0);
    }
    if (status_label) {
        lv_label_set_text(status_label,
            on ? "LED is ON" : "LED is OFF");
    }
}
