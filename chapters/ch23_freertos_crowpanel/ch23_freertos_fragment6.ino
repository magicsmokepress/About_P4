// network_task on Core 0
void network_task(void *param) {
    while (true) {
        if (need_dhcp) {
            NetEvent evt = { .type = NET_DHCP_START };
            xQueueSend(net_event_queue, &evt, 0);

            bool ok = do_dhcp();  // Blocks 3-10s - UI unaffected

            evt.type = ok ? NET_DHCP_OK : NET_DHCP_FAIL;
            memcpy(evt.ip, cur_ip, 4);
            xQueueSend(net_event_queue, &evt, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ui_task on Core 1
void ui_task(void *param) {
    while (true) {
        NetEvent evt;
        if (xQueueReceive(net_event_queue, &evt, 0) == pdTRUE) {
            switch (evt.type) {
                case NET_DHCP_START:
                    show_status("DHCP...", COLOR_AMBER);
                    break;
                case NET_DHCP_OK:
                    show_ip(evt.ip);
                    show_status("Connected", COLOR_GREEN);
                    break;
                case NET_DHCP_FAIL:
                    show_status("DHCP failed", COLOR_RED);
                    break;
            }
        }

        update_ring_animations();  // Never interrupted
        lv_timer_handler();        // Every 5ms, always
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
