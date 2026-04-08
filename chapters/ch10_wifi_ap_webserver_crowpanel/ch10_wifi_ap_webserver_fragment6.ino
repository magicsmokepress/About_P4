if (!wifi_connected && WiFi.status() == WL_CONNECTED) {
    wifi_connected = true;
    show_connected_ui();
    if (!server_started) start_web_server();
}
