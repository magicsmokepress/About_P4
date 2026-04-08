void loop() {
    // ❌ This freezes the display for 2-5 seconds
    int n = WiFi.scanNetworks();

    // ❌ Touch events during the scan are lost forever
    // ❌ Animations stopped mid-frame
    // ❌ Status label never showed "Scanning..."

    update_network_list(n);

    lv_timer_handler();  // Only runs AFTER the blocking call
    delay(5);
}
