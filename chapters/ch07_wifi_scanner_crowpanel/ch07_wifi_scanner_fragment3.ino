static lv_color_t rssi_color(int rssi) {
    if (rssi > -60) return lv_color_hex(0x00E676);  // green
    if (rssi > -80) return lv_color_hex(0xFFEB3B);  // yellow
    return lv_color_hex(0xFF5252);                    // red
}
