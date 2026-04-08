snprintf(buf, sizeof(buf), "%s  %s  (%d dBm)%s",
         LV_SYMBOL_WIFI,
         WiFi.SSID(i).c_str(),
         WiFi.RSSI(i),
         WiFi.encryptionType(i) == WIFI_AUTH_OPEN
             ? "" : "  " LV_SYMBOL_EYE_CLOSE);
