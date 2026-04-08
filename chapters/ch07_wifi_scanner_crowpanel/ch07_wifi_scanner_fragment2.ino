int n = WiFi.scanNetworks();
if (n < 0) {
    delay(2000);
    n = WiFi.scanNetworks();
}
