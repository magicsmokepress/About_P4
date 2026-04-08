if (WiFi.status() == WL_CONNECTED && !was_connected) {
    was_connected = true;
    // Update UI with IP and RSSI
} else if (WiFi.status() != WL_CONNECTED && was_connected) {
    was_connected = false;
    update_status("Disconnected");
}
