/**
 * CrowPanel Advanced 7" ESP32-P4 — ESP-NOW Test
 *
 * ╔═══════════════════════════════════════════════════════════╗
 * ║  ESP-NOW is NOT SUPPORTED on ESP32-P4 (as of 2026-03)   ║
 * ╚═══════════════════════════════════════════════════════════╝
 *
 * The ESP32-P4 has no native WiFi — it proxies WiFi calls to the
 * ESP32-C6 co-processor via SDIO through the esp_wifi_remote
 * component. This proxy only supports basic WiFi APIs (STA/AP/TCP/IP).
 * ESP-NOW protocol tunneling was never implemented.
 *
 * Symptoms:
 *   - <esp_now.h> header IS found (from esp_wifi_remote includes)
 *   - Compilation succeeds (function declarations exist)
 *   - Linking FAILS with "undefined reference" to:
 *       esp_now_init, esp_now_send, esp_now_register_send_cb,
 *       esp_now_register_recv_cb, esp_now_add_peer
 *
 * Additional note: The send callback signature on P4 differs from
 * other ESP32 variants — it uses wifi_tx_info_t* instead of
 * const uint8_t* as the first parameter.
 *
 * Alternatives for peer-to-peer communication:
 *   - BLE (confirmed working via C6 co-processor)
 *   - WiFi UDP broadcast (uses supported WiFi APIs, needs router)
 *   - Community C6 firmware (github: esp32-p4-c6-espnow-enabler)
 *
 * This sketch is kept as documentation of the limitation.
 * It will NOT compile due to the missing linker symbols.
 */

#error "ESP-NOW is not supported on ESP32-P4. See comments above for alternatives."
