// ─── Data structure shared via queue ──────────────────────────
struct ScanResult {
    int count;
    String ssids[20];
    int rssi[20];
};

QueueHandle_t scan_queue;

// ─── Core 0: Network task ─────────────────────────────────────
void network_task(void *pvParameters) {
    while (true) {
        // This blocks for 2-5 seconds - but Core 1 keeps running
        int n = WiFi.scanNetworks();

        if (n > 0) {
            ScanResult result;
            result.count = min(n, 20);
            for (int i = 0; i < result.count; i++) {
                result.ssids[i] = WiFi.SSID(i);
                result.rssi[i] = WiFi.RSSI(i);
            }
            xQueueSend(scan_queue, &result, 0);
            WiFi.scanDelete();
        }

        vTaskDelay(pdMS_TO_TICKS(30000));  // Scan every 30s
    }
}

// ─── Core 1: UI task ──────────────────────────────────────────
void ui_task(void *pvParameters) {
    while (true) {
        // Check for new scan results (non-blocking)
        ScanResult result;
        if (xQueueReceive(scan_queue, &result, 0) == pdTRUE) {
            update_network_list(result);  // LVGL calls here
        }

        // Poll touch
        poll_touch();

        // LVGL tick - runs every 5ms, NEVER interrupted
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ─── Setup ────────────────────────────────────────────────────
void setup() {
    // ... hardware init, LVGL init ...

    scan_queue = xQueueCreate(2, sizeof(ScanResult));

    xTaskCreatePinnedToCore(network_task, "net", 8192,
                            NULL, 1, NULL, 0);    // Core 0
    xTaskCreatePinnedToCore(ui_task, "ui", 16384,
                            NULL, 2, NULL, 1);     // Core 1
}

void loop() {
    vTaskDelay(portMAX_DELAY);  // Main loop does nothing
}
