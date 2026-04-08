static uint32_t frame_count = 0;

void loop() {
    // ... existing frame capture code ...
    if (esp_cam_ctlr_receive(cam_handle, &t, 100) == ESP_OK) {
        // Auto-expose every 60 frames
        if (++frame_count % 60 == 0) {
            auto_expose(raw_buf, CAM_H, CAM_V);
        }
        bayer_to_rgb565(raw_buf, rgb_buf, CAM_H, CAM_V);
        M5.Display.pushImage(0, 0, DISP_H, DISP_V, rgb_buf);
    }
}
