// Create a queue that holds 10 sensor readings
QueueHandle_t sensor_queue = xQueueCreate(10, sizeof(SensorData));

// Producer (Core 0) - sends data
SensorData data = { temperature, humidity, millis() };
xQueueSend(sensor_queue, &data, 0);  // 0 = don't wait if full

// Consumer (Core 1) - receives data
SensorData data;
if (xQueueReceive(sensor_queue, &data, 0) == pdTRUE) {
    // Got new data - safe to use in LVGL calls
    lv_label_set_text_fmt(temp_label, "%.1f°C", data.temperature);
}
